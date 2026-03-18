#include "AutoConsole/Core/DummyPluginContext.h"

#include <iostream>
#include <utility>

#include "AutoConsole/Core/EventDispatcher.h"

namespace AutoConsole::Core
{
    DummyPluginContext::DummyPluginContext(
        EventDispatcher& eventDispatcher,
        SendInputFn sendInputFn,
        StopSessionFn stopSessionFn,
        WaitOutputFn waitOutputFn)
        : eventDispatcher_(eventDispatcher),
        sendInputFn_(std::move(sendInputFn)),
        stopSessionFn_(std::move(stopSessionFn)),
        waitOutputFn_(std::move(waitOutputFn))
    {
    }

    bool DummyPluginContext::send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage)
    {
        if (!sendInputFn_)
        {
            errorMessage = "send_input is not configured";
            return false;
        }

        return sendInputFn_(sessionId, text, errorMessage);
    }

    bool DummyPluginContext::stop_session(const std::string& sessionId, std::string& errorMessage)
    {
        if (!stopSessionFn_)
        {
            errorMessage = "stop_session is not configured";
            return false;
        }

        return stopSessionFn_(sessionId, errorMessage);
    }

    bool DummyPluginContext::wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage)
    {
        if (!waitOutputFn_)
        {
            errorMessage = "wait_output is not configured";
            return false;
        }

        return waitOutputFn_(sessionId, contains, timeoutMs, errorMessage);
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
