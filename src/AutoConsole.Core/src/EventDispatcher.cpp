#include "AutoConsole/Core/EventDispatcher.h"

namespace AutoConsole::Core
{
    void EventDispatcher::subscribe(Handler handler)
    {
        handlers_.push_back(handler);
    }

    void EventDispatcher::publish(const AutoConsole::Abstractions::Event& eventValue) const
    {
        for (const auto& handler : handlers_)
        {
            handler(eventValue);
        }
    }
}
