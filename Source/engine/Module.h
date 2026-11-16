#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Ports.h"
#include "Transport.h"

// Base class for every module on the canvas. A module is an AudioProcessor living
// in the AudioProcessorGraph. It declares its ports and parameters; the UI builds
// itself from those, so most modules need no UI code at all.
class ModuleProcessor : public juce::AudioProcessor
{
public:
    ModuleProcessor (std::vector<PortInfo> inputs, std::vector<PortInfo> outputs, int hiddenOutputChannels = 0)
        : AudioProcessor (makeBuses (inputs, outputs, hiddenOutputChannels)),
          ins (std::move (inputs)), outs (std::move (outputs))
    {
    }

    virtual juce::String getTypeId() const = 0;
    virtual juce::String getDisplayName() const = 0;

    const std::vector<PortInfo>& getInputPorts() const  { return ins; }
    const std::vector<PortInfo>& getOutputPorts() const { return outs; }

    virtual bool hasModuleEditor() const { return false; }  // opens a separate editor window
    virtual float getMeterLevel() const  { return -1.0f; }  // >= 0 shows a meter on the node

    // Anything beyond plain parameters (note data, patterns) is saved here.
    virtual void saveExtra (juce::ValueTree&) const {}
    virtual void loadExtra (const juce::ValueTree&) {}

    void setTransport (Transport* t) { transport = t; }

    // ---- AudioProcessor boilerplate
    const juce::String getName() const override { return getDisplayName(); }
    bool acceptsMidi() const override  { return hasPort (ins, PortType::Note); }
    bool producesMidi() const override { return hasPort (outs, PortType::Note); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }

protected:
    juce::AudioParameterFloat* addFloat (const juce::String& id, const juce::String& name,
                                         juce::NormalisableRange<float> range, float def, const juce::String& unit = {})
    {
        auto* p = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, name, range, def,
                                                 juce::AudioParameterFloatAttributes().withLabel (unit));
        addParameter (p);
        return p;
    }

    juce::AudioParameterChoice* addChoice (const juce::String& id, const juce::String& name,
                                           const juce::StringArray& choices, int def)
    {
        auto* p = new juce::AudioParameterChoice (juce::ParameterID { id, 1 }, name, choices, def);
        addParameter (p);
        return p;
    }

    // Logarithmic-feeling range, good for frequencies and times.
    static juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    Transport* transport = nullptr;

private:
    static bool hasPort (const std::vector<PortInfo>& ports, PortType t)
    {
        for (auto& p : ports)
            if (p.type == t)
                return true;
        return false;
    }

    static int channelsFor (const std::vector<PortInfo>& ports)
    {
        int n = 0;
        for (auto& p : ports)
            n = std::max (n, p.channel + portWidth (p.type));
        return n;
    }

    static BusesProperties makeBuses (const std::vector<PortInfo>& in, const std::vector<PortInfo>& out, int hiddenOut)
    {
        BusesProperties b;
        if (const int n = channelsFor (in); n > 0)
            b = b.withInput ("In", juce::AudioChannelSet::discreteChannels (n), true);
        if (const int n = std::max (channelsFor (out), hiddenOut); n > 0)
            b = b.withOutput ("Out", juce::AudioChannelSet::discreteChannels (n), true);
        return b;
    }

    std::vector<PortInfo> ins, outs;
};
