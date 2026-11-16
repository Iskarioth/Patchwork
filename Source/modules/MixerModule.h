#pragma once

#include "../engine/Module.h"
#include <array>

// Four stereo inputs, each with level and pan, summed to one stereo output.
class MixerModule : public ModuleProcessor
{
public:
    static constexpr int numInputs = 4;

    MixerModule()
        : ModuleProcessor ({ { "In 1", PortType::Audio, 0 }, { "In 2", PortType::Audio, 2 },
                             { "In 3", PortType::Audio, 4 }, { "In 4", PortType::Audio, 6 } },
                           { { "Out", PortType::Audio, 0 } })
    {
        // levels first, then pans, so each knob column on the node is one channel
        for (int k = 0; k < numInputs; ++k)
            level[(size_t) k] = addFloat ("level" + juce::String (k + 1), "Level " + juce::String (k + 1), { -60.0f, 6.0f }, 0.0f, "dB");
        for (int k = 0; k < numInputs; ++k)
            pan[(size_t) k] = addFloat ("pan" + juce::String (k + 1), "Pan " + juce::String (k + 1), { -1.0f, 1.0f }, 0.0f);
    }

    juce::String getTypeId() const override { return "mixer"; }
    juce::String getDisplayName() const override { return "Mixer"; }
    float getMeterLevel() const override { return meter.load(); }

    void prepareToPlay (double, int) override
    {
        for (int k = 0; k < numInputs; ++k)
            gains[(size_t) k] = targetGains (k);
    }

    void processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&) override
    {
        const int n = buf.getNumSamples();
        std::array<const float*, numInputs * 2> in;
        for (size_t c = 0; c < in.size(); ++c)
            in[c] = buf.getReadPointer ((int) c);

        // ramp each gain across the block so moving a knob doesn't zipper
        std::array<Gains, numInputs> from = gains, step;
        for (int k = 0; k < numInputs; ++k)
        {
            const auto to = targetGains (k);
            step[(size_t) k] = { (to.l - from[(size_t) k].l) / (float) n, (to.r - from[(size_t) k].r) / (float) n };
            gains[(size_t) k] = to;
        }

        // inputs share channels with the output, so read every input before writing
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = 0, r = 0;
            for (size_t k = 0; k < numInputs; ++k)
            {
                l += in[k * 2][i] * (from[k].l + step[k].l * (float) i);
                r += in[k * 2 + 1][i] * (from[k].r + step[k].r * (float) i);
            }
            L[i] = l;
            R[i] = r;
            peak = std::max ({ peak, std::abs (l), std::abs (r) });
        }
        meter = std::max (peak, meter.load() * 0.9f);
    }

private:
    struct Gains { float l = 1, r = 1; };

    // Equal-power balance: centre leaves the input untouched, hard left/right silences the other side.
    Gains targetGains (int k) const
    {
        const float g = juce::Decibels::decibelsToGain (level[(size_t) k]->get(), -60.0f);
        const float angle = (pan[(size_t) k]->get() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        return { g * std::min (1.0f, juce::MathConstants<float>::sqrt2 * std::cos (angle)),
                 g * std::min (1.0f, juce::MathConstants<float>::sqrt2 * std::sin (angle)) };
    }

    std::array<Gains, numInputs> gains;
    std::atomic<float> meter { 0 };
    std::array<juce::AudioParameterFloat*, numInputs> level {}, pan {};
};
