#include "AutoConsole/Core/CoreRuntime.h"

#include <chrono>

namespace
{
    std::string now_timestamp_utc()
    {
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(millis);
    }

    std::string state_to_string(AutoConsole::Abstractions::SessionState state)
    {
        switch (state)
        {
        case AutoConsole::Abstractions::SessionState::Created:
            return "Created";
        case AutoConsole::Abstractions::SessionState::Starting:
            return "Starting";
        case AutoConsole::Abstractions::SessionState::Running:
            return "Running";
        case AutoConsole::Abstractions::SessionState::Stopped:
            return "Stopped";
        case AutoConsole::Abstractions::SessionState::Failed:
            return "Failed";
        default:
            return "Unknown";
        }
    }
}

namespace AutoConsole::Core
{
    CoreRuntime::CoreRuntime()
        : pluginContext_(
            eventDispatcher_,
            [this](const std::string& sessionId, const std::string& text, std::string& errorMessage)
            {
                return send_input(sessionId, text, errorMessage);
            },
            [this](const std::string& sessionId, std::string& errorMessage)
            {
                return stop_session(sessionId, errorMessage);
            },
            [this](const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage)
            {
                return wait_output(sessionId, contains, timeoutMs, errorMessage);
            })
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

    void CoreRuntime::subscribe_events(EventDispatcher::Handler handler)
    {
        eventDispatcher_.subscribe(handler);
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
        processRunner_.cleanup_finished_sessions();

        StartSessionResult result{};
        result.session = sessionManager_.create_session(profile);

        sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Starting);
        result.session.state = AutoConsole::Abstractions::SessionState::Starting;

        std::string errorMessage;
        const bool started = processRunner_.start(
            result.session.id,
            profile,
            [this](const std::string& sessionId, const std::string& text)
            {
                store_output_record(sessionId, text);

                AutoConsole::Abstractions::Event eventValue{};
                eventValue.type = "stdout_line";
                eventValue.sessionId = sessionId;
                eventValue.data["text"] = text;
                eventValue.data["timestamp"] = now_timestamp_utc();
                eventDispatcher_.publish(eventValue);
            },
            [this](const std::string& sessionId, const std::string& text)
            {
                store_output_record(sessionId, text);

                AutoConsole::Abstractions::Event eventValue{};
                eventValue.type = "stderr_line";
                eventValue.sessionId = sessionId;
                eventValue.data["text"] = text;
                eventValue.data["timestamp"] = now_timestamp_utc();
                eventDispatcher_.publish(eventValue);
            },
            [this](const std::string& sessionId, int exitCode)
            {
                sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);

                AutoConsole::Abstractions::Event exitEvent{};
                exitEvent.type = "process_exited";
                exitEvent.sessionId = sessionId;
                exitEvent.data["exitCode"] = std::to_string(exitCode);
                exitEvent.data["timestamp"] = now_timestamp_utc();
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
        startedEvent.data["timestamp"] = now_timestamp_utc();
        eventDispatcher_.publish(startedEvent);

        return result;
    }

    bool CoreRuntime::send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (session->state != AutoConsole::Abstractions::SessionState::Running)
        {
            errorMessage = "session is not running (state=" + state_to_string(session->state) + ")";
            return false;
        }

        return processRunner_.write_input(sessionId, text, true, errorMessage);
    }

    bool CoreRuntime::stop_session(const std::string& sessionId, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (session->state != AutoConsole::Abstractions::SessionState::Running &&
            session->state != AutoConsole::Abstractions::SessionState::Starting)
        {
            errorMessage = "session is not running (state=" + state_to_string(session->state) + ")";
            return false;
        }

        const bool stopped = processRunner_.stop(sessionId);
        if (!stopped)
        {
            errorMessage = "failed to stop process";
            return false;
        }

        sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
        return true;
    }

    bool CoreRuntime::wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (contains.empty())
        {
            errorMessage = "contains must not be empty";
            return false;
        }

        if (timeoutMs < 0)
        {
            errorMessage = "timeoutMs must be >= 0";
            return false;
        }

        std::unique_lock<std::mutex> lock(outputMutex_);

        std::uint64_t lastSeen = 0;
        for (const auto& record : outputRecords_)
        {
            if (record.sessionId == sessionId && record.text.find(contains) != std::string::npos)
            {
                return true;
            }

            lastSeen = record.sequence;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true)
        {
            const bool signaled = outputCv_.wait_until(lock, deadline, [this, lastSeen]()
            {
                return !outputRecords_.empty() && outputRecords_.back().sequence > lastSeen;
            });

            if (!signaled)
            {
                errorMessage = "wait_output timed out (sessionId=" + sessionId + ", contains=\"" + contains + "\", timeoutMs=" + std::to_string(timeoutMs) + ")";
                return false;
            }

            for (const auto& record : outputRecords_)
            {
                if (record.sequence <= lastSeen)
                {
                    continue;
                }

                if (record.sessionId == sessionId && record.text.find(contains) != std::string::npos)
                {
                    return true;
                }
            }

            lastSeen = outputRecords_.back().sequence;
        }
    }

    std::vector<AutoConsole::Abstractions::SessionInfo> CoreRuntime::sessions()
    {
        processRunner_.cleanup_finished_sessions();
        return sessionManager_.list_sessions();
    }

    void CoreRuntime::store_output_record(const std::string& sessionId, const std::string& text)
    {
        {
            std::lock_guard<std::mutex> lock(outputMutex_);
            outputRecords_.push_back(OutputRecord{ ++outputSequence_, sessionId, text });
            constexpr std::size_t MaxRecords = 2048;
            if (outputRecords_.size() > MaxRecords)
            {
                outputRecords_.pop_front();
            }
        }

        outputCv_.notify_all();
    }

}
