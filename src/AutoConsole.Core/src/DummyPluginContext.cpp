#include "AutoConsole/Core/DummyPluginContext.h"

#include <iostream>

#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/ProcessRunner.h"
#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    DummyPluginContext::DummyPluginContext(
        EventDispatcher& eventDispatcher,
        ProcessRunner& processRunner,
        SessionManager& sessionManager)
        : eventDispatcher_(eventDispatcher),
          processRunner_(processRunner),
          sessionManager_(sessionManager)
    {
    }

    void DummyPluginContext::send_input(const std::string& sessionId, const std::string& text)
    {
        std::string errorMessage;
        (void)processRunner_.send_input(sessionId, text, errorMessage);
    }

    void DummyPluginContext::stop_session(const std::string& sessionId)
    {
        if (processRunner_.stop(sessionId))
        {
            sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
        }
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
