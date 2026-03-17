#pragma once

#include <functional>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"

namespace AutoConsole::Core
{
    class EventDispatcher
    {
    public:
        using Handler = std::function<void(const AutoConsole::Abstractions::Event&)>;

        void subscribe(Handler handler);
        void publish(const AutoConsole::Abstractions::Event& eventValue) const;

    private:
        std::vector<Handler> handlers_;
    };
}
