#include "PianoRollEditor.h"
#include "Theme.h"

namespace
{
bool isBlackKey (int p) { const int n = p % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
}

// ------------------------------------------------------------------ grid

PianoRollGrid::PianoRollGrid (PianoRollModule& m) : module (m)
{
    notes = module.getNotes();
    startTimerHz (30);
}

void PianoRollGrid::reload()
{
    notes = module.getNotes();
    repaint();
}

double PianoRollGrid::middlePitch() const
{
    if (notes.empty())
        return 60;
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    return (lo + hi) * 0.5;
}

void PianoRollGrid::timerCallback()
{
    const double step = module.getPlayheadStep();
    if (std::abs (step - shownStep) > 0.01)
    {
        shownStep = step;
        repaint();
    }
}

PianoRollGrid::Pos PianoRollGrid::posFor (juce::Point<float> p) const
{
    const int col = juce::jlimit (0, loopSteps() - 1, (int) std::floor ((p.x - keyW) / cellW()));
    const int pitch = juce::jlimit (bottomPitch, topPitch, topPitch - (int) std::floor (p.y / rowH));
    return { p.x, col, pitch };
}

int PianoRollGrid::hitNote (const Pos& pos) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(size_t) i];
        if (n.pitch == pos.pitch && pos.col >= n.start && pos.col < n.start + n.length && n.start < loopSteps())
            return i;
    }
    return -1;
}

bool PianoRollGrid::nearRightEdge (const SeqNote& n, float x) const
{
    const float end = keyW + (float) (n.start + n.length) * cellW();
    return x > end - std::min (8.0f, cellW() * 0.45f);
}

void PianoRollGrid::paint (juce::Graphics& g)
{
    const int loop = loopSteps();
    const float cw = cellW();
    const int w = getWidth();

    for (int p = topPitch; p >= bottomPitch; --p)
    {
        const int y = yForPitch (p);
        const bool black = isBlackKey (p);
        g.setColour (black ? juce::Colour (0xff16181b) : juce::Colour (0xff1d2024));
        g.fillRect (keyW, y, w - keyW, rowH);
        g.setColour (p % 12 == 0 ? juce::Colour (0xff3d434b) : juce::Colour (0xff24272c));
        g.fillRect (keyW, y + rowH - 1, w - keyW, 1);

        g.setColour (black ? juce::Colour (0xff2a2d31) : juce::Colour (0xffcfd2d6));
        g.fillRect (0, y, keyW - 1, rowH);
        if (! black)
        {
            g.setColour (juce::Colour (0xff9ea3a9));
            g.fillRect (0, y + rowH - 1, keyW - 1, 1);
        }
        if (p % 12 == 0)
        {
            g.setColour (juce::Colour (0xff333333));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::MidiMessage::getMidiNoteName (p, true, true, 4), 3, y, keyW - 6, rowH, juce::Justification::centredLeft);
        }
    }

    for (int c = 0; c <= loop; ++c)
    {
        const int x = juce::roundToInt (keyW + c * cw);
        g.setColour (c % 16 == 0 ? juce::Colour (0xff5a616a) : c % 4 == 0 ? juce::Colour (0xff353a41) : juce::Colour (0xff25282d));
        g.fillRect (x, 0, 1, getHeight());
    }

    const auto noteColour = Theme::forCategory ("Modulators");
    for (const auto& n : notes)
    {
        if (n.start >= loop || n.pitch > topPitch || n.pitch < bottomPitch)
            continue;
        const float x = keyW + (float) n.start * cw;
        const float len = (float) std::min (n.length, loop - n.start) * cw;
        const auto r = juce::Rectangle<float> (x + 1, (float) yForPitch (n.pitch) + 1, len - 2, rowH - 2.0f);
        g.setColour (noteColour.withAlpha (0.45f + 0.55f * n.velocity));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (noteColour.darker (0.6f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (r.getRight() - 3, r.getY() + 2, 2.0f, r.getHeight() - 4);
    }

    if (shownStep >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect ((float) keyW + (float) shownStep * cw, 0.0f, 1.5f, (float) getHeight());
    }
}

void PianoRollGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = posFor (e.position);
    if (e.x < keyW)
    {
        audition (pos.pitch);
        return;
    }

    const int hit = hitNote (pos);
    if (e.mods.isPopupMenu() || (e.mods.isAltDown() && hit >= 0))
    {
        if (hit >= 0)
        {
            notes.erase (notes.begin() + hit);
            push();
            repaint();
        }
        return;
    }

    if (hit >= 0)
    {
        dragIndex = hit;
        auto& n = notes[(size_t) hit];
        if (nearRightEdge (n, pos.x))
            mode = Mode::resize;
        else
        {
            mode = Mode::move;
            dragOffset = pos.col - (int) n.start;
            lastPitch = n.pitch;
            audition (n.pitch);
        }
    }
    else
    {
        const double len = std::min (lastLength, (double) (loopSteps() - pos.col));
        notes.push_back ({ pos.pitch, (double) pos.col, len, 0.8f });
        dragIndex = (int) notes.size() - 1;
        dragOrigin = pos.col;
        mode = Mode::create;
        audition (pos.pitch);
        push();
        repaint();
    }
}

void PianoRollGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (mode == Mode::none || dragIndex < 0 || dragIndex >= (int) notes.size())
        return;

    const auto pos = posFor (e.position);
    auto& n = notes[(size_t) dragIndex];
    const int loop = loopSteps();

    if (mode == Mode::create && pos.col != dragOrigin)
        mode = Mode::resize;

    if (mode == Mode::move)
    {
        n.start = juce::jlimit (0.0, std::max (0.0, loop - n.length), (double) (pos.col - dragOffset));
        if (pos.pitch != lastPitch)
        {
            n.pitch = lastPitch = pos.pitch;
            audition (n.pitch);
        }
    }
    else if (mode == Mode::resize)
    {
        n.length = juce::jlimit (1.0, loop - n.start, (double) (pos.col - n.start + 1));
        lastLength = n.length;
    }
    push();
    repaint();
}

void PianoRollGrid::mouseUp (const juce::MouseEvent&)
{
    mode = Mode::none;
    dragIndex = -1;
}

void PianoRollGrid::mouseMove (const juce::MouseEvent& e)
{
    if (e.x < keyW)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }
    const auto pos = posFor (e.position);
    const int hit = hitNote (pos);
    setMouseCursor (hit < 0 ? juce::MouseCursor::CrosshairCursor
                    : nearRightEdge (notes[(size_t) hit], pos.x) ? juce::MouseCursor::LeftRightResizeCursor
                                                                  : juce::MouseCursor::DraggingHandCursor);
}

void PianoRollGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // wheel over a note changes its velocity, anywhere else scrolls
    const int hit = hitNote (posFor (e.position));
    if (hit < 0)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }
    auto& n = notes[(size_t) hit];
    n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity + (wheel.deltaY > 0 ? 0.05f : -0.05f));
    push();
    repaint();
}

// ------------------------------------------------------------------ editor window content

PianoRollEditor::PianoRollEditor (PianoRollModule& m) : module (m), grid (m)
{
    barsBox.addItemList ({ "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
    const int bars = module.getBars();
    barsBox.setSelectedId (bars >= 8 ? 4 : bars >= 4 ? 3 : bars >= 2 ? 2 : 1, juce::dontSendNotification);
    barsBox.onChange = [this] {
        static constexpr int values[] = { 1, 2, 4, 8 };
        module.setBars (values[barsBox.getSelectedId() - 1]);
        grid.repaint();
    };
    clearButton.onClick = [this] {
        module.setNotes ({});
        grid.reload();
    };

    addAndMakeVisible (barsBox);
    addAndMakeVisible (clearButton);
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (1000, 580);
}

void PianoRollEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Click to add, drag to set length. Drag to move, drag the right edge to resize. "
                "Right-click deletes. Wheel over a note = velocity. Click the keys to audition.",
                200, 0, getWidth() - 210, 36, juce::Justification::centredLeft);
}

void PianoRollEditor::resized()
{
    auto b = getLocalBounds();
    auto bar = b.removeFromTop (36).reduced (6);
    barsBox.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (8);
    clearButton.setBounds (bar.removeFromLeft (70));
    viewport.setBounds (b);
    grid.setSize (viewport.getMaximumVisibleWidth(), grid.idealHeight());

    if (! scrolledToNotes && viewport.getHeight() > 0)
    {
        scrolledToNotes = true;
        const int y = grid.yForPitch ((int) grid.middlePitch()) - viewport.getHeight() / 2;
        viewport.setViewPosition (0, std::max (0, y));
    }
}
