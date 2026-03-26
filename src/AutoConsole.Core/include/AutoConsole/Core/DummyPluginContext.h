#pragma once

#include <string>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    class DummyPluginContext final : public AutoConsole::Abstractions::PluginContext
    {
    public:
        DummyPluginContext(
            class EventDispatcher& eventDispatcher,
            class ProcessRunner& processRunner,
            class SessionManager& sessionManager);

        void send_input(const std::string& sessionId, const std::string& text) override;
        void stop_session(const std::string& sessionId) override;
        void emit_event(const AutoConsole::Abstractions::Event& eventValue) override;
        void log(const std::string& level, const std::string& message) override;

    private:
        EventDispatcher& eventDispatcher_;
        ProcessRunner& processRunner_;
        SessionManager& sessionManager_;
    };
}
