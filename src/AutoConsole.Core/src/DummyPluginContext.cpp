#include "AutoConsole/Core/DummyPluginContext.h"

#include <iostream>
#include <utility>

#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/ProcessRunner.h"
#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    DummyPluginContext::DummyPluginContext(
        EventDispatcher& eventDispatcher,
<<<<<<< HEAD
        ProcessRunner& processRunner,
        SessionManager& sessionManager)
        : eventDispatcher_(eventDispatcher),
          processRunner_(processRunner),
          sessionManager_(sessionManager)
=======
        SendInputFn sendInputFn,
        StopSessionFn stopSessionFn,
        WaitOutputFn waitOutputFn,
        CallPluginActionFn callPluginActionFn,
        LogSinkFn logSinkFn)
        : eventDispatcher_(eventDispatcher),
        sendInputFn_(std::move(sendInputFn)),
        stopSessionFn_(std::move(stopSessionFn)),
        waitOutputFn_(std::move(waitOutputFn)),
        callPluginActionFn_(std::move(callPluginActionFn)),
        logSinkFn_(std::move(logSinkFn))
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    {
    }

    bool DummyPluginContext::send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage)
    {
<<<<<<< HEAD
        std::string errorMessage;
        (void)processRunner_.send_input(sessionId, text, errorMessage);
=======
        if (!sendInputFn_)
        {
            errorMessage = "send_input is not configured";
            return false;
        }

        return sendInputFn_(sessionId, text, errorMessage);
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    }

    bool DummyPluginContext::stop_session(const std::string& sessionId, std::string& errorMessage)
    {
<<<<<<< HEAD
        if (processRunner_.stop(sessionId))
        {
            sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
        }
=======
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

    bool DummyPluginContext::call_plugin_action(
        const std::string& pluginId,
        const std::string& action,
        const std::unordered_map<std::string, std::string>& actionArgs,
        std::string& errorMessage)
    {
        if (!callPluginActionFn_)
        {
            errorMessage = "call_plugin_action is not configured";
            return false;
        }

        return callPluginActionFn_(pluginId, action, actionArgs, errorMessage);
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    }

    void DummyPluginContext::emit_event(const AutoConsole::Abstractions::Event& eventValue)
    {
        eventDispatcher_.publish(eventValue);
    }

    void DummyPluginContext::log(const std::string& level, const std::string& message)
    {
        if (logSinkFn_)
        {
            logSinkFn_(level, message);
            return;
        }

        std::cout << "[" << level << "] " << message << '\n';
    }
}
