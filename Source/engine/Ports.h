#pragma once

#include <juce_core/juce_core.h>

// Three kinds of cable:
//   Audio - stereo signal (2 graph channels)
//   Note  - MIDI events (the graph's MIDI channel)
//   Mod   - control signal, -1..1 at audio rate (1 graph channel)
enum class PortType { Audio, Note, Mod };

struct PortInfo
{
    juce::String name;
    PortType type;
    int channel = 0; // first graph channel used by this port (ignored for Note)
};

inline int portWidth (PortType t)
{
    switch (t)
    {
        case PortType::Audio: return 2;
        case PortType::Mod:   return 1;
        case PortType::Note:  return 0;
    }
    return 0;
}
