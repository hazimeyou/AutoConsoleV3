#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/Profile.h"
#include "AutoConsole/Abstractions/SessionInfo.h"
#include "AutoConsole/Core/DummyPluginContext.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/PluginHost.h"
#include "AutoConsole/Core/ProcessRunner.h"
#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    struct StartSessionResult
    {
        AutoConsole::Abstractions::SessionInfo session;
        bool started = false;
        std::string errorMessage;
    };

    class CoreRuntime
    {
    public:
        CoreRuntime();

        void register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin);
        void subscribe_events(EventDispatcher::Handler handler);
        void publish_event(const AutoConsole::Abstractions::Event& eventValue);
        AutoConsole::Abstractions::PluginContext& plugin_context();
        StartSessionResult start_session(const AutoConsole::Abstractions::Profile& profile);
        bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage);
        bool stop_session(const std::string& sessionId, std::string& errorMessage);
        bool wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage);
        std::vector<AutoConsole::Abstractions::SessionInfo> sessions();

    private:
        struct OutputRecord
        {
            std::uint64_t sequence = 0;
            std::string sessionId;
            std::string text;
        };

        void store_output_record(const std::string& sessionId, const std::string& text);

        ProcessRunner processRunner_;
        SessionManager sessionManager_;
        EventDispatcher eventDispatcher_;
        PluginHost pluginHost_;
        DummyPluginContext pluginContext_;
        mutable std::mutex outputMutex_;
        std::condition_variable outputCv_;
        std::deque<OutputRecord> outputRecords_;
        std::uint64_t outputSequence_ = 0;
    };
}
