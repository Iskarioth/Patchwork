#pragma once

#include "../engine/SequencerModule.h"
#include "../engine/DrumMap.h"

// 8 lanes x 16 steps with 3 velocity levels, in 8 pattern slots. Sends General MIDI drum notes.
class DrumSequencerModule : public SequencerModule
{
public:
    static constexpr int numLanes = (int) drumLanes.size();
    static constexpr int numSteps = 16;

    juce::String getTypeId() const override { return "drumseq"; }
    juce::String getDisplayName() const override { return "Drum Sequencer"; }
    bool hasModuleEditor() const override { return true; }

    int getStep (int pattern, int lane, int step) const { return steps[(size_t) pattern][(size_t) lane][(size_t) step].load(); }
    void setStep (int pattern, int lane, int step, int velocityLevel)
    {
        steps[(size_t) pattern][(size_t) lane][(size_t) step] = juce::jlimit (0, 3, velocityLevel);
    }

    void clearPattern (int pattern)
    {
        for (auto& lane : steps[(size_t) pattern])
            for (auto& s : lane)
                s = 0;
    }

    bool isEmpty (int pattern) const
    {
        for (auto& lane : steps[(size_t) pattern])
            for (auto& s : lane)
                if (s.load() != 0)
                    return false;
        return true;
    }

    // One PATTERN child per pattern in use, with a "laneN" row of step levels each.
    void saveExtra (juce::ValueTree& t) const override
    {
        for (int p = 0; p < numPatterns; ++p)
        {
            if (isEmpty (p))
                continue;
            juce::ValueTree pt ("PATTERN");
            pt.setProperty ("index", p, nullptr);
            for (int l = 0; l < numLanes; ++l)
            {
                juce::String row;
                for (int s = 0; s < numSteps; ++s)
                    row << getStep (p, l, s);
                pt.setProperty ("lane" + juce::String (l), row, nullptr);
            }
            t.appendChild (pt, nullptr);
        }
    }

    void loadExtra (const juce::ValueTree& t) override
    {
        auto loadPattern = [this] (int p, const juce::ValueTree& src) {
            for (int l = 0; l < numLanes; ++l)
            {
                const auto row = src["lane" + juce::String (l)].toString();
                for (int s = 0; s < numSteps; ++s)
                    setStep (p, l, s, s < row.length() ? row[s] - '0' : 0);
            }
        };

        for (int p = 0; p < numPatterns; ++p)
            clearPattern (p);

        bool anyPattern = false;
        for (const auto& pt : t)
            if (const int p = (int) pt.getProperty ("index", -1); pt.hasType ("PATTERN") && p >= 0 && p < numPatterns)
            {
                loadPattern (p, pt);
                anyPattern = true;
            }

        if (! anyPattern) // patches from before pattern slots keep the lanes at the top
            loadPattern (0, t);
    }

protected:
    int gatherNotes (int pattern, std::vector<SeqNote>& out) override
    {
        for (int l = 0; l < numLanes; ++l)
            for (int s = 0; s < numSteps; ++s)
                if (const int v = getStep (pattern, l, s); v > 0)
                    out.push_back ({ drumLanes[(size_t) l].pitch, (double) s, 0.5, drumVelocities[(size_t) v] });
        return numSteps;
    }

private:
    // atomics, so the editor can write single steps without taking the lock
    std::array<std::array<std::array<std::atomic<int>, numSteps>, numLanes>, numPatterns> steps {};
};
