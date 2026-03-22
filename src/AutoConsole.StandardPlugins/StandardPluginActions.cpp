#include "StandardPluginActions.h"

#include <chrono>
#include <thread>

#include "AutoConsole/Abstractions/Event.h"

namespace
{
    std::string now_timestamp_utc()
    {
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(millis);
    }
}

namespace AutoConsole::StandardPlugins
{
    bool StandardPluginActions::send_input(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& sessionId,
        const std::string& text,
        std::string& errorMessage)
    {
        return context.send_input(sessionId, text, errorMessage);
    }

    bool StandardPluginActions::wait_output(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& sessionId,
        const std::string& contains,
        int timeoutMs,
        std::string& errorMessage)
    {
        return context.wait_output(sessionId, contains, timeoutMs, errorMessage);
    }

    bool StandardPluginActions::delay(
        AutoConsole::Abstractions::PluginContext& context,
        int durationMs,
        std::string& errorMessage)
    {
        (void)context;
        if (durationMs < 0)
        {
            errorMessage = "durationMs must be >= 0";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
        return true;
    }

    bool StandardPluginActions::timer(
        AutoConsole::Abstractions::PluginContext& context,
        int durationMs,
        std::string& errorMessage)
    {
        if (durationMs < 0)
        {
            errorMessage = "durationMs must be >= 0";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

        AutoConsole::Abstractions::Event eventValue{};
        eventValue.type = "timer";
        eventValue.data["durationMs"] = std::to_string(durationMs);
        eventValue.data["timestamp"] = now_timestamp_utc();
        context.emit_event(eventValue);
        return true;
    }

    bool StandardPluginActions::stop_process(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& sessionId,
        std::string& errorMessage)
    {
        if (sessionId.empty())
        {
            errorMessage = "sessionId is required";
            return false;
        }

        return context.stop_session(sessionId, errorMessage);
    }

    bool StandardPluginActions::emit_event(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& eventType,
        const std::string& sessionId,
        const std::string& payload,
        std::string& errorMessage)
    {
        if (eventType.empty())
        {
            errorMessage = "eventType is required";
            return false;
        }

        AutoConsole::Abstractions::Event eventValue{};
        eventValue.type = eventType;
        eventValue.sessionId = sessionId;
        if (!payload.empty())
        {
            eventValue.data["payload"] = payload;
        }

        context.emit_event(eventValue);
        return true;
    }

    bool StandardPluginActions::call_plugin(
        AutoConsole::Abstractions::PluginContext& context,
        const std::string& pluginId,
        const std::string& action,
        const ActionArgs& actionArgs,
        std::string& errorMessage)
    {
        if (pluginId.empty() || action.empty())
        {
            errorMessage = "pluginId and action are required";
            return false;
        }

        return context.call_plugin_action(pluginId, action, actionArgs, errorMessage);
    }

    bool StandardPluginActions::execute_action(
        const std::string& action,
        const ActionArgs& actionArgs,
        AutoConsole::Abstractions::PluginContext& context,
        std::string& errorMessage)
    {
        if (action == "send_input")
        {
            const auto sessionIt = actionArgs.find("sessionId");
            const auto textIt = actionArgs.find("text");
            if (sessionIt == actionArgs.end() || textIt == actionArgs.end())
            {
                errorMessage = "send_input requires sessionId and text";
                return false;
            }
            return send_input(context, sessionIt->second, textIt->second, errorMessage);
        }

        if (action == "wait_output")
        {
            const auto sessionIt = actionArgs.find("sessionId");
            const auto containsIt = actionArgs.find("contains");
            const auto timeoutIt = actionArgs.find("timeoutMs");
            if (sessionIt == actionArgs.end() || containsIt == actionArgs.end() || timeoutIt == actionArgs.end())
            {
                errorMessage = "wait_output requires sessionId, contains, and timeoutMs";
                return false;
            }

            int timeoutMs = 0;
            try
            {
                timeoutMs = std::stoi(timeoutIt->second);
            }
            catch (...)
            {
                errorMessage = "timeoutMs must be an integer";
                return false;
            }

            return wait_output(context, sessionIt->second, containsIt->second, timeoutMs, errorMessage);
        }

        if (action == "delay")
        {
            const auto durationIt = actionArgs.find("durationMs");
            if (durationIt == actionArgs.end())
            {
                errorMessage = "delay requires durationMs";
                return false;
            }

            int durationMs = 0;
            try
            {
                durationMs = std::stoi(durationIt->second);
            }
            catch (...)
            {
                errorMessage = "durationMs must be an integer";
                return false;
            }

            return delay(context, durationMs, errorMessage);
        }

        if (action == "timer")
        {
            const auto durationIt = actionArgs.find("durationMs");
            if (durationIt == actionArgs.end())
            {
                errorMessage = "timer requires durationMs";
                return false;
            }

            int durationMs = 0;
            try
            {
                durationMs = std::stoi(durationIt->second);
            }
            catch (...)
            {
                errorMessage = "durationMs must be an integer";
                return false;
            }

            return timer(context, durationMs, errorMessage);
        }

        if (action == "stop_process")
        {
            const auto sessionIt = actionArgs.find("sessionId");
            if (sessionIt == actionArgs.end())
            {
                errorMessage = "stop_process requires sessionId";
                return false;
            }

            return stop_process(context, sessionIt->second, errorMessage);
        }

        if (action == "emit_event")
        {
            const auto eventTypeIt = actionArgs.find("eventType");
            if (eventTypeIt == actionArgs.end())
            {
                errorMessage = "emit_event requires eventType";
                return false;
            }

            const auto sessionIt = actionArgs.find("sessionId");
            const auto payloadIt = actionArgs.find("payload");
            const std::string sessionId = (sessionIt != actionArgs.end()) ? sessionIt->second : "";
            const std::string payload = (payloadIt != actionArgs.end()) ? payloadIt->second : "";
            return emit_event(context, eventTypeIt->second, sessionId, payload, errorMessage);
        }

        if (action == "call_plugin")
        {
            const auto pluginIdIt = actionArgs.find("pluginId");
            const auto targetActionIt = actionArgs.find("action");
            if (pluginIdIt == actionArgs.end() || targetActionIt == actionArgs.end())
            {
                errorMessage = "call_plugin requires pluginId and action";
                return false;
            }

            ActionArgs nestedArgs;
            for (const auto& kv : actionArgs)
            {
                if (kv.first.rfind("arg.", 0) == 0)
                {
                    nestedArgs.emplace(kv.first.substr(4), kv.second);
                }
            }

            return call_plugin(context, pluginIdIt->second, targetActionIt->second, nestedArgs, errorMessage);
        }

        errorMessage = "unknown standard action: " + action;
        return false;
    }
}
