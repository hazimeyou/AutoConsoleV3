#include "AutoConsole/Core/CoreRuntime.h"

namespace AutoConsole::Core
{
    CoreRuntime::CoreRuntime()
        : pluginContext_(eventDispatcher_)
    {
        eventDispatcher_.subscribe([this](const AutoConsole::Abstractions::Event& eventValue)
        {
            pluginHost_.dispatch_event(eventValue, pluginContext_);
        });
    }

    void CoreRuntime::register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin)
    {
        pluginHost_.register_plugin(plugin);
    }

    void CoreRuntime::publish_event(const AutoConsole::Abstractions::Event& eventValue)
    {
        eventDispatcher_.publish(eventValue);
    }

    AutoConsole::Abstractions::PluginContext& CoreRuntime::plugin_context()
    {
        return pluginContext_;
    }

    StartSessionResult CoreRuntime::start_session(const AutoConsole::Abstractions::Profile& profile)
    {
        StartSessionResult result{};
        result.session = sessionManager_.create_session(profile);

        sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Starting);
        result.session.state = AutoConsole::Abstractions::SessionState::Starting;

        std::string errorMessage;
        const bool started = processRunner_.start(
            result.session.id,
            profile,
            [this](const std::string& sessionId, int exitCode)
            {
                sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);

                AutoConsole::Abstractions::Event exitEvent{};
                exitEvent.type = "process_exited";
                exitEvent.sessionId = sessionId;
                exitEvent.data["exitCode"] = std::to_string(exitCode);
                eventDispatcher_.publish(exitEvent);
            },
            errorMessage);

        if (!started)
        {
            sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Failed);
            result.session.state = AutoConsole::Abstractions::SessionState::Failed;
            result.started = false;
            result.errorMessage = errorMessage;
            return result;
        }

        sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Running);
        result.session.state = AutoConsole::Abstractions::SessionState::Running;
        result.started = true;

        AutoConsole::Abstractions::Event startedEvent{};
        startedEvent.type = "process_started";
        startedEvent.sessionId = result.session.id;
        eventDispatcher_.publish(startedEvent);

        return result;
    }

    bool CoreRuntime::stop_session(const std::string& sessionId)
    {
        const bool stopped = processRunner_.stop(sessionId);
        if (stopped)
        {
            sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
        }

        return stopped;
    }

    std::vector<AutoConsole::Abstractions::SessionInfo> CoreRuntime::sessions() const
    {
        return sessionManager_.list_sessions();
    }
}
