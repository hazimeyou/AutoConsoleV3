#include <iostream>
#include <memory>
#include <string>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "LogPlugin.h"

int main()
{
    AutoConsole::Core::CoreRuntime runtime;
    runtime.register_plugin(std::make_shared<AutoConsole::StandardPlugins::LogPlugin>());

    AutoConsole::Abstractions::Event startupEvent{};
    startupEvent.type = "manual_trigger";
    startupEvent.sessionId = "bootstrap";
    runtime.publish_event(startupEvent);

    std::cout << "AutoConsole v3 started\n";
    std::cout << "Type 'help' for commands.\n";

    std::string line;
    while (true)
    {
        std::cout << "> ";
        if (!std::getline(std::cin, line))
        {
            break;
        }

        if (line == "exit")
        {
            break;
        }

        if (line == "help")
        {
            std::cout << "Available commands: help, ping, exit\n";
            continue;
        }

        if (line == "ping")
        {
            std::cout << "pong\n";
            continue;
        }

        std::cout << "unknown command\n";
    }

    return 0;
}
