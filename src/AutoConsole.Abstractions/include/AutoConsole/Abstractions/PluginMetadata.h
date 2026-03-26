#pragma once

#include <string>
#include <vector>

#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace AutoConsole::Abstractions
{
    struct PluginMetadata
    {
        std::string id;
        std::string displayName;
        std::string version;
        int apiVersion = 1;
        std::string author;
        std::string description;
        std::vector<std::string> capabilities;
    };
}
