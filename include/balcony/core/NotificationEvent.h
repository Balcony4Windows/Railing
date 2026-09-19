#pragma once

#include "balcony/core/EventBus.h"

#include <string>

// Data model for a system notification. Nothing publishes these yet:
// wiring a real listener (Windows.UI.Notifications.Management.
// UserNotificationListener, via WinRT) needs app-identity/COM activation
// work substantial enough for its own pass. This gets the shape and the
// bus ready for that pass. See CLAUDE.md section 6.
namespace balcony::core
{
    struct NotificationEvent
    {
        std::wstring appName;
        std::wstring title;
        std::wstring message;
    };

    using NotificationBus = EventBus<NotificationEvent>;
}
