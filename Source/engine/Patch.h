#pragma once

#include "Module.h"
#include "Transport.h"
#include <optional>

// The patch: which modules exist, where they sit on the canvas, and how they are
// cabled. It owns the AudioProcessorGraph and translates port-level cables into
// graph channel connections. It knows nothing about the UI or the audio device.
class Patch
{
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    struct Cable
    {
        NodeID src;
        int srcPort = 0;
        NodeID dst;
        int dstPort = 0;
        bool operator== (const Cable&) const = default;
    };

    explicit Patch (Transport&);

    juce::AudioProcessorGraph& getGraph() { return graph; }
    Transport& getTransport() { return transport; }

    NodeID addModule (const juce::String& typeId, juce::Point<int> position, std::optional<NodeID> id = std::nullopt);
    void removeModule (NodeID);
    ModuleProcessor* getModule (NodeID) const;
    juce::String getModuleType (NodeID) const;
    std::vector<NodeID> getModuleIds() const;

    juce::Point<int> getPosition (NodeID) const;
    void setPosition (NodeID, juce::Point<int>);

    bool canConnect (const Cable&) const;
    bool connect (const Cable&);
    void disconnect (const Cable&);
    const std::vector<Cable>& getCables() const { return cables; }

    void clear();
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);
    void buildDefault();

    static void setParam (ModuleProcessor*, const juce::String& paramId, float plainValue);

private:
    std::vector<juce::AudioProcessorGraph::Connection> toConnections (const Cable&) const;
    void addOutputDevice();

    Transport& transport;
    juce::AudioProcessorGraph graph;
    NodeID deviceOut;
    std::vector<Cable> cables;
};
