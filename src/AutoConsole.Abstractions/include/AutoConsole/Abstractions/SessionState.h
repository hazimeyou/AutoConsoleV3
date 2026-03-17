#pragma once

namespace AutoConsole::Abstractions
{
    enum class SessionState
    {
        Created,
        Starting,
        Running,
        Stopped,
        Failed
    };
}
