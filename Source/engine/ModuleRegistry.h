#pragma once

#include "Module.h"
#include <functional>

// Every module type the canvas can create. Add a new module by writing its class
// and adding one line to the list in ModuleRegistry.cpp.
struct ModuleType
{
    juce::String id;
    juce::String name;
    juce::String category;
    std::function<std::unique_ptr<ModuleProcessor>()> create;
};

const std::vector<ModuleType>& getModuleTypes();
const ModuleType* findModuleType (const juce::String& id);
std::unique_ptr<ModuleProcessor> createModule (const juce::String& id);
