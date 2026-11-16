#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Patch.h"

// Connects the patch's graph to the sound card and drives the transport clock.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine (Patch&, Transport&);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    Patch& patch;
    Transport& transport;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
};
