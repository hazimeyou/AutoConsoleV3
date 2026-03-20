#pragma once

#include <string>
#include <unordered_map>

#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Abstractions
{
    class IPluginActionExecutor
    {
    public:
        virtual ~IPluginActionExecutor() = default;

        virtual bool execute_action(
            const std::string& action,
            const std::unordered_map<std::string, std::string>& actionArgs,
            PluginContext& context,
            std::string& errorMessage) = 0;
    };
}
