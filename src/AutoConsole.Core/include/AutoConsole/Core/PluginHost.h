#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    struct LoadedPluginInfo
    {
        AutoConsole::Abstractions::PluginMetadata metadata;
        std::string source;
        std::string location;
    };

    class PluginHost
    {
    public:
        bool register_plugin(
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
            const std::string& source,
            const std::string& location,
            std::string& errorMessage);
        void dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const;
        std::vector<LoadedPluginInfo> list_plugins() const;
        std::optional<LoadedPluginInfo> find_plugin(const std::string& pluginId) const;

    private:
        struct PluginRecord
        {
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin;
            LoadedPluginInfo info;
        };

        std::vector<PluginRecord> plugins_;
    };
}
