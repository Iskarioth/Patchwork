#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../modules/PianoRollModule.h"

// The note grid. Edits a local copy of the notes and pushes it to the module on every change.
class PianoRollGrid : public juce::Component, private juce::Timer
{
public:
    static constexpr int topPitch = 96, bottomPitch = 24, rowH = 13, keyW = 48;

    explicit PianoRollGrid (PianoRollModule&);
    int idealHeight() const { return (topPitch - bottomPitch + 1) * rowH; }
    void reload();
    int yForPitch (int pitch) const { return (topPitch - pitch) * rowH; }
    double middlePitch() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Pos { float x; int col; int pitch; };
    enum class Mode { none, move, resize, create };

    void timerCallback() override;
    int loopSteps() const { return module.getBars() * 16; }
    float cellW() const { return (float) (getWidth() - keyW) / (float) loopSteps(); }
    Pos posFor (juce::Point<float>) const;
    int hitNote (const Pos&) const;
    bool nearRightEdge (const SeqNote&, float x) const;
    void push() { module.setNotes (notes); }
    void audition (int pitch) { module.preview (pitch, 0.8f, 0.25); }

    PianoRollModule& module;
    std::vector<SeqNote> notes;
    Mode mode = Mode::none;
    int dragIndex = -1, dragOffset = 0, dragOrigin = 0, lastPitch = -1;
    double lastLength = 4;
    double shownStep = -1;
};

class PianoRollEditor : public juce::Component
{
public:
    explicit PianoRollEditor (PianoRollModule&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PianoRollModule& module;
    PianoRollGrid grid;
    juce::Viewport viewport;
    juce::ComboBox barsBox;
    juce::TextButton clearButton { "Clear" };
    bool scrolledToNotes = false;
};
