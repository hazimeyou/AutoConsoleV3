#pragma once

#include <string>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::StandardPlugins
{
    class StandardPluginActions
    {
    public:
        static bool send_input(
            AutoConsole::Abstractions::PluginContext& context,
            const std::string& sessionId,
            const std::string& text,
            std::string& errorMessage);

        static bool wait_output(
            AutoConsole::Abstractions::PluginContext& context,
            const std::string& sessionId,
            const std::string& contains,
            int timeoutMs,
            std::string& errorMessage);
    };
}
