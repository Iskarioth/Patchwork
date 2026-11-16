#pragma once

#include "NodeComponent.h"
#include <optional>

// The workspace. Right-click to add modules, drag between ports to cable them,
// drag empty space to pan, right-click a cable to remove it.
class PatchCanvas : public juce::Component
{
public:
    explicit PatchCanvas (Patch&);

    void rebuild();                   // recreate node components from the patch
    void addModuleAt (const juce::String& typeId, juce::Point<int>);
    void deleteNode (Patch::NodeID);
    void selectNode (NodeComponent*);

    // cable dragging, driven by NodeComponent mouse events (canvas coordinates)
    void beginCableDrag (NodeComponent&, bool fromInput, int port);
    void updateCableDrag (juce::Point<float>);
    void endCableDrag (juce::Point<float>);
    std::optional<PortType> getDragType() const { return drag ? std::optional (drag->type) : std::nullopt; }
    bool isDraggingFromInput() const { return drag && drag->fromInput; }

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Drag { Patch::NodeID node; bool fromInput; int port; PortType type; juce::Point<float> end; };

    NodeComponent* findNode (Patch::NodeID) const;
    juce::Point<float> portPosition (Patch::NodeID, bool isInput, int port) const;
    std::optional<Patch::Cable> cableAt (juce::Point<float>) const;
    static juce::Path cablePath (juce::Point<float> from, juce::Point<float> to);
    void showAddMenu (juce::Point<int>);

    Patch& patch;
    juce::OwnedArray<NodeComponent> nodes;
    NodeComponent* selected = nullptr;
    std::optional<Drag> drag;
    bool panning = false;
    juce::Point<int> panStartView;
};
