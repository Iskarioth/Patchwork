#pragma once

#include "../engine/SequencerModule.h"

// Polyphonic note sequencer with 8 pattern slots, each with its own notes and length.
// Edited in its own window (PianoRollEditor).
class PianoRollModule : public SequencerModule
{
public:
    PianoRollModule()
    {
        for (auto& b : bars)
            b = 2;
    }

    juce::String getTypeId() const override { return "pianoroll"; }
    juce::String getDisplayName() const override { return "Piano Roll"; }
    bool hasModuleEditor() const override { return true; }

    std::vector<SeqNote> getNotes (int pattern)
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        return notes[(size_t) pattern];
    }

    void setNotes (int pattern, std::vector<SeqNote> newNotes)
    {
        {
            const juce::SpinLock::ScopedLockType sl (dataLock);
            notes[(size_t) pattern].swap (newNotes);
        }
        // the old vector is freed here, outside the lock, on the message thread
    }

    int getBars (int pattern) const { return bars[(size_t) pattern].load(); }
    void setBars (int pattern, int b) { bars[(size_t) pattern] = juce::jlimit (1, 16, b); }

    // One PATTERN child per pattern in use.
    void saveExtra (juce::ValueTree& t) const override
    {
        auto* self = const_cast<PianoRollModule*> (this);
        for (int p = 0; p < numPatterns; ++p)
        {
            const auto patternNotes = self->getNotes (p);
            if (patternNotes.empty() && getBars (p) == 2)
                continue;
            juce::ValueTree pt ("PATTERN");
            pt.setProperty ("index", p, nullptr);
            pt.setProperty ("bars", getBars (p), nullptr);
            for (const auto& n : patternNotes)
            {
                juce::ValueTree nt ("NOTE");
                nt.setProperty ("p", n.pitch, nullptr);
                nt.setProperty ("s", n.start, nullptr);
                nt.setProperty ("l", n.length, nullptr);
                nt.setProperty ("v", n.velocity, nullptr);
                pt.appendChild (nt, nullptr);
            }
            t.appendChild (pt, nullptr);
        }
    }

    void loadExtra (const juce::ValueTree& t) override
    {
        auto loadPattern = [this] (int p, const juce::ValueTree& src) {
            setBars (p, (int) src.getProperty ("bars", 2));
            std::vector<SeqNote> loaded;
            for (const auto& nt : src)
                if (nt.hasType ("NOTE"))
                    loaded.push_back ({ (int) nt["p"], (double) nt["s"], (double) nt["l"], (float) nt["v"] });
            setNotes (p, std::move (loaded));
        };

        for (int p = 0; p < numPatterns; ++p)
            loadPattern (p, {});

        bool anyPattern = false;
        for (const auto& pt : t)
            if (const int p = (int) pt.getProperty ("index", -1); pt.hasType ("PATTERN") && p >= 0 && p < numPatterns)
            {
                loadPattern (p, pt);
                anyPattern = true;
            }

        if (! anyPattern) // patches from before pattern slots keep bars and notes at the top
            loadPattern (0, t);
    }

protected:
    int gatherNotes (int pattern, std::vector<SeqNote>& out) override
    {
        const auto& n = notes[(size_t) pattern];
        out.insert (out.end(), n.begin(), n.end());
        return bars[(size_t) pattern].load() * 16;
    }

private:
    std::array<std::vector<SeqNote>, numPatterns> notes;
    std::array<std::atomic<int>, numPatterns> bars;
};
