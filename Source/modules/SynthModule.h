#pragma once

#include "../engine/Module.h"
#include "../engine/Dsp.h"

namespace synth_detail
{
struct Params
{
    int wave = 0;
    float detune = 8, cutoff = 2000, q = 0.7f, envAmt = 0.3f, fdecay = 0.3f, volume = 0.6f;
    juce::ADSR::Parameters adsr;
    const float* cutMod = nullptr;   // per-sample mod input, -1..1
    const float* pitchMod = nullptr;
};

struct Sound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Two detuned oscillators -> resonant lowpass with its own envelope -> amp ADSR.
class Voice : public juce::SynthesiserVoice
{
public:
    explicit Voice (const Params& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

    void startNote (int note, float vel, juce::SynthesiserSound*, int) override
    {
        freq = juce::MidiMessage::getMidiNoteInHertz (note);
        velocity = vel;
        phase1 = 0.0;
        phase2 = 0.37;
        filterEnv = 1.0f;
        filter.reset();
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters (params.adsr);
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            adsr.noteOff();
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        const double sr = getSampleRate();
        adsr.setParameters (params.adsr);
        const float fCoef = (float) std::exp (-4.6 / (std::max (0.005, (double) params.fdecay) * sr));
        const double detRatio = std::pow (2.0, params.detune / 1200.0);

        for (int i = start; i < start + num; ++i)
        {
            double f = freq;
            if (const float pm = params.pitchMod[i]; std::abs (pm) > 1.0e-6f)
                f *= std::exp2 (pm * 2.0 / 12.0); // +-2 semitones at full mod

            const double dt1 = f * detRatio / sr, dt2 = f / detRatio / sr;
            const float s = 0.5f * (pw::oscillator (params.wave, phase1, dt1) + pw::oscillator (params.wave, phase2, dt2));
            phase1 += dt1; if (phase1 >= 1.0) phase1 -= 1.0;
            phase2 += dt2; if (phase2 >= 1.0) phase2 -= 1.0;

            filterEnv *= fCoef;
            const float octaves = params.envAmt * 7.0f * filterEnv * velocity + params.cutMod[i] * 4.0f;
            filter.set (params.cutoff * std::exp2 (octaves), params.q, sr);
            const float y = filter.process (s).lp * adsr.getNextSample() * velocity * params.volume * 0.3f;

            out.addSample (0, i, y);
            out.addSample (1, i, y);

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    const Params& params;
    juce::ADSR adsr;
    pw::Svf filter;
    double freq = 440, phase1 = 0, phase2 = 0;
    float velocity = 1, filterEnv = 0;
};
} // namespace synth_detail

// 8-voice subtractive synth.
class SynthModule : public ModuleProcessor
{
public:
    SynthModule()
        : ModuleProcessor ({ { "Notes", PortType::Note, 0 },
                             { "Cutoff mod", PortType::Mod, 0 },
                             { "Pitch mod", PortType::Mod, 1 } },
                           { { "Out", PortType::Audio, 0 } })
    {
        wave     = addChoice ("wave", "Wave", { "Saw", "Square", "Triangle", "Sine" }, 0);
        detune   = addFloat ("detune", "Detune", { 0.0f, 50.0f }, 8.0f, "ct");
        attack   = addFloat ("attack", "Attack", logRange (0.001f, 3.0f), 0.01f, "s");
        decay    = addFloat ("decay", "Decay", logRange (0.01f, 3.0f), 0.3f, "s");
        sustain  = addFloat ("sustain", "Sustain", { 0.0f, 1.0f }, 0.7f);
        release  = addFloat ("release", "Release", logRange (0.01f, 5.0f), 0.4f, "s");
        cutoff   = addFloat ("cutoff", "Cutoff", logRange (30.0f, 18000.0f), 2400.0f, "Hz");
        reso     = addFloat ("reso", "Resonance", { 0.0f, 1.0f }, 0.25f);
        envAmt   = addFloat ("envamt", "Filter env", { 0.0f, 1.0f }, 0.3f);
        fdecay   = addFloat ("fdecay", "Env decay", logRange (0.01f, 3.0f), 0.35f, "s");
        volume   = addFloat ("volume", "Volume", { 0.0f, 1.0f }, 0.6f);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new synth_detail::Voice (params));
        synth.addSound (new synth_detail::Sound());
    }

    juce::String getTypeId() const override { return "synth"; }
    juce::String getDisplayName() const override { return "Synth"; }

    void prepareToPlay (double sr, int maxBlock) override
    {
        synth.setCurrentPlaybackSampleRate (sr);
        modBuffer.setSize (2, maxBlock);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();

        // inputs and outputs share channels, so grab the mod signals before clearing
        modBuffer.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            if (c < buffer.getNumChannels()) modBuffer.copyFrom (c, 0, buffer, c, 0, n);
            else modBuffer.clear (c, 0, n);

        params.wave = wave->getIndex();
        params.detune = detune->get();
        params.cutoff = cutoff->get();
        params.q = 0.5f + reso->get() * reso->get() * 14.0f;
        params.envAmt = envAmt->get();
        params.fdecay = fdecay->get();
        params.volume = volume->get();
        params.adsr = { attack->get(), decay->get(), sustain->get(), release->get() };
        params.cutMod = modBuffer.getReadPointer (0);
        params.pitchMod = modBuffer.getReadPointer (1);

        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, n);
        midi.clear();
    }

private:
    synth_detail::Params params;
    juce::Synthesiser synth;
    juce::AudioBuffer<float> modBuffer;
    juce::AudioParameterChoice* wave;
    juce::AudioParameterFloat *detune, *attack, *decay, *sustain, *release, *cutoff, *reso, *envAmt, *fdecay, *volume;
};
