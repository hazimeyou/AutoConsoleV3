#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    AutoConsole::Abstractions::SessionInfo SessionManager::create_session(const AutoConsole::Abstractions::Profile& profile)
    {
        AutoConsole::Abstractions::SessionInfo session{};
        session.id = "session-" + std::to_string(nextSessionNumber_++);
        session.profileId = profile.id;
        session.state = AutoConsole::Abstractions::SessionState::Created;

        sessions_[session.id] = session;
        return session;
    }

    std::optional<AutoConsole::Abstractions::SessionInfo> SessionManager::get_session(const std::string& sessionId) const
    {
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }
}
