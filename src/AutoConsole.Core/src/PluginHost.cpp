#include "AutoConsole/Core/PluginHost.h"

#include <exception>

namespace AutoConsole::Core
{
    bool PluginHost::register_plugin(
        std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
        const std::string& source,
        const std::string& location,
        std::string& errorMessage)
    {
        if (!plugin)
        {
            errorMessage = "plugin instance is null";
            return false;
        }

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
        return true;
    }

    void PluginHost::dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const
    {
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
    }
}
