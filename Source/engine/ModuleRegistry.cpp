#include "ModuleRegistry.h"
#include "../modules/PianoRollModule.h"
#include "../modules/DrumSequencerModule.h"
#include "../modules/SynthModule.h"
#include "../modules/DrumMachineModule.h"
#include "../modules/EffectModules.h"

template <typename T>
static std::function<std::unique_ptr<ModuleProcessor>()> make()
{
    return [] { return std::make_unique<T>(); };
}

const std::vector<ModuleType>& getModuleTypes()
{
    static const std::vector<ModuleType> types {
        { "pianoroll",  "Piano Roll",     "Sequencers",  make<PianoRollModule>() },
        { "drumseq",    "Drum Sequencer", "Sequencers",  make<DrumSequencerModule>() },
        { "synth",      "Synth",          "Instruments", make<SynthModule>() },
        { "drums",      "Drum Machine",   "Instruments", make<DrumMachineModule>() },
        { "filter",     "Filter",         "Effects",     make<FilterModule>() },
        { "distortion", "Distortion",     "Effects",     make<DistortionModule>() },
        { "delay",      "Delay",          "Effects",     make<DelayModule>() },
        { "reverb",     "Reverb",         "Effects",     make<ReverbModule>() },
        { "lfo",        "LFO",            "Modulators",  make<LfoModule>() },
        { "output",     "Output",         "Utility",     make<OutputModule>() },
    };
    return types;
}

const ModuleType* findModuleType (const juce::String& id)
{
    for (auto& t : getModuleTypes())
        if (t.id == id)
            return &t;
    return nullptr;
}

std::unique_ptr<ModuleProcessor> createModule (const juce::String& id)
{
    if (auto* t = findModuleType (id))
        return t->create();
    return nullptr;
}
