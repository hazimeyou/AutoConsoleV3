#pragma once

#include <string>
#include <unordered_map>

#include "AutoConsole/Abstractions/PluginActionResult.h"
#include "AutoConsole/Abstractions/PluginContext.h"

namespace AutoConsole::Abstractions
{
    class IActionPlugin
    {
    public:
        virtual ~IActionPlugin() = default;

        virtual PluginActionResult execute_action(
            const std::string& action,
            const std::unordered_map<std::string, std::string>& args,
            PluginContext& context) = 0;
    };
}
