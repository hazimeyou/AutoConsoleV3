#pragma once

#include <optional>
#include <string>

#include "AutoConsole/Abstractions/Profile.h"

namespace AutoConsole::Core
{
    class ProfileLoader
    {
    public:
        static std::optional<AutoConsole::Abstractions::Profile> load_from_file(
            const std::string& filePath,
            std::string& errorMessage);
    };
}
