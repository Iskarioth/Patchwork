#pragma once

#include "../engine/SequencerModule.h"

// Polyphonic note sequencer. Edited in its own window (PianoRollEditor).
class PianoRollModule : public SequencerModule
{
public:
    juce::String getTypeId() const override { return "pianoroll"; }
    juce::String getDisplayName() const override { return "Piano Roll"; }
    bool hasModuleEditor() const override { return true; }

    std::vector<SeqNote> getNotes()
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        return notes;
    }

    void setNotes (std::vector<SeqNote> newNotes)
    {
        {
            const juce::SpinLock::ScopedLockType sl (dataLock);
            notes.swap (newNotes);
        }
        // the old vector is freed here, outside the lock, on the message thread
    }

    int getBars() const { return bars.load(); }
    void setBars (int b) { bars = juce::jlimit (1, 16, b); }

    void saveExtra (juce::ValueTree& t) const override
    {
        t.setProperty ("bars", bars.load(), nullptr);
        for (const auto& n : const_cast<PianoRollModule*> (this)->getNotes())
        {
            juce::ValueTree nt ("NOTE");
            nt.setProperty ("p", n.pitch, nullptr);
            nt.setProperty ("s", n.start, nullptr);
            nt.setProperty ("l", n.length, nullptr);
            nt.setProperty ("v", n.velocity, nullptr);
            t.appendChild (nt, nullptr);
        }
    }

    void loadExtra (const juce::ValueTree& t) override
    {
        setBars ((int) t.getProperty ("bars", 2));
        std::vector<SeqNote> loaded;
        for (const auto& nt : t)
            if (nt.hasType ("NOTE"))
                loaded.push_back ({ (int) nt["p"], (double) nt["s"], (double) nt["l"], (float) nt["v"] });
        setNotes (std::move (loaded));
    }

protected:
    int gatherNotes (std::vector<SeqNote>& out) override
    {
        out.insert (out.end(), notes.begin(), notes.end());
        return bars.load() * 16;
    }

private:
    std::vector<SeqNote> notes;
    std::atomic<int> bars { 2 };
};
