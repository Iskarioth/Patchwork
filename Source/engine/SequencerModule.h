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
// note-offs, stopping cleanly and auditioning notes from the editor.
class SequencerModule : public ModuleProcessor
{
public:
    SequencerModule() : ModuleProcessor ({}, { { "Notes", PortType::Note, 0 } })
    {
        scratch.reserve (4096);
        events.reserve (512);
        offAt.fill (-1);
    }

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
        if (playing)
        {
            const double sps = Transport::samplesPerStep (sampleRate, transport->bpm.load());
            const double pos = (double) transport->position.load();
            const double endPos = pos + n;
            int loopSteps = 16;
            {
                const juce::SpinLock::ScopedLockType sl (dataLock);
                scratch.clear();
                loopSteps = std::max (1, gatherNotes (scratch));
            }
            const double loopLen = loopSteps * sps;

            for (auto k = (int64_t) std::floor (pos / loopLen); (double) k * loopLen < endPos; ++k)
                for (const auto& note : scratch)
                {
                    if (note.start >= loopSteps)
                        continue;
                    const double on = (double) k * loopLen + note.start * sps;
                    if (on >= pos && on < endPos)
                        events.push_back ({ (int) (on - pos), note.pitch, note.velocity,
                                            (int64_t) std::max (1.0, note.length * sps) });
                }

            playheadStep = std::fmod (pos, loopLen) / sps;
        }
        else
        {
            if (wasPlaying)
                killAll (midi);
            playheadStep = -1.0;
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
    // Called on the audio thread with dataLock held. Append notes, return loop length in steps.
    virtual int gatherNotes (std::vector<SeqNote>& out) = 0;

    juce::SpinLock dataLock; // guards the subclass's pattern data

private:
    struct Event { int offset; int pitch; float velocity; int64_t lengthSamples; };
    struct PreviewNote { int pitch; float velocity; double seconds; };

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
    std::atomic<double> playheadStep { -1.0 };
    std::vector<SeqNote> scratch;
    std::vector<Event> events;

    juce::AbstractFifo previewFifo { 64 };
    std::array<PreviewNote, 64> previewBuf {};
};
