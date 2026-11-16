#pragma once

#include "Module.h"
#include <array>

// One note in a sequencer, measured in 16th-note steps.
struct SeqNote
{
    int pitch = 60;
    double start = 0;
    double length = 1;
    float velocity = 0.8f;
};

// Base for anything that turns pattern data into MIDI on a Note output.
// Subclasses only describe their notes; this class handles timing, looping,
// note-offs, stopping cleanly, switching patterns and auditioning notes from the editor.
class SequencerModule : public ModuleProcessor
{
public:
    static constexpr int numPatterns = 8;

    SequencerModule() : ModuleProcessor ({}, { { "Notes", PortType::Note, 0 } })
    {
        patternChoice = addChoice ("pattern", "Pattern", { "A", "B", "C", "D", "E", "F", "G", "H" }, 0);
        scratch.reserve (4096);
        events.reserve (512);
        offAt.fill (-1);
    }

    // The pattern chosen on the node. The editors edit this one.
    int getSelectedPattern() const { return patternChoice->getIndex(); }
    juce::AudioParameterChoice& getPatternParameter() { return *patternChoice; }

    // The pattern being played, or -1 when stopped. While playing, a newly selected
    // pattern only takes over at the end of the current loop.
    int getPlayingPattern() const { return playingPattern.load(); }

    // Message thread: play a note now, even when the transport is stopped.
    void preview (int pitch, float velocity, double seconds)
    {
        previewFifo.write (1).forEach ([&] (int i) { previewBuf[(size_t) i] = { pitch, velocity, seconds }; });
    }

    // Current position inside the loop in steps, or -1 when stopped.
    double getPlayheadStep() const { return playheadStep.load(); }

    void prepareToPlay (double sr, int) override { sampleRate = sr; }

    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        audio.clear();
        midi.clear();
        const int n = audio.getNumSamples();
        const int64_t clockStart = localClock;
        events.clear();

        previewFifo.read (previewFifo.getNumReady()).forEach ([&] (int i) {
            const auto& p = previewBuf[(size_t) i];
            events.push_back ({ 0, p.pitch, p.velocity, (int64_t) std::max (1.0, p.seconds * sampleRate) });
        });

        const bool playing = transport != nullptr && transport->blockPlaying.load();
        const int wanted = patternChoice->getIndex();
        if (playing)
        {
            const double sps = Transport::samplesPerStep (sampleRate, transport->bpm.load());
            const double pos = (double) transport->position.load();
            const double endPos = pos + n;
            if (! wasPlaying || pos < anchor) // started, or restarted from the top
            {
                current = wanted;
                anchor = pos;
            }
            double loopLen = gather (current, sps);

            // loops of the current pattern are counted from where it started (anchor)
            for (double loopStart = anchor + std::floor ((pos - anchor) / loopLen) * loopLen; loopStart < endPos; loopStart += loopLen)
            {
                if (wanted != current && loopStart >= pos) // a loop ends in this block: switch here
                {
                    current = wanted;
                    anchor = loopStart;
                    loopLen = gather (current, sps);
                }
                for (const auto& note : scratch)
                {
                    if (note.start >= loopSteps)
                        continue;
                    const double on = loopStart + note.start * sps;
                    if (on >= pos && on < endPos)
                        events.push_back ({ (int) (on - pos), note.pitch, note.velocity,
                                            (int64_t) std::max (1.0, note.length * sps) });
                }
            }

            playheadStep = std::fmod (std::max (0.0, pos - anchor), loopLen) / sps;
            playingPattern = current;
        }
        else
        {
            if (wasPlaying)
                killAll (midi);
            playheadStep = -1.0;
            playingPattern = -1;
        }
        wasPlaying = playing;

        std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });

        for (const auto& e : events)
        {
            const int p = juce::jlimit (0, 127, e.pitch);
            flushOffs (midi, clockStart, clockStart + e.offset + 1);
            if (offAt[(size_t) p] >= 0) // retrigger: close the old note first
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, p), e.offset);
                offAt[(size_t) p] = -1;
            }
            midi.addEvent (juce::MidiMessage::noteOn (1, p, juce::jlimit (0.01f, 1.0f, e.velocity)), e.offset);
            offAt[(size_t) p] = clockStart + e.offset + e.lengthSamples;
        }
        flushOffs (midi, clockStart, clockStart + n);
        localClock = clockStart + n;
    }

protected:
    // Called on the audio thread with dataLock held. Append the notes of one pattern,
    // return its loop length in steps.
    virtual int gatherNotes (int pattern, std::vector<SeqNote>& out) = 0;

    juce::SpinLock dataLock; // guards the subclass's pattern data

private:
    struct Event { int offset; int pitch; float velocity; int64_t lengthSamples; };
    struct PreviewNote { int pitch; float velocity; double seconds; };

    // Fills scratch with the notes of pattern p; returns its loop length in samples.
    double gather (int p, double samplesPerStep)
    {
        const juce::SpinLock::ScopedLockType sl (dataLock);
        scratch.clear();
        loopSteps = std::max (1, gatherNotes (p, scratch));
        return loopSteps * samplesPerStep;
    }

    void flushOffs (juce::MidiBuffer& midi, int64_t blockStart, int64_t limit)
    {
        for (size_t p = 0; p < offAt.size(); ++p)
            if (offAt[p] >= 0 && offAt[p] < limit)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, (int) p), (int) std::max<int64_t> (0, offAt[p] - blockStart));
                offAt[p] = -1;
            }
    }

    void killAll (juce::MidiBuffer& midi)
    {
        for (size_t p = 0; p < offAt.size(); ++p)
            if (offAt[p] >= 0)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, (int) p), 0);
                offAt[p] = -1;
            }
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    double sampleRate = 44100.0;
    int64_t localClock = 0;          // counts every processed sample, playing or not
    std::array<int64_t, 128> offAt;  // pending note-off per pitch, in localClock samples
    bool wasPlaying = false;
    int current = 0, loopSteps = 16;  // pattern being played and its length
    double anchor = 0;               // transport position where it started
    std::atomic<double> playheadStep { -1.0 };
    std::atomic<int> playingPattern { -1 };
    juce::AudioParameterChoice* patternChoice;
    std::vector<SeqNote> scratch;
    std::vector<Event> events;

    juce::AbstractFifo previewFifo { 64 };
    std::array<PreviewNote, 64> previewBuf {};
};
