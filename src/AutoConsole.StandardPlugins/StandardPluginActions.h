#pragma once

#include <string>
#include <unordered_map>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::StandardPlugins
{
    class StandardPluginActions
    {
    public:
        using ActionArgs = std::unordered_map<std::string, std::string>;

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

        static bool delay(
            AutoConsole::Abstractions::PluginContext& context,
            int durationMs,
            std::string& errorMessage);

        static bool stop_process(
            AutoConsole::Abstractions::PluginContext& context,
            const std::string& sessionId,
            std::string& errorMessage);

        static bool emit_event(
            AutoConsole::Abstractions::PluginContext& context,
            const std::string& eventType,
            const std::string& sessionId,
            const std::string& payload,
            std::string& errorMessage);

        static bool call_plugin(
            AutoConsole::Abstractions::PluginContext& context,
            const std::string& pluginId,
            const std::string& action,
            const ActionArgs& actionArgs,
            std::string& errorMessage);

        static bool execute_action(
            const std::string& action,
            const ActionArgs& actionArgs,
            AutoConsole::Abstractions::PluginContext& context,
            std::string& errorMessage);
    };
}
