#pragma once

#include <array>

// Lanes of the drum sequencer and the General MIDI notes they send.
// The drum machine listens on the same notes, so any GM drum source works too.
struct DrumLane { const char* name; int pitch; };

inline constexpr std::array<DrumLane, 8> drumLanes { {
    { "Kick",       36 },
    { "Snare",      38 },
    { "Clap",       39 },
    { "Closed hat", 42 },
    { "Open hat",   46 },
    { "Low tom",    45 },
    { "High tom",   50 },
    { "Rim",        37 },
} };

inline constexpr std::array<float, 4> drumVelocities { 0.0f, 0.45f, 0.72f, 1.0f };
