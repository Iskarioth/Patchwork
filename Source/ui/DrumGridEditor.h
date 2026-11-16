#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../modules/DrumSequencerModule.h"

// 8 x 16 step grid. Click toggles, right-click cycles velocity, drag paints.
class DrumGridEditor : public juce::Component, private juce::Timer
{
public:
    explicit DrumGridEditor (DrumSequencerModule&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    static constexpr int toolbarH = 40, labelW = 110, rowH = 34;

    void timerCallback() override;
    juce::Rectangle<int> cellBounds (int lane, int step) const;
    bool cellAt (juce::Point<int>, int& lane, int& step) const;

    DrumSequencerModule& module;
    juce::TextButton clearButton { "Clear" };
    int paintValue = -1;
    int shownStep = -2;
};
