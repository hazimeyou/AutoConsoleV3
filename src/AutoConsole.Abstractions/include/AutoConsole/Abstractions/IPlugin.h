#pragma once

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/PluginContext.h"
#include "AutoConsole/Abstractions/PluginMetadata.h"

namespace AutoConsole::Abstractions
{
    class IPlugin
    {
    public:
        virtual ~IPlugin() = default;

        virtual PluginMetadata metadata() const = 0;
        virtual void on_event(const Event& eventValue, PluginContext& context) = 0;
    };
}
