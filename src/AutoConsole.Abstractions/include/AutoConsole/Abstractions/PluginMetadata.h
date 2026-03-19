#pragma once

#include <string>
#include <vector>

#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace AutoConsole::Abstractions
{
    struct PluginMetadata
    {
        std::string id;
        std::string name;
        std::string displayName;
        std::string version;
        std::string apiVersion = kPluginApiVersion;
        std::string author;
        std::string description;
        std::vector<std::string> capabilities;
    };
}
