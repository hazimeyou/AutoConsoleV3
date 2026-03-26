#pragma once

#include "AutoConsole/Abstractions/IPlugin.h"

namespace AutoConsole::Abstractions
{
    constexpr int PluginApiVersion = 1;

    using CreatePluginFn = AutoConsole::Abstractions::IPlugin* (*)();
    using DestroyPluginFn = void (*)(AutoConsole::Abstractions::IPlugin*);
    using GetPluginApiVersionFn = int (*)();
}
