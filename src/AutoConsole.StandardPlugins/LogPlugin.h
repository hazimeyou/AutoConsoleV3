#pragma once

#include "AutoConsole/Abstractions/IPlugin.h"

namespace AutoConsole::StandardPlugins
{
    class LogPlugin final : public AutoConsole::Abstractions::IPlugin
    {
    public:
        AutoConsole::Abstractions::PluginMetadata metadata() const override;
        void on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) override;
    };
}
