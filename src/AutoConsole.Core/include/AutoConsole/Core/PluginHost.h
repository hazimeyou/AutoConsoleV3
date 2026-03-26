#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IActionPlugin.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/PluginActionResult.h"
#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    enum class PluginSource
    {
        Standard,
        External
    };

    struct PluginInfo
    {
        AutoConsole::Abstractions::PluginMetadata metadata;
        PluginSource source = PluginSource::Standard;
    };

    class PluginHost
    {
    public:
        bool register_plugin(
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
            PluginSource source,
            std::string& errorMessage);
        void dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const;
        std::vector<PluginInfo> list_plugins() const;
        std::shared_ptr<AutoConsole::Abstractions::IPlugin> find_plugin(const std::string& pluginId) const;
        bool execute_action(
            const std::string& pluginId,
            const std::string& action,
            const std::unordered_map<std::string, std::string>& args,
            AutoConsole::Abstractions::PluginContext& context,
            AutoConsole::Abstractions::PluginActionResult& result,
            std::string& errorMessage) const;

    private:
        struct PluginEntry
        {
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin;
            PluginInfo info;
        };

        std::unordered_map<std::string, PluginEntry> plugins_;
    };
}
