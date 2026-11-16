#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/Patch.h"

class PatchCanvas;

// One module on the canvas: title bar, ports, and a knob for every parameter.
class NodeComponent : public juce::Component, private juce::Timer
{
public:
    static constexpr int width = 248, titleH = 26, portRowH = 20, choiceH = 26, knobW = 58, knobH = 62, knobCols = 4;

    NodeComponent (PatchCanvas&, Patch&, Patch::NodeID);
    ~NodeComponent() override;

    Patch::NodeID getId() const { return id; }
    ModuleProcessor& getModule() const { return *module; }

    juce::Point<float> getPortCentre (bool isInput, int index) const;  // local coordinates
    int hitPort (juce::Point<float> local, bool& isInput) const;       // -1 if none

    void setSelected (bool);
    void openEditor();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showMenu();
    int portRows() const;

    PatchCanvas& canvas;
    Patch& patch;
    Patch::NodeID id;
    ModuleProcessor* module;
    juce::Colour accent;

    juce::OwnedArray<juce::Slider> knobs;
    juce::StringArray knobNames;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::StringArray comboNames;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAttachments;
    std::unique_ptr<juce::TextButton> editButton;
    std::unique_ptr<juce::DocumentWindow> editorWindow;

    juce::Rectangle<int> meterArea;
    float shownMeter = -1;
    bool selected = false, draggingNode = false, draggingCable = false;
    juce::Point<int> dragStart;
};
