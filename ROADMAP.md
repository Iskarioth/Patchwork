# Roadmap

## Current state

v0.1: a modular groovebox on a patch canvas. Every sound is synthesized.

- Sequencers: Piano Roll (polyphonic, 1-8 bars) and Drum Sequencer (8 lanes x 16 steps)
- Instruments: 8-voice subtractive Synth and a synthesized Drum Machine
- Effects: Filter, Distortion, Delay, Reverb. Modulator: LFO. Utility: Output
- Typed cables (audio, notes, modulation) that refuse feedback loops; patches save as XML and autosave on quit
- Offline engine tests in `Tests/EngineTests.cpp`

## Next up

1. **Envelope modulator**: an ADSR on a Mod output, triggered by a Note input.
2. **Mixer module**: 4 stereo inputs, each with level and pan.
3. **FM synth voice**: a 2-operator FM instrument.
4. **Pattern slots on the sequencers**: store several patterns and switch between them live.
5. **Record output to WAV**: capture the master output to a file.

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
