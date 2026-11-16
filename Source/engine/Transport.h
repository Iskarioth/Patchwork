#pragma once

#include <atomic>
#include <cstdint>

// Shared clock. The message thread starts/stops it and sets the tempo; the audio
// thread calls beginBlock/endBlock around every graph block, so every module sees
// the same position for the whole block.
struct Transport
{
    std::atomic<bool> playing { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> sampleRate { 44100.0 };

    // Valid for the block currently being processed.
    std::atomic<bool> blockPlaying { false };
    std::atomic<int64_t> position { 0 };   // samples since play was pressed

    void start() { resetRequested = true; playing = true; }
    void stop()  { playing = false; }

    void beginBlock()
    {
        const bool p = playing.load();
        if (resetRequested.exchange (false))
            position = 0;
        blockPlaying = p;
    }

    void endBlock (int numSamples)
    {
        if (blockPlaying.load())
            position += numSamples;
    }

    static double samplesPerStep (double sr, double tempo) { return sr * 60.0 / tempo / 4.0; } // one 16th note

private:
    std::atomic<bool> resetRequested { false };
};
