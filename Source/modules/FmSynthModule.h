#pragma once

#include "../engine/Module.h"
#include "../engine/Dsp.h"

namespace fm_detail
{
struct Params
{
    float ratio = 2, index = 2, envIndex = 4, envDecay = 0.4f, feedback = 0, volume = 0.6f;
    juce::ADSR::Parameters adsr;
    const float* indexMod = nullptr;  // per-sample mod input, -1..1
    const float* pitchMod = nullptr;
};

struct Sound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// 2-operator FM: a sine modulator (with optional self-feedback) phase-modulates a
// sine carrier. The index has its own decaying envelope so notes start bright.
class Voice : public juce::SynthesiserVoice
{
public:
    explicit Voice (const Params& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<Sound*> (s) != nullptr; }

    void startNote (int note, float vel, juce::SynthesiserSound*, int) override
    {
        freq = juce::MidiMessage::getMidiNoteInHertz (note);
        velocity = vel;
        carrierPhase = modPhase = 0.0;
        prevMod1 = prevMod2 = 0.0f;
        indexEnv = 1.0f;
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
        const float envCoef = (float) std::exp (-4.6 / (std::max (0.005, (double) params.envDecay) * sr));

        for (int i = start; i < start + num; ++i)
        {
            double f = freq;
            if (const float pm = params.pitchMod[i]; std::abs (pm) > 1.0e-6f)
                f *= std::exp2 (pm * 2.0 / 12.0); // +-2 semitones at full mod

            indexEnv *= envCoef;
            const float index = std::max (0.0f, params.index + params.envIndex * indexEnv * velocity + params.indexMod[i] * 5.0f);

            // averaging the last two outputs keeps strong feedback from buzzing
            const float fb = params.feedback * 0.75f * (prevMod1 + prevMod2); // up to 1.5 rad; more turns to noise
            const float m = std::sin (pw::twoPi * (float) modPhase + fb);
            prevMod2 = prevMod1;
            prevMod1 = m;
            const float s = std::sin (pw::twoPi * (float) carrierPhase + index * m);

            carrierPhase += f / sr;               if (carrierPhase >= 1.0) carrierPhase -= 1.0;
            modPhase += f * params.ratio / sr;    if (modPhase >= 1.0) modPhase -= std::floor (modPhase);

            const float y = s * adsr.getNextSample() * velocity * params.volume * 0.25f;
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
    double freq = 440, carrierPhase = 0, modPhase = 0;
    float velocity = 1, indexEnv = 0, prevMod1 = 0, prevMod2 = 0;
};
} // namespace fm_detail

// 8-voice 2-operator FM synth.
class FmSynthModule : public ModuleProcessor
{
public:
    FmSynthModule()
        : ModuleProcessor ({ { "Notes", PortType::Note, 0 },
                             { "Index mod", PortType::Mod, 0 },
                             { "Pitch mod", PortType::Mod, 1 } },
                           { { "Out", PortType::Audio, 0 } })
    {
        ratio    = addChoice ("ratio", "Ratio", { "0.5", "1", "2", "3", "4", "5", "6", "7", "8" }, 2);
        index    = addFloat ("index", "Index", { 0.0f, 10.0f }, 1.5f);
        envIndex = addFloat ("envindex", "Index env", { 0.0f, 10.0f }, 4.0f);
        envDecay = addFloat ("envdecay", "Env decay", logRange (0.01f, 3.0f), 0.4f, "s");
        feedback = addFloat ("feedback", "Feedback", { 0.0f, 1.0f }, 0.0f);
        attack   = addFloat ("attack", "Attack", logRange (0.001f, 3.0f), 0.005f, "s");
        decay    = addFloat ("decay", "Decay", logRange (0.01f, 3.0f), 0.6f, "s");
        sustain  = addFloat ("sustain", "Sustain", { 0.0f, 1.0f }, 0.6f);
        release  = addFloat ("release", "Release", logRange (0.01f, 5.0f), 0.5f, "s");
        volume   = addFloat ("volume", "Volume", { 0.0f, 1.0f }, 0.6f);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new fm_detail::Voice (params));
        synth.addSound (new fm_detail::Sound());
    }

    juce::String getTypeId() const override { return "fmsynth"; }
    juce::String getDisplayName() const override { return "FM Synth"; }

    void prepareToPlay (double sr, int maxBlock) override
    {
        synth.setCurrentPlaybackSampleRate (sr);
        modBuffer.setSize (2, maxBlock);
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        static constexpr float ratios[] = { 0.5f, 1, 2, 3, 4, 5, 6, 7, 8 };
        const int n = buffer.getNumSamples();

        // inputs and outputs share channels, so grab the mod signals before clearing
        modBuffer.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            if (c < buffer.getNumChannels()) modBuffer.copyFrom (c, 0, buffer, c, 0, n);
            else modBuffer.clear (c, 0, n);

        params.ratio = ratios[ratio->getIndex()];
        params.index = index->get();
        params.envIndex = envIndex->get();
        params.envDecay = envDecay->get();
        params.feedback = feedback->get();
        params.volume = volume->get();
        params.adsr = { attack->get(), decay->get(), sustain->get(), release->get() };
        params.indexMod = modBuffer.getReadPointer (0);
        params.pitchMod = modBuffer.getReadPointer (1);

        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, n);
        midi.clear();
    }

private:
    fm_detail::Params params;
    juce::Synthesiser synth;
    juce::AudioBuffer<float> modBuffer;
    juce::AudioParameterChoice* ratio;
    juce::AudioParameterFloat *index, *envIndex, *envDecay, *feedback, *attack, *decay, *sustain, *release, *volume;
};
