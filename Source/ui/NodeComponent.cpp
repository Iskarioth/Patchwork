#include "NodeComponent.h"
#include "PatchCanvas.h"
#include "Theme.h"
#include "PianoRollEditor.h"
#include "DrumGridEditor.h"
#include "../engine/ModuleRegistry.h"

namespace
{
class EditorWindow : public juce::DocumentWindow
{
public:
    EditorWindow (const juce::String& title, std::unique_ptr<juce::Component> content)
        : DocumentWindow (title, Theme::background, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content.release(), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { setVisible (false); }
};

std::unique_ptr<juce::Component> createEditorFor (ModuleProcessor& m)
{
    if (auto* roll = dynamic_cast<PianoRollModule*> (&m))
        return std::make_unique<PianoRollEditor> (*roll);
    if (auto* seq = dynamic_cast<DrumSequencerModule*> (&m))
        return std::make_unique<DrumGridEditor> (*seq);
    return nullptr;
}
} // namespace

NodeComponent::NodeComponent (PatchCanvas& c, Patch& p, Patch::NodeID nodeId)
    : canvas (c), patch (p), id (nodeId), module (p.getModule (nodeId))
{
    jassert (module != nullptr);
    const auto* type = findModuleType (patch.getModuleType (id));
    accent = Theme::forCategory (type != nullptr ? type->category : juce::String());

    for (auto* param : module->getParameters())
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            auto* cb = combos.add (new juce::ComboBox());
            cb->addItemList (choice->choices, 1);
            comboAttachments.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*choice, *cb));
            comboNames.add (choice->getName (32));
            addAndMakeVisible (cb);
        }
        else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            s->setColour (juce::Slider::rotarySliderFillColourId, accent);
            sliderAttachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*ranged, *s));
            s->setDoubleClickReturnValue (true, ranged->convertFrom0to1 (ranged->getDefaultValue()));
            s->setPopupDisplayEnabled (true, true, nullptr, 800);
            knobNames.add (ranged->getName (16));
            addAndMakeVisible (s);
        }
    }

    if (module->hasModuleEditor())
    {
        editButton = std::make_unique<juce::TextButton> ("Open editor");
        editButton->onClick = [this] { openEditor(); };
        addAndMakeVisible (*editButton);
    }

    const int knobRows = (knobs.size() + knobCols - 1) / knobCols;
    int h = titleH + 4 + portRows() * portRowH + 6 + combos.size() * choiceH + knobRows * knobH;
    if (editButton) h += 32;
    if (module->getMeterLevel() >= 0) h += 16;
    setSize (width, h + 8);

    if (module->getMeterLevel() >= 0)
        startTimerHz (30);
}

NodeComponent::~NodeComponent()
{
    editorWindow = nullptr;
}

int NodeComponent::portRows() const
{
    return (int) std::max (module->getInputPorts().size(), module->getOutputPorts().size());
}

void NodeComponent::resized()
{
    int y = titleH + 4 + portRows() * portRowH + 6;
    for (int i = 0; i < combos.size(); ++i, y += choiceH)
        combos[i]->setBounds (96, y + 2, width - 106, choiceH - 4);

    for (int i = 0; i < knobs.size(); ++i)
    {
        const int col = i % knobCols, row = i / knobCols;
        const int x = 8 + col * ((width - 16) / knobCols) + ((width - 16) / knobCols - knobW) / 2;
        knobs[i]->setBounds (x, y + row * knobH, knobW, knobH - 18);
    }
    y += ((knobs.size() + knobCols - 1) / knobCols) * knobH;

    if (editButton)
    {
        editButton->setBounds (10, y + 2, width - 20, 24);
        y += 32;
    }
    meterArea = { 10, y + 2, width - 20, 8 };
}

juce::Point<float> NodeComponent::getPortCentre (bool isInput, int index) const
{
    const float y = (float) (titleH + 4 + index * portRowH) + portRowH * 0.5f;
    return { isInput ? 10.0f : (float) width - 10.0f, y };
}

int NodeComponent::hitPort (juce::Point<float> local, bool& isInput) const
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool in = pass == 0;
        const auto& ports = in ? module->getInputPorts() : module->getOutputPorts();
        for (int i = 0; i < (int) ports.size(); ++i)
        {
            // generous hit area: the circle plus its label
            const auto c = getPortCentre (in, i);
            const auto area = juce::Rectangle<float> (in ? 0.0f : (float) width - 90.0f, c.y - portRowH * 0.5f, 90.0f, (float) portRowH);
            if (area.contains (local))
            {
                isInput = in;
                return i;
            }
        }
    }
    return -1;
}

void NodeComponent::setSelected (bool s)
{
    selected = s;
    repaint();
}

void NodeComponent::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Theme::node);
    g.fillRoundedRectangle (b, 6.0f);

    // title bar with a category stripe
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (b, 6.0f);
        g.reduceClipRegion (clip);
        g.setColour (Theme::nodeTitle);
        g.fillRect (0, 0, width, titleH);
        g.setColour (accent);
        g.fillRect (0, 0, width, 3);
    }
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (module->getDisplayName(), 10, 3, width - 20, titleH - 3, juce::Justification::centredLeft);

    // ports
    const auto dragType = canvas.getDragType();
    g.setFont (juce::FontOptions (12.0f));
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool in = pass == 0;
        const auto& ports = in ? module->getInputPorts() : module->getOutputPorts();
        for (int i = 0; i < (int) ports.size(); ++i)
        {
            const auto c = getPortCentre (in, i);
            const auto col = Theme::forPort (ports[(size_t) i].type);
            const bool compatible = dragType && *dragType == ports[(size_t) i].type && canvas.isDraggingFromInput() != in;
            g.setColour (col);
            g.fillEllipse (c.x - 5, c.y - 5, 10, 10);
            if (compatible)
            {
                g.setColour (juce::Colours::white);
                g.drawEllipse (c.x - 8, c.y - 8, 16, 16, 1.5f);
            }
            g.setColour (dragType && ! compatible ? Theme::dim.withAlpha (0.4f) : Theme::dim);
            if (in) g.drawText (ports[(size_t) i].name, (int) c.x + 10, (int) c.y - 8, 100, 16, juce::Justification::centredLeft);
            else    g.drawText (ports[(size_t) i].name, (int) c.x - 110, (int) c.y - 8, 100, 16, juce::Justification::centredRight);
        }
    }

    // parameter labels
    g.setColour (Theme::dim);
    g.setFont (juce::FontOptions (11.0f));
    for (int i = 0; i < combos.size(); ++i)
        g.drawText (comboNames[i], 10, combos[i]->getY(), 84, combos[i]->getHeight(), juce::Justification::centredLeft);
    for (int i = 0; i < knobs.size(); ++i)
    {
        const auto kb = knobs[i]->getBounds();
        g.drawText (knobNames[i], kb.getX() - 6, kb.getBottom(), kb.getWidth() + 12, 14, juce::Justification::centred);
    }

    // meter (output level, LFO position)
    if (module->getMeterLevel() >= 0)
    {
        g.setColour (juce::Colour (0xff15171a));
        g.fillRoundedRectangle (meterArea.toFloat(), 3.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, shownMeter);
        g.setColour (lvl > 0.95f ? juce::Colour (0xffe55353) : accent);
        g.fillRoundedRectangle (meterArea.toFloat().withWidth (meterArea.getWidth() * lvl), 3.0f);
    }

    g.setColour (selected ? accent : Theme::line);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, selected ? 2.0f : 1.0f);
}

void NodeComponent::timerCallback()
{
    const float m = module->getMeterLevel();
    if (std::abs (m - shownMeter) > 0.003f)
    {
        shownMeter = m;
        repaint (meterArea.expanded (2));
    }
}

void NodeComponent::mouseDown (const juce::MouseEvent& e)
{
    bool isInput = false;
    const int port = hitPort (e.position, isInput);

    if (port >= 0 && ! e.mods.isPopupMenu())
    {
        draggingCable = true;
        canvas.beginCableDrag (*this, isInput, port);
        return;
    }

    canvas.selectNode (this);
    toFront (false);

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    draggingNode = true;
    dragStart = getPosition();
}

void NodeComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingCable)
        canvas.updateCableDrag (e.getEventRelativeTo (&canvas).position);
    else if (draggingNode)
    {
        auto p = dragStart + e.getOffsetFromDragStart();
        p = { std::max (0, p.x), std::max (0, p.y) };
        setTopLeftPosition (p);
        patch.setPosition (id, p);
        canvas.repaint();
    }
}

void NodeComponent::mouseUp (const juce::MouseEvent& e)
{
    if (draggingCable)
        canvas.endCableDrag (e.getEventRelativeTo (&canvas).position);
    draggingCable = draggingNode = false;
}

void NodeComponent::mouseMove (const juce::MouseEvent& e)
{
    bool isInput = false;
    setMouseCursor (hitPort (e.position, isInput) >= 0 ? juce::MouseCursor::CrosshairCursor
                    : e.y < titleH ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::NormalCursor);
}

void NodeComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < titleH)
        openEditor();
}

void NodeComponent::showMenu()
{
    juce::PopupMenu menu;
    if (module->hasModuleEditor())
        menu.addItem (1, "Open editor");
    menu.addItem (2, "Delete module");

    menu.showMenuAsync (juce::PopupMenu::Options(), [safeCanvas = juce::Component::SafePointer<PatchCanvas> (&canvas),
                                                     safeThis = juce::Component::SafePointer<NodeComponent> (this),
                                                     nodeId = id] (int result) {
        if (result == 1 && safeThis != nullptr)
            safeThis->openEditor();
        else if (result == 2 && safeCanvas != nullptr)
            safeCanvas->deleteNode (nodeId);
    });
}

void NodeComponent::openEditor()
{
    if (! module->hasModuleEditor())
        return;
    if (editorWindow == nullptr)
        if (auto content = createEditorFor (*module))
            editorWindow = std::make_unique<EditorWindow> (module->getDisplayName(), std::move (content));
    if (editorWindow != nullptr)
    {
        editorWindow->setVisible (true);
        editorWindow->toFront (true);
    }
}
