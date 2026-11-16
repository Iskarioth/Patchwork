#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// Writes the master output to a 24-bit stereo WAV file. start/stop run on the
// message thread; write runs on the audio thread and never blocks or allocates:
// the data goes into a FIFO that a background thread drains to disk.
class Recorder
{
public:
    Recorder() { diskThread.startThread(); }
    ~Recorder() { stop(); }

    // Starts a new recording, replacing f. Returns false if the file can't be written.
    bool start (const juce::File& f, double sampleRate)
    {
        stop();
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = f.createOutputStream();
        if (stream == nullptr)
            return false;

        auto fileWriter = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                              .withSampleRate (sampleRate)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
        if (fileWriter == nullptr)
            return false;

        // ~1.5 s of buffer at 44.1 kHz
        writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (fileWriter.release(), diskThread, 65536);
        file = f;
        recorded = 0;
        dropped = false;
        const juce::SpinLock::ScopedLockType sl (lock);
        active = writer.get();
        return true;
    }

    // Finishes the file. Safe to call when not recording.
    void stop()
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            active = nullptr;
        }
        writer.reset(); // flushes what is still buffered and closes the file
    }

    bool isRecording() const { return writer != nullptr; }
    const juce::File& getFile() const { return file; }
    juce::int64 getSamplesRecorded() const { return recorded.load(); }
    bool hasDroppedAudio() const { return dropped.load(); } // the disk couldn't keep up

    // Audio thread. Skips the block if start/stop is swapping writers right now.
    void write (const float* const* stereo, int numSamples)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked() || active == nullptr)
            return;
        if (active->write (stereo, numSamples))
            recorded += numSamples;
        else
            dropped = true;
    }

private:
    juce::TimeSliceThread diskThread { "Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::AudioFormatWriter::ThreadedWriter* active = nullptr; // what the audio thread sees, guarded by lock
    juce::SpinLock lock;
    juce::File file;
    std::atomic<juce::int64> recorded { 0 };
    std::atomic<bool> dropped { false };
};
