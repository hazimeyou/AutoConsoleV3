#include "AutoConsole/Core/DummyPluginContext.h"

#include <iostream>

#include "AutoConsole/Core/EventDispatcher.h"

namespace AutoConsole::Core
{
    DummyPluginContext::DummyPluginContext(EventDispatcher& eventDispatcher)
        : eventDispatcher_(eventDispatcher)
    {
    }

    void DummyPluginContext::send_input(const std::string& sessionId, const std::string& text)
    {
        (void)sessionId;
        (void)text;
    }

    void DummyPluginContext::stop_session(const std::string& sessionId)
    {
        (void)sessionId;
    }

    void DummyPluginContext::emit_event(const AutoConsole::Abstractions::Event& eventValue)
    {
        eventDispatcher_.publish(eventValue);
    }

    void DummyPluginContext::log(const std::string& level, const std::string& message)
    {
        std::cout << "[" << level << "] " << message << '\n';
    }
}
