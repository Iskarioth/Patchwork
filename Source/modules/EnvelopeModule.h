#pragma once

#include "../engine/Module.h"
#include <bitset>

// ADSR on a Mod output, gated by a Note input. The gate stays open while any note
// is held, and every new note restarts the attack from the current level (no click).
class EnvelopeModule : public ModuleProcessor
{
public:
    EnvelopeModule()
        : ModuleProcessor ({ { "Notes", PortType::Note, 0 } }, { { "Out", PortType::Mod, 0 } })
    {
        attack  = addFloat ("attack", "Attack", logRange (0.001f, 3.0f), 0.01f, "s");
        decay   = addFloat ("decay", "Decay", logRange (0.01f, 3.0f), 0.3f, "s");
        sustain = addFloat ("sustain", "Sustain", { 0.0f, 1.0f }, 0.5f);
        release = addFloat ("release", "Release", logRange (0.01f, 5.0f), 0.4f, "s");
        depth   = addFloat ("depth", "Depth", { -1.0f, 1.0f }, 1.0f);
    }

    juce::String getTypeId() const override { return "envelope"; }
    juce::String getDisplayName() const override { return "Envelope"; }
    float getMeterLevel() const override { return lastLevel.load(); }

    void prepareToPlay (double sr, int) override
    {
        adsr.setSampleRate (sr);
        adsr.reset();
        held.reset();
    }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) override
    {
        adsr.setParameters ({ attack->get(), decay->get(), sustain->get(), release->get() });
        const float d = depth->get();
        const int n = buf.getNumSamples();
        auto* out = buf.getWritePointer (0);
        float level = 0;
        int i = 0;

        // render up to each MIDI event so the gate is sample-accurate
        auto renderTo = [&] (int end) {
            for (; i < end; ++i)
                out[i] = d * (level = adsr.getNextSample());
        };

        for (const auto meta : midi)
        {
            renderTo (juce::jlimit (i, n, meta.samplePosition));
            handle (meta.getMessage());
        }
        renderTo (n);
        midi.clear();
        lastLevel = level;
    }

private:
    void handle (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            held.set ((size_t) m.getNoteNumber());
            adsr.noteOn();
        }
        else if (m.isNoteOff())
        {
            const auto p = (size_t) m.getNoteNumber();
            if (held.test (p))
            {
                held.reset (p);
                if (held.none())
                    adsr.noteOff();
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.reset();
            adsr.noteOff();
        }
    }

    juce::ADSR adsr;
    std::bitset<128> held;   // pitches currently down
    std::atomic<float> lastLevel { 0 };
    juce::AudioParameterFloat *attack, *decay, *sustain, *release, *depth;
};
