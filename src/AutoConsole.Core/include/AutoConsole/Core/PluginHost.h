#pragma once

#include <memory>
<<<<<<< HEAD
=======
#include <optional>
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
#include <string>
#include <unordered_map>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IActionPlugin.h"
#include "AutoConsole/Abstractions/IPlugin.h"
<<<<<<< HEAD
#include "AutoConsole/Abstractions/PluginActionResult.h"
=======
#include "AutoConsole/Abstractions/IPluginActionExecutor.h"
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
<<<<<<< HEAD
    enum class PluginSource
    {
        Standard,
        External
    };

    struct PluginInfo
    {
        AutoConsole::Abstractions::PluginMetadata metadata;
        PluginSource source = PluginSource::Standard;
=======
    struct LoadedPluginInfo
    {
        AutoConsole::Abstractions::PluginMetadata metadata;
        std::string source;
        std::string location;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    };

    class PluginHost
    {
    public:
        bool register_plugin(
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
<<<<<<< HEAD
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
=======
            const std::string& source,
            const std::string& location,
            std::string& errorMessage);
        void dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const;
        bool execute_plugin_action(
            const std::string& pluginId,
            const std::string& action,
            const std::unordered_map<std::string, std::string>& actionArgs,
            AutoConsole::Abstractions::PluginContext& context,
            std::string& errorMessage) const;
        std::vector<LoadedPluginInfo> list_plugins() const;
        std::optional<LoadedPluginInfo> find_plugin(const std::string& pluginId) const;

    private:
        struct PluginRecord
        {
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin;
            LoadedPluginInfo info;
        };

        std::vector<PluginRecord> plugins_;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    };
}
