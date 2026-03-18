#include "StandardPluginActions.h"

namespace AutoConsole::StandardPlugins
{
    bool StandardPluginActions::send_input(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& sessionId,
        const std::string& text,
        std::string& errorMessage)
    {
        return context.send_input(sessionId, text, errorMessage);
    }

    bool StandardPluginActions::wait_output(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& sessionId,
        const std::string& contains,
        int timeoutMs,
        std::string& errorMessage)
    {
        return context.wait_output(sessionId, contains, timeoutMs, errorMessage);
    }
}
