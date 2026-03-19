#include "AutoConsole/Core/PluginHost.h"

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
            record.plugin->on_event(eventValue, context);
        }
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
