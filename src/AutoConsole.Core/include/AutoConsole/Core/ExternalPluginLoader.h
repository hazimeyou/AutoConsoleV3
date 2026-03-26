#pragma once

#include <string>
#include <vector>

namespace AutoConsole::Core
{
    class PluginHost;

    class ExternalPluginLoader
    {
    public:
        std::vector<std::string> load_plugins(const std::string& directory, PluginHost& pluginHost) const;
    };
}
