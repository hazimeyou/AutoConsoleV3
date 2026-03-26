#include "AutoConsole/Core/PluginHost.h"

#include <iostream>

namespace AutoConsole::Core
{
    bool PluginHost::register_plugin(
        std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
        PluginSource source,
        std::string& errorMessage)
    {
        if (!plugin)
        {
            errorMessage = "plugin instance is null";
            return false;
        }

        const auto metadata = plugin->metadata();
        if (metadata.id.empty())
        {
            errorMessage = "plugin metadata id is empty";
            return false;
        }

        if (plugins_.find(metadata.id) != plugins_.end())
        {
            errorMessage = "duplicate plugin id: " + metadata.id;
            return false;
        }

        PluginEntry entry{};
        entry.plugin = plugin;
        entry.info.metadata = metadata;
        entry.info.source = source;
        plugins_.emplace(metadata.id, entry);
        std::cout << "[plugin-host] registered plugin: " << metadata.id << "\n";
        return true;
    }

    void PluginHost::dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const
    {
        for (const auto& kv : plugins_)
        {
            kv.second.plugin->on_event(eventValue, context);
        }
    }

    std::vector<PluginInfo> PluginHost::list_plugins() const
    {
        std::vector<PluginInfo> result;
        result.reserve(plugins_.size());
        for (const auto& kv : plugins_)
        {
            result.push_back(kv.second.info);
        }
        return result;
    }

    std::shared_ptr<AutoConsole::Abstractions::IPlugin> PluginHost::find_plugin(const std::string& pluginId) const
    {
        const auto it = plugins_.find(pluginId);
        if (it == plugins_.end())
        {
            return nullptr;
        }

        return it->second.plugin;
    }

    bool PluginHost::execute_action(
        const std::string& pluginId,
        const std::string& action,
        const std::unordered_map<std::string, std::string>& args,
        AutoConsole::Abstractions::PluginContext& context,
        AutoConsole::Abstractions::PluginActionResult& result,
        std::string& errorMessage) const
    {
        const auto plugin = find_plugin(pluginId);
        if (!plugin)
        {
            errorMessage = "plugin not found";
            return false;
        }

        auto actionPlugin = dynamic_cast<AutoConsole::Abstractions::IActionPlugin*>(plugin.get());
        if (!actionPlugin)
        {
            errorMessage = "plugin does not support actions";
            return false;
        }

        result = actionPlugin->execute_action(action, args, context);
        if (!result.success)
        {
            if (result.message.empty())
            {
                result.message = "plugin action failed";
            }
            errorMessage = result.message;
        }

        return result.success;
    }
}
