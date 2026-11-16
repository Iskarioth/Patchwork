#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "engine/AudioEngine.h"
#include "ui/PatchCanvas.h"
#include "ui/Theme.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void togglePlay();
    void loadPatch (const juce::File&);
    void savePatch (const juce::File&);
    void syncTransportControls();
    static juce::File autosaveFile();

    PatchLookAndFeel lookAndFeel;   // first, so it outlives every component
    Transport transport;
    Patch patch { transport };
    AudioEngine engine { patch, transport };

    juce::TextButton playButton { "Play" }, newButton { "New" }, openButton { "Open..." },
                     saveButton { "Save..." }, audioButton { "Audio..." };
    juce::Slider bpmSlider;
    juce::Label bpmLabel { {}, "BPM" }, positionLabel;
    juce::Viewport viewport;
    PatchCanvas canvas { patch };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };
};
