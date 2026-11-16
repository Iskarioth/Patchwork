#pragma once

#include "../engine/Module.h"
#include "../engine/Dsp.h"

// Fully synthesized drum kit, played by GM drum notes on its Note input.
class DrumMachineModule : public ModuleProcessor
{
public:
    DrumMachineModule()
        : ModuleProcessor ({ { "Notes", PortType::Note, 0 } }, { { "Out", PortType::Audio, 0 } })
    {
        kickPitch  = addFloat ("kickpitch", "Kick pitch", { 30.0f, 90.0f }, 45.0f, "Hz");
        kickDecay  = addFloat ("kickdecay", "Kick decay", logRange (0.08f, 1.5f), 0.45f, "s");
        snareDecay = addFloat ("snaredecay", "Snare decay", logRange (0.04f, 0.6f), 0.18f, "s");
        clapDecay  = addFloat ("clapdecay", "Clap decay", logRange (0.05f, 0.8f), 0.22f, "s");
        hatDecay   = addFloat ("hatdecay", "Hat decay", logRange (0.01f, 0.25f), 0.045f, "s");
        hatTone    = addFloat ("hattone", "Hat tone", logRange (3000.0f, 14000.0f), 7000.0f, "Hz");
        tomTune    = addFloat ("tomtune", "Tom tune", { 0.5f, 2.0f }, 1.0f, "x");
        volume     = addFloat ("volume", "Volume", { 0.0f, 1.0f }, 0.8f);
    }

    juce::String getTypeId() const override { return "drums"; }
    juce::String getDisplayName() const override { return "Drum Machine"; }

    void prepareToPlay (double newRate, int) override
    {
        sr = newRate;
        clapFilter.set (1400.0f, 1.2f, sr);
        rimFilter.set (2200.0f, 4.0f, sr);
        snareHp.set (1200.0f, 0.7f, sr);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        chhHp.set (hatTone->get(), 0.7f, sr);  ohhHp.set (hatTone->get(), 0.7f, sr);
        chhBp.set (hatTone->get() * 1.4f, 0.8f, sr); ohhBp.set (hatTone->get() * 1.4f, 0.8f, sr);

        int pos = 0;
        for (const auto meta : midi)
        {
            render (buffer, pos, meta.samplePosition);
            pos = meta.samplePosition;
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                trigger (msg.getNoteNumber(), msg.getFloatVelocity());
        }
        render (buffer, pos, buffer.getNumSamples());
        midi.clear();
    }

private:
    // pitched voice with a falling pitch envelope: kick and toms
    struct Tonal
    {
        double phase = 0;
        float fStart = 100, fEnd = 50, pe = 0, pc = 0;
        pw::DecayEnv amp;

        void trigger (float vel, float start, float end, float pitchTau, float decay, double sr)
        {
            fStart = start; fEnd = end; pe = 1.0f; phase = 0;
            pc = (float) std::exp (-1.0 / (pitchTau * sr));
            amp.trigger (vel, decay, sr);
        }

        float next (double sr)
        {
            if (amp.v <= 0) return 0;
            const float f = fEnd + (fStart - fEnd) * pe;
            pe *= pc;
            phase += f / sr;
            if (phase >= 1) phase -= 1;
            return std::sin (pw::twoPi * (float) phase) * amp.next();
        }
    };

    void trigger (int note, float vel)
    {
        switch (note)
        {
            case 35: case 36:
                kick.trigger (vel, kickPitch->get() * 3.5f, kickPitch->get(), 0.03f, kickDecay->get(), sr);
                kickClick.trigger (0.25f * vel, 0.006f, sr);
                break;
            case 38: case 40:
                snareNoise.trigger (0.7f * vel, snareDecay->get(), sr);
                snareBody.trigger (0.6f * vel, 220, 160, 0.03f, 0.1f, sr);
                break;
            case 39:
                clapTime = 0; clapVel = vel; clapOn = true;
                break;
            case 42: case 44:
                ohh.choke (sr); // a closed hat cuts off the open hat
                chh.trigger (0.45f * vel, hatDecay->get(), sr);
                break;
            case 46:
                ohh.trigger (0.45f * vel, hatDecay->get() * 7.5f, sr);
                break;
            case 41: case 43: case 45: case 47:
                tomLo.trigger (0.8f * vel, 176 * tomTune->get(), 110 * tomTune->get(), 0.025f, 0.35f, sr);
                break;
            case 48: case 50:
                tomHi.trigger (0.8f * vel, 304 * tomTune->get(), 190 * tomTune->get(), 0.025f, 0.35f, sr);
                break;
            case 37:
                rim.trigger (0.5f * vel, 0.03f, sr);
                break;
            default: break;
        }
    }

    float clapEnv()
    {
        if (! clapOn) return 0;
        const float t = (float) (clapTime++ / sr);
        if (t < 0.033f)
            return clapVel * 0.9f * std::exp (-std::fmod (t, 0.011f) / 0.0025f);
        const float e = clapVel * 0.7f * std::exp (-(t - 0.033f) / (clapDecay->get() / 9.2f));
        if (e < 1.0e-5f) clapOn = false;
        return e;
    }

    void render (juce::AudioBuffer<float>& buf, int from, int to)
    {
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        const float vol = volume->get();

        for (int i = from; i < to; ++i)
        {
            float l = 0, r = 0;
            auto add = [&] (float s, float pan) {
                l += s * (pan > 0 ? 1 - pan : 1);
                r += s * (pan < 0 ? 1 + pan : 1);
            };

            add (kick.next (sr) + noise.next() * kickClick.next(), 0);
            add (snareHp.process (noise.next()).hp * snareNoise.next() + snareBody.next (sr), 0);
            if (clapOn) add (clapFilter.process (noise.next()).bp * clapEnv() * 2.5f, 0.05f);
            if (chh.v > 0) add (chhBp.process (chhHp.process (noise.next()).hp).bp * chh.next() * 2.0f, 0.2f);
            if (ohh.v > 0) add (ohhBp.process (ohhHp.process (noise.next()).hp).bp * ohh.next() * 2.0f, 0.2f);
            add (tomLo.next (sr), -0.25f);
            add (tomHi.next (sr), 0.25f);
            if (rim.v > 0)
            {
                rimPhase += 1700.0 / sr; if (rimPhase >= 1) rimPhase -= 1;
                add (rimFilter.process (rimPhase < 0.5 ? 1.0f : -1.0f).bp * rim.next(), -0.15f);
            }

            L[i] = l * vol;
            R[i] = r * vol;
        }
    }

    double sr = 44100;
    pw::Noise noise;
    Tonal kick, snareBody, tomLo, tomHi;
    pw::DecayEnv kickClick, snareNoise, chh, ohh, rim;
    pw::Svf snareHp, clapFilter, chhHp, chhBp, ohhHp, ohhBp, rimFilter;
    double rimPhase = 0;
    int64_t clapTime = 0;
    float clapVel = 0;
    bool clapOn = false;

    juce::AudioParameterFloat *kickPitch, *kickDecay, *snareDecay, *clapDecay, *hatDecay, *hatTone, *tomTune, *volume;
};
