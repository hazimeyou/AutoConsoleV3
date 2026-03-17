#pragma once

#include <string>
#include <vector>

namespace AutoConsole::Abstractions
{
    struct Profile
    {
        std::string id;
        std::string name;
        std::string command;
        std::vector<std::string> args;
        std::string workingDirectory;
    };
}
