#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AutoConsole/Abstractions/Profile.h"
#include "AutoConsole/Abstractions/SessionInfo.h"

namespace AutoConsole::Core
{
    class SessionManager
    {
    public:
        AutoConsole::Abstractions::SessionInfo create_session(const AutoConsole::Abstractions::Profile& profile);
        std::optional<AutoConsole::Abstractions::SessionInfo> get_session(const std::string& sessionId) const;
        bool set_state(const std::string& sessionId, AutoConsole::Abstractions::SessionState state);
        bool set_exit_code(const std::string& sessionId, int exitCode);
        std::vector<AutoConsole::Abstractions::SessionInfo> list_sessions() const;

    private:
        std::unordered_map<std::string, AutoConsole::Abstractions::SessionInfo> sessions_;
        int nextSessionNumber_ = 1;
    };
}
