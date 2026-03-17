#include "AutoConsole/Core/ProcessRunner.h"

namespace AutoConsole::Core
{
    bool ProcessRunner::start(const std::string& sessionId)
    {
        (void)sessionId;
        return true;
    }

    void ProcessRunner::stop(const std::string& sessionId)
    {
        (void)sessionId;
    }
}
