#include "Patch.h"
#include "ModuleRegistry.h"
#include "../modules/PianoRollModule.h"
#include "../modules/DrumSequencerModule.h"

using Graph = juce::AudioProcessorGraph;

Patch::Patch (Transport& t) : transport (t)
{
    addOutputDevice();
}

void Patch::addOutputDevice()
{
    deviceOut = graph.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (Graph::AudioGraphIOProcessor::audioOutputNode))->nodeID;
}

Patch::NodeID Patch::addModule (const juce::String& typeId, juce::Point<int> position, std::optional<NodeID> id)
{
    auto proc = createModule (typeId);
    if (proc == nullptr)
        return {};

    proc->setTransport (&transport);
    auto node = graph.addNode (std::move (proc), id);
    if (node == nullptr)
        return {};

    node->properties.set ("type", typeId);
    setPosition (node->nodeID, position);

    // Output modules are wired to the sound card behind the scenes.
    if (typeId == "output")
        for (int ch = 0; ch < 2; ++ch)
            graph.addConnection ({ { node->nodeID, ch }, { deviceOut, ch } });

    return node->nodeID;
}

void Patch::removeModule (NodeID id)
{
    std::erase_if (cables, [id] (const Cable& c) { return c.src == id || c.dst == id; });
    graph.removeNode (id);
}

ModuleProcessor* Patch::getModule (NodeID id) const
{
    if (auto* node = graph.getNodeForId (id))
        return dynamic_cast<ModuleProcessor*> (node->getProcessor());
    return nullptr;
}

juce::String Patch::getModuleType (NodeID id) const
{
    if (auto* node = graph.getNodeForId (id))
        return node->properties["type"].toString();
    return {};
}

std::vector<Patch::NodeID> Patch::getModuleIds() const
{
    std::vector<NodeID> ids;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<ModuleProcessor*> (node->getProcessor()) != nullptr)
            ids.push_back (node->nodeID);
    return ids;
}

juce::Point<int> Patch::getPosition (NodeID id) const
{
    if (auto* node = graph.getNodeForId (id))
        return { (int) node->properties["x"], (int) node->properties["y"] };
    return {};
}

void Patch::setPosition (NodeID id, juce::Point<int> p)
{
    if (auto* node = graph.getNodeForId (id))
    {
        node->properties.set ("x", p.x);
        node->properties.set ("y", p.y);
    }
}

std::vector<Graph::Connection> Patch::toConnections (const Cable& c) const
{
    std::vector<Graph::Connection> result;
    auto* s = getModule (c.src);
    auto* d = getModule (c.dst);
    if (s == nullptr || d == nullptr
        || c.srcPort < 0 || c.srcPort >= (int) s->getOutputPorts().size()
        || c.dstPort < 0 || c.dstPort >= (int) d->getInputPorts().size())
        return result;

    const auto& sp = s->getOutputPorts()[(size_t) c.srcPort];
    const auto& dp = d->getInputPorts()[(size_t) c.dstPort];
    if (sp.type != dp.type)
        return result;

    if (sp.type == PortType::Note)
        result.push_back ({ { c.src, Graph::midiChannelIndex }, { c.dst, Graph::midiChannelIndex } });
    else
        for (int i = 0; i < portWidth (sp.type); ++i)
            result.push_back ({ { c.src, sp.channel + i }, { c.dst, dp.channel + i } });
    return result;
}

bool Patch::canConnect (const Cable& c) const
{
    if (c.src == c.dst || std::find (cables.begin(), cables.end(), c) != cables.end())
        return false;
    if (graph.isAnInputTo (c.dst, c.src)) // would create a feedback loop
        return false;

    const auto conns = toConnections (c);
    if (conns.empty())
        return false;
    for (auto& conn : conns)
        if (! graph.canConnect (conn))
            return false;
    return true;
}

bool Patch::connect (const Cable& c)
{
    if (! canConnect (c))
        return false;
    for (auto& conn : toConnections (c))
        graph.addConnection (conn);
    cables.push_back (c);
    return true;
}

void Patch::disconnect (const Cable& c)
{
    const auto it = std::find (cables.begin(), cables.end(), c);
    if (it == cables.end())
        return;
    for (auto& conn : toConnections (c))
        graph.removeConnection (conn);
    cables.erase (it);
}

void Patch::clear()
{
    cables.clear();
    graph.clear();
    addOutputDevice();
}

void Patch::setParam (ModuleProcessor* m, const juce::String& paramId, float plainValue)
{
    if (m == nullptr)
        return;
    for (auto* p : m->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && rp->getParameterID() == paramId)
            rp->setValueNotifyingHost (rp->convertTo0to1 (plainValue));
}

// ------------------------------------------------------------------ save / load

juce::ValueTree Patch::toValueTree() const
{
    juce::ValueTree root ("PATCH");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("bpm", transport.bpm.load(), nullptr);

    for (auto id : getModuleIds())
    {
        auto* m = getModule (id);
        const auto pos = getPosition (id);
        juce::ValueTree mt ("MODULE");
        mt.setProperty ("id", (int) id.uid, nullptr);
        mt.setProperty ("type", getModuleType (id), nullptr);
        mt.setProperty ("x", pos.x, nullptr);
        mt.setProperty ("y", pos.y, nullptr);

        juce::ValueTree params ("PARAMS");
        for (auto* p : m->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                params.setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()), nullptr);
        mt.appendChild (params, nullptr);

        juce::ValueTree extra ("EXTRA");
        m->saveExtra (extra);
        mt.appendChild (extra, nullptr);
        root.appendChild (mt, nullptr);
    }

    for (auto& c : cables)
    {
        juce::ValueTree ct ("CABLE");
        ct.setProperty ("src", (int) c.src.uid, nullptr);
        ct.setProperty ("srcPort", c.srcPort, nullptr);
        ct.setProperty ("dst", (int) c.dst.uid, nullptr);
        ct.setProperty ("dstPort", c.dstPort, nullptr);
        root.appendChild (ct, nullptr);
    }
    return root;
}

void Patch::fromValueTree (const juce::ValueTree& root)
{
    clear();
    if (! root.hasType ("PATCH"))
        return;

    transport.bpm = (double) root.getProperty ("bpm", 120.0);

    for (const auto& mt : root)
    {
        if (! mt.hasType ("MODULE"))
            continue;
        const auto id = addModule (mt["type"].toString(), { (int) mt["x"], (int) mt["y"] },
                                   NodeID { (juce::uint32) (int) mt["id"] });
        auto* m = getModule (id);
        if (m == nullptr)
            continue;

        const auto params = mt.getChildWithName ("PARAMS");
        for (int i = 0; i < params.getNumProperties(); ++i)
        {
            const auto name = params.getPropertyName (i);
            setParam (m, name.toString(), (float) params[name]);
        }
        m->loadExtra (mt.getChildWithName ("EXTRA"));
    }

    for (const auto& ct : root)
        if (ct.hasType ("CABLE"))
            connect ({ NodeID { (juce::uint32) (int) ct["src"] }, (int) ct["srcPort"],
                       NodeID { (juce::uint32) (int) ct["dst"] }, (int) ct["dstPort"] });
}

// ------------------------------------------------------------------ starter patch

void Patch::buildDefault()
{
    clear();
    transport.bpm = 118.0;

    const auto roll   = addModule ("pianoroll", { 40, 40 });
    const auto synth  = addModule ("synth", { 330, 40 });
    const auto filter = addModule ("filter", { 620, 40 });
    const auto delay  = addModule ("delay", { 910, 40 });
    const auto verb   = addModule ("reverb", { 1200, 40 });
    const auto lfo    = addModule ("lfo", { 330, 400 });
    const auto seq    = addModule ("drumseq", { 40, 620 });
    const auto drums  = addModule ("drums", { 330, 620 });
    const auto out    = addModule ("output", { 1200, 330 });

    connect ({ roll, 0, synth, 0 });
    connect ({ synth, 0, filter, 0 });
    connect ({ lfo, 0, filter, 1 });
    connect ({ filter, 0, delay, 0 });
    connect ({ delay, 0, verb, 0 });
    connect ({ verb, 0, out, 0 });
    connect ({ seq, 0, drums, 0 });
    connect ({ drums, 0, out, 0 });

    setParam (getModule (synth), "cutoff", 2600.0f);
    setParam (getModule (synth), "attack", 0.04f);
    setParam (getModule (synth), "release", 0.6f);
    setParam (getModule (filter), "cutoff", 1100.0f);
    setParam (getModule (filter), "moddepth", 0.35f);
    setParam (getModule (lfo), "sync", 3.0f);   // 1 bar
    setParam (getModule (lfo), "depth", 0.6f);
    setParam (getModule (out), "volume", -9.0f);

    if (auto* pr = dynamic_cast<PianoRollModule*> (getModule (roll)))
    {
        std::vector<SeqNote> notes;
        auto chord = [&] (double start, double len, std::initializer_list<int> pitches) {
            for (int p : pitches) notes.push_back ({ p, start, len, 0.75f });
        };
        chord (0, 14, { 48, 51, 55, 58 });   // Cm7
        chord (16, 14, { 44, 48, 51, 55 });  // Abmaj7
        pr->setNotes (std::move (notes));
        pr->setBars (2);
    }

    if (auto* ds = dynamic_cast<DrumSequencerModule*> (getModule (seq)))
    {
        auto set = [ds] (int lane, std::initializer_list<int> steps, int vel) {
            for (int s : steps) ds->setStep (lane, s, vel);
        };
        set (0, { 0, 4, 8, 12 }, 3);                                   // kick
        set (1, { 4, 12 }, 3);                                         // snare
        set (2, { 12 }, 2);                                            // clap
        set (3, { 0, 1, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15 }, 1);        // closed hat
        set (3, { 0, 4, 8, 12 }, 2);
        set (4, { 2, 6, 10, 14 }, 2);                                  // open hat
        set (7, { 7, 13 }, 1);                                         // rim
    }
}
