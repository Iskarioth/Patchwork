#include "DrumGridEditor.h"
#include "Theme.h"

DrumGridEditor::DrumGridEditor (DrumSequencerModule& m) : module (m)
{
    clearButton.onClick = [this] {
        module.clearAll();
        repaint();
    };
    addAndMakeVisible (clearButton);
    setSize (labelW + 16 * 46 + 30, toolbarH + DrumSequencerModule::numLanes * rowH + 16);
    startTimerHz (30);
}

void DrumGridEditor::resized()
{
    clearButton.setBounds (8, 7, 70, 26);
}

juce::Rectangle<int> DrumGridEditor::cellBounds (int lane, int step) const
{
    const float avail = (float) (getWidth() - labelW - 16 - 3 * 6);
    const float cw = avail / 16.0f;
    const int x = labelW + juce::roundToInt (step * cw) + (step / 4) * 6;
    return { x, toolbarH + lane * rowH, juce::roundToInt (cw) - 4, rowH - 5 };
}

bool DrumGridEditor::cellAt (juce::Point<int> p, int& lane, int& step) const
{
    for (int l = 0; l < DrumSequencerModule::numLanes; ++l)
        for (int s = 0; s < DrumSequencerModule::numSteps; ++s)
            if (cellBounds (l, s).expanded (2).contains (p))
            {
                lane = l;
                step = s;
                return true;
            }
    return false;
}

void DrumGridEditor::timerCallback()
{
    const double ph = module.getPlayheadStep();
    const int step = ph < 0 ? -1 : (int) ph % 16;
    if (step != shownStep)
    {
        shownStep = step;
        repaint();
    }
}

void DrumGridEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Click toggles a step. Right-click a lit step to change velocity. Drag to paint. Click a name to audition.",
                90, 0, getWidth() - 100, toolbarH, juce::Justification::centredLeft);

    const auto accent = Theme::forCategory ("Sequencers");
    for (int l = 0; l < DrumSequencerModule::numLanes; ++l)
    {
        const auto first = cellBounds (l, 0);
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (drumLanes[(size_t) l].name, 10, first.getY(), labelW - 16, first.getHeight(), juce::Justification::centredLeft);

        for (int s = 0; s < DrumSequencerModule::numSteps; ++s)
        {
            const auto r = cellBounds (l, s).toFloat();
            const int v = module.getStep (l, s);
            g.setColour (v == 0 ? (s % 4 == 0 ? juce::Colour (0xff2e333a) : juce::Colour (0xff262a30))
                                : accent.withAlpha (v == 1 ? 0.35f : v == 2 ? 0.65f : 1.0f));
            g.fillRoundedRectangle (r, 3.0f);
            if (s == shownStep)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (r.reduced (1), 3.0f, 2.0f);
            }
        }
    }
}

void DrumGridEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (p.x < labelW && p.y > toolbarH)
    {
        const int lane = (p.y - toolbarH) / rowH;
        if (lane >= 0 && lane < DrumSequencerModule::numLanes)
            module.preview (drumLanes[(size_t) lane].pitch, 1.0f, 0.1);
        return;
    }

    int lane, step;
    if (! cellAt (p, lane, step))
        return;

    const int v = module.getStep (lane, step);
    if (e.mods.isPopupMenu() || e.mods.isAltDown())
    {
        if (v > 0)
            module.setStep (lane, step, v == 1 ? 3 : v - 1);
        paintValue = -1;
    }
    else
    {
        paintValue = v > 0 ? 0 : 3;
        module.setStep (lane, step, paintValue);
    }
    repaint();
}

void DrumGridEditor::mouseDrag (const juce::MouseEvent& e)
{
    int lane, step;
    if (paintValue >= 0 && cellAt (e.getPosition(), lane, step) && module.getStep (lane, step) != paintValue)
    {
        module.setStep (lane, step, paintValue);
        repaint();
    }
}

void DrumGridEditor::mouseUp (const juce::MouseEvent&)
{
    paintValue = -1;
}
