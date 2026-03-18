#pragma once

#include <string>

#include "AutoConsole/Abstractions/Event.h"

namespace AutoConsole::Abstractions
{
    class PluginContext
    {
    public:
        virtual ~PluginContext() = default;

        virtual bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage) = 0;
        virtual bool stop_session(const std::string& sessionId, std::string& errorMessage) = 0;
        virtual bool wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage) = 0;
        virtual void emit_event(const Event& eventValue) = 0;
        virtual void log(const std::string& level, const std::string& message) = 0;
    };
}
