#pragma once

#include <string>

#include "AutoConsole/Abstractions/SessionState.h"

namespace AutoConsole::Abstractions
{
    struct SessionInfo
    {
        std::string id;
        std::string profileId;
        SessionState state = SessionState::Created;
    };
}
