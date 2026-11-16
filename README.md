# Patchwork

A modular, patch-based groovebox for the desktop, written in C++ with JUCE 9.

You start with an empty canvas. Right-click to add modules (a piano roll, a synth, a filter, an LFO), drag cables between their ports, and press Space. Every sound is synthesized. There are no samples.

## Building on Windows

You need:

- Visual Studio 2022 or newer with the "Desktop development with C++" workload
- CMake 3.22 or newer (the copy that comes with Visual Studio works)
- Git (CMake uses it to download JUCE the first time)

From a Visual Studio Developer PowerShell in this folder:

```powershell
cmake -B build -A x64
cmake --build build --config Release
.\build\Patchwork_artefacts\Release\Patchwork.exe
```

CMake picks the newest installed Visual Studio, so there is no generator to name. Tested with VS 2026 (MSVC 19.51).

The first configure downloads JUCE 9.0.5 and takes a few minutes. After that, builds are incremental.

To work in the IDE instead, open `build\Patchwork.sln` and set `Patchwork` as the startup project.

If you already have a JUCE checkout, point to it instead of downloading:
`cmake -B build -DPATCHWORK_JUCE_PATH=C:\path\to\JUCE`

### Engine tests

```powershell
cmake --build build --config Release --target PatchworkTests
.\build\PatchworkTests_artefacts\Release\PatchworkTests.exe
```

These render audio through the graph with no sound card and check the patch makes sound, stops cleanly, refuses bad cables, and survives save/load. Run them after changing anything in `Source/engine` or `Source/modules`.

## Using it

| Action | How |
|---|---|
| Add a module | Right-click empty canvas |
| Connect | Drag from a port to a matching port. Compatible ports light up while dragging |
| Re-patch | Drag from an input that already has a cable to pick it up |
| Remove a cable | Right-click it |
| Remove a module | Right-click its title, or select it and press Delete |
| Move a module | Drag its title bar |
| Pan | Drag empty canvas, or scroll |
| Play / stop | Space |
| Edit notes or drum steps | "Open editor" on the Piano Roll / Drum Sequencer |
| Reset a knob | Double-click it |
| Sound card / buffer size | Audio... (pick WASAPI for lower latency on Windows) |

The patch autosaves on quit and reloads on start. Save... and Open... handle `.patch` files, which are plain XML.

### Cable types

- **Orange, audio:** stereo sound.
- **Teal, notes:** MIDI from a sequencer to an instrument.
- **Purple, modulation:** a control signal (-1..1) from an LFO into a knob input like "Cutoff mod".

A port only accepts its own cable type, and cables that would create a feedback loop are refused.

## Modules (v0.1)

| Category | Module | Notes |
|---|---|---|
| Sequencers | Piano Roll | Polyphonic, 1-8 bars, velocity per note |
| | Drum Sequencer | 8 lanes x 16 steps, 3 velocity levels, sends GM drum notes |
| Instruments | Synth | 8 voices, 2 detuned oscillators, resonant filter with envelope, ADSR. Cutoff and pitch mod inputs |
| | Drum Machine | Kick, snare, clap, closed/open hat (with choke), two toms, rim. All synthesized |
| Effects | Filter | Lowpass/highpass/bandpass, drive, cutoff mod input |
| | Distortion | Drive, tone, mix |
| | Delay | Tempo-synced, ping-pong, damping |
| | Reverb | Size, damping, width, mix |
| Modulators | LFO | 5 shapes, free or tempo-synced, depth, offset |
| | Envelope | ADSR triggered by a note input, depth -1..1 (negative inverts) |
| Utility | Output | Master volume, soft clipper, meter. Several outputs are summed |

## How it's built

```
Source/
  engine/      no UI code; also compiled into the test runner
    Transport.h        shared clock (play state, tempo, sample position)
    Ports.h            port and cable types
    Module.h           base class every module derives from
    SequencerModule.h  timing, looping and note-offs for anything that sends notes
    Patch.*            the AudioProcessorGraph, cables -> graph connections, save/load
    ModuleRegistry.*   the list of module types shown in the right-click menu
    AudioEngine.*      sound card <-> graph, drives the clock
    Dsp.h              filters, envelopes, oscillators
  modules/     one header per module (or group of small ones)
  ui/          canvas, node boxes, cables, piano roll and drum grid editors
Tests/         offline engine tests
```

Every module is a `juce::AudioProcessor` inside a `juce::AudioProcessorGraph`. Ports map to graph channels: an audio port is two channels, a mod port is one, and a note port is the graph's MIDI channel. The node UI is generated from each module's parameters, so a new module usually needs no UI code.

### Adding a module

1. Write a class deriving from `ModuleProcessor` (or `SequencerModule` for note sources). Declare ports in the constructor, add parameters with `addFloat` / `addChoice`, and implement `processBlock`.
2. Add one line to the list in `Source/engine/ModuleRegistry.cpp`.
3. Rebuild. It shows up in the right-click menu with knobs for all its parameters.
