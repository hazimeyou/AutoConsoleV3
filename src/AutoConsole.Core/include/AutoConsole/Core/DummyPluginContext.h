#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Core
{
    class DummyPluginContext final : public AutoConsole::Abstractions::PluginContext
    {
    public:
<<<<<<< HEAD
        DummyPluginContext(
            class EventDispatcher& eventDispatcher,
            class ProcessRunner& processRunner,
            class SessionManager& sessionManager);
=======
        using SendInputFn = std::function<bool(const std::string&, const std::string&, std::string&)>;
        using StopSessionFn = std::function<bool(const std::string&, std::string&)>;
        using WaitOutputFn = std::function<bool(const std::string&, const std::string&, int, std::string&)>;
        using CallPluginActionFn = std::function<bool(const std::string&, const std::string&, const std::unordered_map<std::string, std::string>&, std::string&)>;
        using LogSinkFn = std::function<void(const std::string&, const std::string&)>;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419

        DummyPluginContext(
            class EventDispatcher& eventDispatcher,
            SendInputFn sendInputFn,
            StopSessionFn stopSessionFn,
            WaitOutputFn waitOutputFn,
            CallPluginActionFn callPluginActionFn,
            LogSinkFn logSinkFn);

        bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage) override;
        bool stop_session(const std::string& sessionId, std::string& errorMessage) override;
        bool wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage) override;
        bool call_plugin_action(
            const std::string& pluginId,
            const std::string& action,
            const std::unordered_map<std::string, std::string>& actionArgs,
            std::string& errorMessage) override;
        void emit_event(const AutoConsole::Abstractions::Event& eventValue) override;
        void log(const std::string& level, const std::string& message) override;

    private:
        EventDispatcher& eventDispatcher_;
<<<<<<< HEAD
        ProcessRunner& processRunner_;
        SessionManager& sessionManager_;
=======
        SendInputFn sendInputFn_;
        StopSessionFn stopSessionFn_;
        WaitOutputFn waitOutputFn_;
        CallPluginActionFn callPluginActionFn_;
        LogSinkFn logSinkFn_;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    };
}
