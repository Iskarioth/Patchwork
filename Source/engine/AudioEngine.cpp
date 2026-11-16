#include "AudioEngine.h"

AudioEngine::AudioEngine (Patch& p, Transport& t) : patch (p), transport (t)
{
    player.setProcessor (&patch.getGraph());
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    player.setProcessor (nullptr);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext& ctx)
{
    transport.beginBlock();
    player.audioDeviceIOCallbackWithContext (in, numIn, out, numOut, numSamples, ctx);
    if (numOut >= 2)
        recorder.write (out, numSamples);
    transport.endBlock (numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    transport.sampleRate = device->getCurrentSampleRate();
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}
