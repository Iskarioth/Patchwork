#pragma once

#include "../engine/Module.h"
#include "../engine/Dsp.h"

// ------------------------------------------------------------------ Filter
class FilterModule : public ModuleProcessor
{
public:
    FilterModule()
        : ModuleProcessor ({ { "In", PortType::Audio, 0 }, { "Cutoff mod", PortType::Mod, 2 } },
                           { { "Out", PortType::Audio, 0 } })
    {
        mode     = addChoice ("mode", "Mode", { "Lowpass", "Highpass", "Bandpass" }, 0);
        cutoff   = addFloat ("cutoff", "Cutoff", logRange (30.0f, 18000.0f), 1200.0f, "Hz");
        reso     = addFloat ("reso", "Resonance", { 0.0f, 1.0f }, 0.3f);
        modDepth = addFloat ("moddepth", "Mod depth", { 0.0f, 1.0f }, 0.5f);
        drive    = addFloat ("drive", "Drive", { 0.0f, 1.0f }, 0.0f);
    }

    juce::String getTypeId() const override { return "filter"; }
    juce::String getDisplayName() const override { return "Filter"; }

    void prepareToPlay (double newRate, int) override { sr = newRate; smoothed = cutoff->get(); }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int m = mode->getIndex();
        const float q = 0.5f + reso->get() * reso->get() * 16.0f;
        const float depth = modDepth->get() * 6.0f; // octaves at full mod
        const float target = cutoff->get();
        const float gain = 1.0f + drive->get() * 8.0f;
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        const auto* mod = buf.getReadPointer (2);

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            smoothed += (target - smoothed) * 0.002f;
            if ((i & 3) == 0)
            {
                const float fc = smoothed * std::exp2 (mod[i] * depth);
                left.set (fc, q, sr);
                right.set (fc, q, sr);
            }
            float inL = L[i], inR = R[i];
            if (gain > 1.0f) { inL = std::tanh (inL * gain); inR = std::tanh (inR * gain); }
            const auto a = left.process (inL), b = right.process (inR);
            L[i] = m == 0 ? a.lp : m == 1 ? a.hp : a.bp;
            R[i] = m == 0 ? b.lp : m == 1 ? b.hp : b.bp;
        }
    }

private:
    double sr = 44100;
    float smoothed = 1000;
    pw::Svf left, right;
    juce::AudioParameterChoice* mode;
    juce::AudioParameterFloat *cutoff, *reso, *modDepth, *drive;
};

// ------------------------------------------------------------------ Distortion
class DistortionModule : public ModuleProcessor
{
public:
    DistortionModule()
        : ModuleProcessor ({ { "In", PortType::Audio, 0 } }, { { "Out", PortType::Audio, 0 } })
    {
        drive  = addFloat ("drive", "Drive", { 0.0f, 36.0f }, 12.0f, "dB");
        tone   = addFloat ("tone", "Tone", logRange (500.0f, 18000.0f), 6000.0f, "Hz");
        mix    = addFloat ("mix", "Mix", { 0.0f, 1.0f }, 1.0f);
        output = addFloat ("output", "Output", { -24.0f, 6.0f }, -6.0f, "dB");
    }

    juce::String getTypeId() const override { return "distortion"; }
    juce::String getDisplayName() const override { return "Distortion"; }

    void prepareToPlay (double newRate, int) override { sr = newRate; }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const float g = juce::Decibels::decibelsToGain (drive->get());
        const float out = juce::Decibels::decibelsToGain (output->get());
        const float wet = mix->get();
        toneL.setCutoff (tone->get(), sr);
        toneR.setCutoff (tone->get(), sr);
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            // a little asymmetry adds even harmonics
            const float dl = toneL.process (std::tanh (L[i] * g + 0.1f) - std::tanh (0.1f));
            const float dr = toneR.process (std::tanh (R[i] * g + 0.1f) - std::tanh (0.1f));
            L[i] = (L[i] * (1 - wet) + dl * wet) * out;
            R[i] = (R[i] * (1 - wet) + dr * wet) * out;
        }
    }

private:
    double sr = 44100;
    pw::OnePole toneL, toneR;
    juce::AudioParameterFloat *drive, *tone, *mix, *output;
};

// ------------------------------------------------------------------ Delay (tempo synced)
class DelayModule : public ModuleProcessor
{
public:
    DelayModule()
        : ModuleProcessor ({ { "In", PortType::Audio, 0 } }, { { "Out", PortType::Audio, 0 } })
    {
        time     = addChoice ("time", "Time", { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" }, 2);
        pingPong = addChoice ("pingpong", "Ping-pong", { "Off", "On" }, 1);
        feedback = addFloat ("feedback", "Feedback", { 0.0f, 0.95f }, 0.45f);
        damping  = addFloat ("damping", "Damping", logRange (800.0f, 18000.0f), 5000.0f, "Hz");
        mix      = addFloat ("mix", "Mix", { 0.0f, 1.0f }, 0.3f);
    }

    juce::String getTypeId() const override { return "delay"; }
    juce::String getDisplayName() const override { return "Delay"; }

    void prepareToPlay (double newRate, int) override
    {
        sr = newRate;
        size = (int) (sr * 4.0) + 2;
        line.setSize (2, size);
        line.clear();
        writePos = 0;
        currentDelay = -1;
    }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        static constexpr double beats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        const double tempo = transport != nullptr ? transport->bpm.load() : 120.0;
        const double target = juce::jlimit (1.0, (double) size - 2, beats[time->getIndex()] * 60.0 / tempo * sr);
        if (currentDelay < 0) currentDelay = target;

        const bool pp = pingPong->getIndex() == 1;
        const float fb = feedback->get(), wet = mix->get();
        dampL.setCutoff (damping->get(), sr);
        dampR.setCutoff (damping->get(), sr);
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        auto* dl = line.getWritePointer (0);
        auto* dr = line.getWritePointer (1);

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            currentDelay += (target - currentDelay) * 0.0005; // glide when the tempo or time changes
            double rp = writePos - currentDelay;
            if (rp < 0) rp += size;
            const int i0 = (int) rp, i1 = (i0 + 1) % size;
            const float frac = (float) (rp - i0);
            const float yl = dl[i0] + (dl[i1] - dl[i0]) * frac;
            const float yr = dr[i0] + (dr[i1] - dr[i0]) * frac;

            const float inL = L[i], inR = R[i];
            if (pp)
            {
                dl[writePos] = (inL + inR) * 0.5f + dampR.process (yr) * fb;
                dr[writePos] = dampL.process (yl) * fb;
            }
            else
            {
                dl[writePos] = inL + dampL.process (yl) * fb;
                dr[writePos] = inR + dampR.process (yr) * fb;
            }
            writePos = (writePos + 1) % size;

            L[i] = inL + yl * wet;
            R[i] = inR + yr * wet;
        }
    }

private:
    double sr = 44100, currentDelay = -1;
    int size = 1, writePos = 0;
    juce::AudioBuffer<float> line;
    pw::OnePole dampL, dampR;
    juce::AudioParameterChoice *time, *pingPong;
    juce::AudioParameterFloat *feedback, *damping, *mix;
};

// ------------------------------------------------------------------ Reverb
class ReverbModule : public ModuleProcessor
{
public:
    ReverbModule()
        : ModuleProcessor ({ { "In", PortType::Audio, 0 } }, { { "Out", PortType::Audio, 0 } })
    {
        roomSize = addFloat ("size", "Size", { 0.0f, 1.0f }, 0.7f);
        damping  = addFloat ("damping", "Damping", { 0.0f, 1.0f }, 0.4f);
        width    = addFloat ("width", "Width", { 0.0f, 1.0f }, 1.0f);
        mix      = addFloat ("mix", "Mix", { 0.0f, 1.0f }, 0.25f);
    }

    juce::String getTypeId() const override { return "reverb"; }
    juce::String getDisplayName() const override { return "Reverb"; }

    void prepareToPlay (double newRate, int) override { reverb.setSampleRate (newRate); reverb.reset(); }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        juce::Reverb::Parameters p;
        p.roomSize = roomSize->get();
        p.damping = damping->get();
        p.width = width->get();
        p.wetLevel = mix->get() * 0.6f;
        p.dryLevel = 1.0f - mix->get() * 0.5f;
        reverb.setParameters (p);
        reverb.processStereo (buf.getWritePointer (0), buf.getWritePointer (1), buf.getNumSamples());
    }

private:
    juce::Reverb reverb;
    juce::AudioParameterFloat *roomSize, *damping, *width, *mix;
};

// ------------------------------------------------------------------ LFO
class LfoModule : public ModuleProcessor
{
public:
    LfoModule() : ModuleProcessor ({}, { { "Out", PortType::Mod, 0 } })
    {
        shape  = addChoice ("shape", "Shape", { "Sine", "Triangle", "Saw", "Square", "Random" }, 0);
        sync   = addChoice ("sync", "Sync", { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 0);
        rate   = addFloat ("rate", "Rate", logRange (0.02f, 20.0f), 1.0f, "Hz");
        depth  = addFloat ("depth", "Depth", { 0.0f, 1.0f }, 0.5f);
        offset = addFloat ("offset", "Offset", { -1.0f, 1.0f }, 0.0f);
    }

    juce::String getTypeId() const override { return "lfo"; }
    juce::String getDisplayName() const override { return "LFO"; }
    float getMeterLevel() const override { return 0.5f + 0.5f * lastValue.load(); }

    void prepareToPlay (double newRate, int) override { sr = newRate; }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        static constexpr double syncBeats[] = { 0, 16, 8, 4, 2, 1, 0.5, 0.25 };
        const int s = sync->getIndex();
        const double tempo = transport != nullptr ? transport->bpm.load() : 120.0;
        const double cycle = s == 0 ? sr / rate->get() : syncBeats[s] * 60.0 / tempo * sr;

        // when synced and playing, lock the phase to the song position
        if (s != 0 && transport != nullptr && transport->blockPlaying.load())
            phase = std::fmod ((double) transport->position.load() / cycle, 1.0);

        const double inc = 1.0 / cycle;
        const int sh = shape->getIndex();
        const float d = depth->get(), o = offset->get();
        auto* out = buf.getWritePointer (0);
        float v = 0;

        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            switch (sh)
            {
                case 0:  v = std::sin (pw::twoPi * (float) phase); break;
                case 1:  v = (float) (1.0 - 4.0 * std::abs (phase - 0.5)); break;
                case 2:  v = (float) (1.0 - 2.0 * phase); break;
                case 3:  v = phase < 0.5 ? 1.0f : -1.0f; break;
                default: v = held; break;
            }
            out[i] = juce::jlimit (-1.0f, 1.0f, o + d * v);
            phase += inc;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                held = noise.next();
            }
        }
        lastValue = juce::jlimit (-1.0f, 1.0f, o + d * v);
    }

private:
    double sr = 44100, phase = 0;
    float held = 0;
    pw::Noise noise;
    std::atomic<float> lastValue { 0 };
    juce::AudioParameterChoice *shape, *sync;
    juce::AudioParameterFloat *rate, *depth, *offset;
};

// ------------------------------------------------------------------ Output
// Master volume, a safety soft-clipper and a meter. Wired to the sound card by the Patch.
class OutputModule : public ModuleProcessor
{
public:
    OutputModule() : ModuleProcessor ({ { "In", PortType::Audio, 0 } }, {}, 2)
    {
        volume = addFloat ("volume", "Volume", { -60.0f, 6.0f }, -6.0f, "dB");
    }

    juce::String getTypeId() const override { return "output"; }
    juce::String getDisplayName() const override { return "Output"; }
    float getMeterLevel() const override { return meter.load(); }

    void prepareToPlay (double, int) override {}

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const float g = juce::Decibels::decibelsToGain (volume->get(), -60.0f);
        float peak = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buf.getWritePointer (c);
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                d[i] = pw::softClip (d[i] * g);
                peak = std::max (peak, std::abs (d[i]));
            }
        }
        meter = std::max (peak, meter.load() * 0.9f);
    }

private:
    std::atomic<float> meter { 0 };
    juce::AudioParameterFloat* volume;
};
