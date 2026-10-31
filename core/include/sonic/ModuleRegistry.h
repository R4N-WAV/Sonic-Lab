#pragma once

#include "sonic/Module.h"

#include <memory>
#include <string>
#include <vector>

namespace sonic
{

struct ModuleInfo
{
    std::string typeId;
    std::string displayName;
    std::string category;   // "Character", "Dynamics", "Tone", "Space", "Modulation", "Utility"
    std::string summary;    // one plain-language sentence for the palette tooltip
};

const std::vector<ModuleInfo>& availableModules();
std::unique_ptr<Module> createModule (const std::string& typeId);

} // namespace sonic
