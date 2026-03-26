#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "AutoConsole/Abstractions/Profile.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "ApiLogBuffer.h"

namespace AutoConsole::Cli
{
    class ApiServer
    {
    public:
        using ProfileResolver = std::function<std::optional<AutoConsole::Abstractions::Profile>(const std::string&, std::string&)>;

        ApiServer(
            AutoConsole::Core::CoreRuntime& runtime,
            ProfileResolver profileResolver,
            ApiLogBuffer* logBuffer);

        ~ApiServer();

        bool start(int port, std::string& errorMessage);
        void stop();

    private:
        void run_loop();

        AutoConsole::Core::CoreRuntime& runtime_;
        ProfileResolver profileResolver_;
        ApiLogBuffer* logBuffer_ = nullptr;
        std::thread serverThread_;
        void* listenSocket_ = nullptr;
        std::atomic<bool> running_{ false };
    };
}
