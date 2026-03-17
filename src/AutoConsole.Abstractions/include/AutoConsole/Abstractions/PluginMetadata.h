#pragma once

#include <string>

namespace AutoConsole::Abstractions
{
    struct PluginMetadata
    {
        std::string id;
        std::string name;
        std::string version;
    };
}
