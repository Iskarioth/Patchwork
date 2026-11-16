#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Patch.h"
#include "Recorder.h"

// Connects the patch's graph to the sound card, drives the transport clock and
// feeds the master output to the recorder.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine (Patch&, Transport&);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    Recorder& getRecorder() { return recorder; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    Patch& patch;
    Transport& transport;
    Recorder recorder;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
};
