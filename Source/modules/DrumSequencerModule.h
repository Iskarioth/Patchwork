#pragma once

#include "../engine/SequencerModule.h"
#include "../engine/DrumMap.h"

// 8 lanes x 16 steps with 3 velocity levels. Sends General MIDI drum notes.
class DrumSequencerModule : public SequencerModule
{
public:
    static constexpr int numLanes = (int) drumLanes.size();
    static constexpr int numSteps = 16;

    juce::String getTypeId() const override { return "drumseq"; }
    juce::String getDisplayName() const override { return "Drum Sequencer"; }
    bool hasModuleEditor() const override { return true; }

    int getStep (int lane, int step) const { return steps[(size_t) lane][(size_t) step].load(); }
    void setStep (int lane, int step, int velocityLevel) { steps[(size_t) lane][(size_t) step] = juce::jlimit (0, 3, velocityLevel); }

    void clearAll()
    {
        for (auto& lane : steps)
            for (auto& s : lane)
                s = 0;
    }

    void saveExtra (juce::ValueTree& t) const override
    {
        for (int l = 0; l < numLanes; ++l)
        {
            juce::String row;
            for (int s = 0; s < numSteps; ++s)
                row << getStep (l, s);
            t.setProperty ("lane" + juce::String (l), row, nullptr);
        }
    }

    void loadExtra (const juce::ValueTree& t) override
    {
        for (int l = 0; l < numLanes; ++l)
        {
            const auto row = t["lane" + juce::String (l)].toString();
            for (int s = 0; s < numSteps; ++s)
                setStep (l, s, s < row.length() ? row[s] - '0' : 0);
        }
    }

protected:
    int gatherNotes (std::vector<SeqNote>& out) override
    {
        for (int l = 0; l < numLanes; ++l)
            for (int s = 0; s < numSteps; ++s)
                if (const int v = getStep (l, s); v > 0)
                    out.push_back ({ drumLanes[(size_t) l].pitch, (double) s, 0.5, drumVelocities[(size_t) v] });
        return numSteps;
    }

private:
    // atomics, so the editor can write single steps without taking the lock
    std::array<std::array<std::atomic<int>, numSteps>, numLanes> steps {};
};
