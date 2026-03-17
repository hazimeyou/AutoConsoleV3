#pragma once

#include <string>

namespace AutoConsole::Core
{
    class ProcessRunner
    {
    public:
        bool start(const std::string& sessionId);
        void stop(const std::string& sessionId);
    };
}
