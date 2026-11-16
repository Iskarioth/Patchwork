# Roadmap

## Current state

v0.1: a modular groovebox on a patch canvas. Every sound is synthesized.

- Sequencers: Piano Roll (polyphonic, 1-8 bars) and Drum Sequencer (8 lanes x 16 steps)
- Instruments: 8-voice subtractive Synth and a synthesized Drum Machine
- Effects: Filter, Distortion, Delay, Reverb. Modulators: LFO, Envelope. Utility: Mixer, Output
- Typed cables (audio, notes, modulation) that refuse feedback loops; patches save as XML and autosave on quit
- Offline engine tests in `Tests/EngineTests.cpp`

## Next up

1. **FM synth voice**: a 2-operator FM instrument.
2. **Pattern slots on the sequencers**: store several patterns and switch between them live.
3. **Record output to WAV**: capture the master output to a file.

## Ideas / maybe later

- Song arrangement
- Sampler
- MIDI keyboard input
- MIDI file export
- Undo/redo
- Canvas zoom
- Module presets

## Done

When an item is finished, move it here with the date (YYYY-MM-DD) and update "Current state".

- 2026-11-16 **Mixer module**: 4 stereo inputs with level and equal-power pan, click-free gain changes, output meter.
- 2026-11-16 **Envelope modulator**: ADSR on a Mod output, gated by a Note input. Held while any note is down, retriggers click-free, depth -1..1.
