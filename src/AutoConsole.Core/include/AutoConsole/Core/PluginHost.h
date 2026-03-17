#pragma once

#include <memory>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    class PluginHost
    {
    public:
        void register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin);
        void dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const;

    private:
        std::vector<std::shared_ptr<AutoConsole::Abstractions::IPlugin>> plugins_;
    };
}
