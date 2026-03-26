#include "AutoConsole/Core/PluginHost.h"

<<<<<<< HEAD
#include <iostream>
=======
#include <exception>
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419

namespace AutoConsole::Core
{
    bool PluginHost::register_plugin(
        std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
<<<<<<< HEAD
        PluginSource source,
=======
        const std::string& source,
        const std::string& location,
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
        std::string& errorMessage)
    {
        if (!plugin)
        {
            errorMessage = "plugin instance is null";
            return false;
        }

<<<<<<< HEAD
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
=======
        AutoConsole::Abstractions::PluginMetadata metadata{};
        try
        {
            metadata = plugin->metadata();
        }
        catch (...)
        {
            errorMessage = "plugin metadata() threw an exception";
            return false;
        }
        if (metadata.id.empty())
        {
            errorMessage = "plugin metadata.id is empty";
            return false;
        }

        if (metadata.displayName.empty())
        {
            metadata.displayName = metadata.name;
        }

        if (metadata.name.empty())
        {
            metadata.name = metadata.displayName;
        }

        for (const auto& existing : plugins_)
        {
            if (existing.info.metadata.id == metadata.id)
            {
                errorMessage = "duplicate plugin id: " + metadata.id;
                return false;
            }
        }

        plugins_.push_back(PluginRecord{
            std::move(plugin),
            LoadedPluginInfo{
                std::move(metadata),
                source,
                location
            }
            });
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
        return true;
    }

    void PluginHost::dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const
    {
<<<<<<< HEAD
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
=======
        for (const auto& record : plugins_)
        {
            try
            {
                record.plugin->on_event(eventValue, context);
            }
            catch (const std::exception& ex)
            {
                context.log("error", "plugin on_event failed (" + record.info.metadata.id + "): " + ex.what());
            }
            catch (...)
            {
                context.log("error", "plugin on_event failed (" + record.info.metadata.id + "): unknown exception");
            }
        }
    }

    bool PluginHost::execute_plugin_action(
        const std::string& pluginId,
        const std::string& action,
        const std::unordered_map<std::string, std::string>& actionArgs,
        AutoConsole::Abstractions::PluginContext& context,
        std::string& errorMessage) const
    {
        for (const auto& record : plugins_)
        {
            if (record.info.metadata.id != pluginId)
            {
                continue;
            }

            auto* executor = dynamic_cast<AutoConsole::Abstractions::IPluginActionExecutor*>(record.plugin.get());
            if (!executor)
            {
                errorMessage = "plugin does not support actions: " + pluginId;
                return false;
            }

            try
            {
                return executor->execute_action(action, actionArgs, context, errorMessage);
            }
            catch (const std::exception& ex)
            {
                errorMessage = "plugin action threw exception: " + std::string(ex.what());
                context.log("error", "plugin action failed (" + pluginId + "): " + ex.what());
                return false;
            }
            catch (...)
            {
                errorMessage = "plugin action threw exception: unknown";
                context.log("error", "plugin action failed (" + pluginId + "): unknown exception");
                return false;
            }
        }

        errorMessage = "plugin not found: " + pluginId;
        return false;
    }

    std::vector<LoadedPluginInfo> PluginHost::list_plugins() const
    {
        std::vector<LoadedPluginInfo> list;
        list.reserve(plugins_.size());
        for (const auto& record : plugins_)
        {
            list.push_back(record.info);
        }

        return list;
    }

    std::optional<LoadedPluginInfo> PluginHost::find_plugin(const std::string& pluginId) const
    {
        for (const auto& record : plugins_)
        {
            if (record.info.metadata.id == pluginId)
            {
                return record.info;
            }
        }

        return std::nullopt;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    }
}
