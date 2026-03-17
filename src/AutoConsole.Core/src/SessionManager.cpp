#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    AutoConsole::Abstractions::SessionInfo SessionManager::create_session(const AutoConsole::Abstractions::Profile& profile)
    {
        AutoConsole::Abstractions::SessionInfo session{};
        session.id = "session-" + std::to_string(nextSessionNumber_++);
        session.profileId = profile.id;
        session.profileName = profile.name;
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

    bool SessionManager::set_state(const std::string& sessionId, AutoConsole::Abstractions::SessionState state)
    {
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            return false;
        }

        it->second.state = state;
        return true;
    }

    std::vector<AutoConsole::Abstractions::SessionInfo> SessionManager::list_sessions() const
    {
        std::vector<AutoConsole::Abstractions::SessionInfo> result;
        result.reserve(sessions_.size());

        for (const auto& kv : sessions_)
        {
            result.push_back(kv.second);
        }

        return result;
    }
}
