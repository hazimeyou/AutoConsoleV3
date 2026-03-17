#pragma once

#include <string>

namespace AutoConsole::Abstractions
{
    struct JobInfo
    {
        std::string id;
        std::string name;
        std::string status;
    };
}
