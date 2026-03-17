#pragma once

#include <string>

#include "AutoConsole/Abstractions/Event.h"

namespace AutoConsole::Abstractions
{
    class PluginContext
    {
    public:
        virtual ~PluginContext() = default;

        virtual void send_input(const std::string& sessionId, const std::string& text) = 0;
        virtual void stop_session(const std::string& sessionId) = 0;
        virtual void emit_event(const Event& eventValue) = 0;
        virtual void log(const std::string& level, const std::string& message) = 0;
    };
}
