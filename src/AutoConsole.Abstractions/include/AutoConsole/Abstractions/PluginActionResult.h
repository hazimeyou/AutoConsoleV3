#pragma once

#include <string>
#include <unordered_map>

namespace AutoConsole::Abstractions
{
    struct PluginActionResult
    {
        bool success = false;
        std::string message;
        std::unordered_map<std::string, std::string> data;
    };
}
