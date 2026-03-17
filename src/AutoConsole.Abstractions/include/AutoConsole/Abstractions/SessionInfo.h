#pragma once

#include <string>

#include "AutoConsole/Abstractions/SessionState.h"

namespace AutoConsole::Abstractions
{
    struct SessionInfo
    {
        std::string id;
        std::string profileId;
        std::string profileName;
        SessionState state = SessionState::Created;
    };
}
