#include "MainComponent.h"

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);

    playButton.setClickingTogglesState (false);
    playButton.onClick = [this] { togglePlay(); };
    playButton.setTooltip ("Play / stop (Space)");

    newButton.onClick = [this] {
        transport.stop();
        patch.clear();
        canvas.rebuild();
    };

    openButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Open patch", autosaveFile().getParentDirectory(), "*.patch");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult().existsAsFile())
                                      loadPatch (fc.getResult());
                              });
    };

    saveButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Save patch", autosaveFile().getParentDirectory(), "*.patch");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) {
                                  if (fc.getResult() != juce::File())
                                      savePatch (fc.getResult().withFileExtension ("patch"));
                              });
    };

    audioButton.onClick = [this] {
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager(), 0, 0, 2, 2,
                                                                               false, false, true, false);
        selector->setSize (520, 360);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector.release());
        o.dialogTitle = "Audio settings";
        o.dialogBackgroundColour = Theme::background;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    };

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 24);
    bpmSlider.setRange (40.0, 220.0, 1.0);
    bpmSlider.onValueChange = [this] { transport.bpm = bpmSlider.getValue(); };
    bpmLabel.setColour (juce::Label::textColourId, Theme::dim);
    positionLabel.setFont (juce::FontOptions (18.0f));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0c0d0f));
    positionLabel.setColour (juce::Label::outlineColourId, Theme::line);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &positionLabel, &bpmLabel, &bpmSlider,
                                                             &newButton, &openButton, &saveButton, &audioButton })
        addAndMakeVisible (c);

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    // Pick up where you left off, or start from the example patch.
    if (autosaveFile().existsAsFile())
        loadPatch (autosaveFile());
    else
    {
        patch.buildDefault();
        canvas.rebuild();
    }
    syncTransportControls();

    setWantsKeyboardFocus (true);
    startTimerHz (20);
    setSize (1500, 900);
}

MainComponent::~MainComponent()
{
    transport.stop();
    savePatch (autosaveFile());
    setLookAndFeel (nullptr);
}

juce::File MainComponent::autosaveFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Patchwork").getChildFile ("autosave.patch");
}

void MainComponent::loadPatch (const juce::File& f)
{
    if (auto xml = juce::parseXML (f))
    {
        transport.stop();
        patch.fromValueTree (juce::ValueTree::fromXml (*xml));
        canvas.rebuild();
        syncTransportControls();
    }
}

void MainComponent::savePatch (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    if (auto xml = patch.toValueTree().createXml())
        xml->writeTo (f);
}

void MainComponent::syncTransportControls()
{
    bpmSlider.setValue (transport.bpm.load(), juce::dontSendNotification);
}

void MainComponent::togglePlay()
{
    if (transport.playing) transport.stop();
    else transport.start();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }
    return false;
}

void MainComponent::timerCallback()
{
    const bool playing = transport.playing.load();
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);

    juce::String pos ("1.1.1");
    if (playing)
    {
        const double step = (double) transport.position.load() / Transport::samplesPerStep (transport.sampleRate, transport.bpm);
        const auto s = (juce::int64) step;
        pos = juce::String (s / 16 + 1) + "." + juce::String ((s % 16) / 4 + 1) + "." + juce::String (s % 4 + 1);
    }
    positionLabel.setText (pos, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff191b1f));
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Right-click the canvas to add modules. Drag from a port to connect. Right-click a cable or module to remove it.",
                getLocalBounds().removeFromTop (44).withTrimmedLeft (560).withTrimmedRight (330), juce::Justification::centredLeft);
    g.setColour (Theme::line);
    g.fillRect (0, 44, getWidth(), 1);
}

void MainComponent::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (44).reduced (8, 8);
    playButton.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (8);
    positionLabel.setBounds (bar.removeFromLeft (80));
    bar.removeFromLeft (14);
    bpmLabel.setBounds (bar.removeFromLeft (36));
    bpmSlider.setBounds (bar.removeFromLeft (120));

    audioButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (6);
    saveButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (6);
    openButton.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (6);
    newButton.setBounds (bar.removeFromRight (60));

    b.removeFromTop (1);
    viewport.setBounds (b);
}
