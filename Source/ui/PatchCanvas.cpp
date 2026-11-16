#include "PatchCanvas.h"
#include "Theme.h"
#include "../engine/ModuleRegistry.h"

PatchCanvas::PatchCanvas (Patch& p) : patch (p)
{
    setSize (4000, 2600);
    setWantsKeyboardFocus (true);
}

void PatchCanvas::rebuild()
{
    selected = nullptr;
    drag.reset();
    nodes.clear();
    for (auto id : patch.getModuleIds())
    {
        auto* n = nodes.add (new NodeComponent (*this, patch, id));
        addAndMakeVisible (n);
        n->setTopLeftPosition (patch.getPosition (id));
    }
    repaint();
}

void PatchCanvas::addModuleAt (const juce::String& typeId, juce::Point<int> pos)
{
    const auto id = patch.addModule (typeId, pos);
    if (id == Patch::NodeID {})
        return;
    auto* n = nodes.add (new NodeComponent (*this, patch, id));
    addAndMakeVisible (n);
    n->setTopLeftPosition (pos);
    selectNode (n);
}

void PatchCanvas::deleteNode (Patch::NodeID id)
{
    if (auto* n = findNode (id))
    {
        if (selected == n)
            selected = nullptr;
        nodes.removeObject (n); // closes its editor window before the module goes away
    }
    patch.removeModule (id);
    repaint();
}

void PatchCanvas::selectNode (NodeComponent* n)
{
    if (selected != nullptr && selected != n)
        selected->setSelected (false);
    selected = n;
    if (n != nullptr)
        n->setSelected (true);
    grabKeyboardFocus();
}

NodeComponent* PatchCanvas::findNode (Patch::NodeID id) const
{
    for (auto* n : nodes)
        if (n->getId() == id)
            return n;
    return nullptr;
}

juce::Point<float> PatchCanvas::portPosition (Patch::NodeID id, bool isInput, int port) const
{
    if (auto* n = findNode (id))
        return n->getPortCentre (isInput, port) + n->getPosition().toFloat();
    return {};
}

juce::Path PatchCanvas::cablePath (juce::Point<float> from, juce::Point<float> to)
{
    const float dx = std::max (50.0f, std::abs (to.x - from.x) * 0.5f);
    juce::Path p;
    p.startNewSubPath (from);
    p.cubicTo (from.x + dx, from.y, to.x - dx, to.y, to.x, to.y);
    return p;
}

std::optional<Patch::Cable> PatchCanvas::cableAt (juce::Point<float> pos) const
{
    for (const auto& c : patch.getCables())
    {
        juce::Point<float> nearest;
        cablePath (portPosition (c.src, false, c.srcPort), portPosition (c.dst, true, c.dstPort)).getNearestPoint (pos, nearest);
        if (nearest.getDistanceFrom (pos) < 7.0f)
            return c;
    }
    return std::nullopt;
}

void PatchCanvas::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    const auto clip = g.getClipBounds();
    g.setColour (Theme::gridLine);
    for (int x = clip.getX() - clip.getX() % 32; x < clip.getRight(); x += 32)
        g.fillRect (x, clip.getY(), 1, clip.getHeight());
    for (int y = clip.getY() - clip.getY() % 32; y < clip.getBottom(); y += 32)
        g.fillRect (clip.getX(), y, clip.getWidth(), 1);

    for (const auto& c : patch.getCables())
    {
        auto* src = patch.getModule (c.src);
        if (src == nullptr || c.srcPort >= (int) src->getOutputPorts().size())
            continue;
        const auto colour = Theme::forPort (src->getOutputPorts()[(size_t) c.srcPort].type);
        const auto path = cablePath (portPosition (c.src, false, c.srcPort), portPosition (c.dst, true, c.dstPort));
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (path, juce::PathStrokeType (7.0f));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.5f));
    }
}

void PatchCanvas::paintOverChildren (juce::Graphics& g)
{
    if (! drag)
        return;
    const auto anchor = portPosition (drag->node, drag->fromInput, drag->port);
    const auto path = drag->fromInput ? cablePath (drag->end, anchor) : cablePath (anchor, drag->end);
    g.setColour (Theme::forPort (drag->type).withAlpha (0.9f));
    g.strokePath (path, juce::PathStrokeType (2.5f));
}

// ------------------------------------------------------------------ cable dragging

void PatchCanvas::beginCableDrag (NodeComponent& node, bool fromInput, int port)
{
    // Grabbing an input that already has a cable picks that cable up from its plug end.
    if (fromInput)
        for (auto it = patch.getCables().rbegin(); it != patch.getCables().rend(); ++it)
            if (it->dst == node.getId() && it->dstPort == port)
            {
                const auto c = *it;
                patch.disconnect (c);
                const auto type = patch.getModule (c.src)->getOutputPorts()[(size_t) c.srcPort].type;
                drag = Drag { c.src, false, c.srcPort, type, portPosition (c.dst, true, c.dstPort) };
                for (auto* n : nodes) n->repaint();
                repaint();
                return;
            }

    const auto& ports = fromInput ? node.getModule().getInputPorts() : node.getModule().getOutputPorts();
    drag = Drag { node.getId(), fromInput, port, ports[(size_t) port].type, portPosition (node.getId(), fromInput, port) };
    for (auto* n : nodes) n->repaint();
}

void PatchCanvas::updateCableDrag (juce::Point<float> pos)
{
    if (drag)
    {
        drag->end = pos;
        repaint();
    }
}

void PatchCanvas::endCableDrag (juce::Point<float> pos)
{
    if (! drag)
        return;

    for (auto* n : nodes)
    {
        bool isInput = false;
        const int port = n->hitPort (pos - n->getPosition().toFloat(), isInput);
        if (port < 0 || isInput == drag->fromInput)
            continue;
        const auto cable = drag->fromInput ? Patch::Cable { n->getId(), port, drag->node, drag->port }
                                           : Patch::Cable { drag->node, drag->port, n->getId(), port };
        patch.connect (cable); // silently ignored if the port types don't match
        break;
    }

    drag.reset();
    for (auto* n : nodes) n->repaint();
    repaint();
}

// ------------------------------------------------------------------ background mouse

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    if (e.mods.isPopupMenu())
    {
        if (const auto cable = cableAt (e.position))
        {
            juce::PopupMenu menu;
            menu.addItem (1, "Remove cable");
            menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PatchCanvas> (this), c = *cable] (int r) {
                if (r == 1 && safe != nullptr)
                {
                    safe->patch.disconnect (c);
                    safe->repaint();
                }
            });
        }
        else
            showAddMenu (e.getPosition());
        return;
    }

    selectNode (nullptr);
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        panning = true;
        panStartView = vp->getViewPosition();
    }
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (panning)
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            // offset is measured in screen space so the canvas moving under the mouse doesn't feed back
            const auto offset = e.getScreenPosition() - e.getMouseDownScreenPosition();
            vp->setViewPosition (panStartView - offset);
        }
}

void PatchCanvas::mouseUp (const juce::MouseEvent&)
{
    panning = false;
}

bool PatchCanvas::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected != nullptr)
    {
        deleteNode (selected->getId());
        return true;
    }
    return false;
}

void PatchCanvas::showAddMenu (juce::Point<int> pos)
{
    juce::PopupMenu menu;
    juce::StringArray categories;
    for (auto& t : getModuleTypes())
        categories.addIfNotAlreadyThere (t.category);

    const auto& types = getModuleTypes();
    for (auto& cat : categories)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) types.size(); ++i)
            if (types[(size_t) i].category == cat)
                sub.addItem (i + 1, types[(size_t) i].name);
        menu.addSubMenu (cat, sub);
    }

    menu.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<PatchCanvas> (this), pos] (int r) {
        if (r > 0 && safe != nullptr)
            safe->addModuleAt (getModuleTypes()[(size_t) r - 1].id, pos);
    });
}
