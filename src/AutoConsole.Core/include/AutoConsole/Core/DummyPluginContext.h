#pragma once

#include <string>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    class DummyPluginContext final : public AutoConsole::Abstractions::PluginContext
    {
    public:
        explicit DummyPluginContext(class EventDispatcher& eventDispatcher);

        void send_input(const std::string& sessionId, const std::string& text) override;
        void stop_session(const std::string& sessionId) override;
        void emit_event(const AutoConsole::Abstractions::Event& eventValue) override;
        void log(const std::string& level, const std::string& message) override;

    private:
        EventDispatcher& eventDispatcher_;
    };
}
