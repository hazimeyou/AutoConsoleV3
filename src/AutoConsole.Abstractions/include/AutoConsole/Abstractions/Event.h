#pragma once

#include <string>
#include <unordered_map>

namespace AutoConsole::Abstractions
{
    struct Event
    {
        std::string type;
        std::string sessionId;
        std::unordered_map<std::string, std::string> data;
    };
}
