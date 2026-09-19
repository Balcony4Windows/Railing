#pragma once

#include "balcony/core/NotificationEvent.h"

#include <memory>

// Listens system-wide for toast notifications via
// Windows.UI.Notifications.Management.UserNotificationListener and
// publishes them on a NotificationBus. See CLAUDE.md section 6.
//
// Getting the listener and requesting access are both, in practice on an
// unpackaged desktop app, calls that can hang indefinitely rather than
// merely being slow (the consent broker has nothing to show and never
// resolves) -- confirmed by direct testing, not a theoretical concern.
// So this never runs any of it on the constructing thread: a detached
// worker does the WinRT work against a refcounted, mutex-protected state
// block the constructor doesn't wait on. If the worker hangs forever, it
// hangs alone; this object's lifetime and the app's shutdown are
// unaffected either way. Call PumpEvents() once per tick on the main
// thread to publish anything the worker has collected.
namespace balcony::core
{
    class NotificationListener
    {
    public:
        NotificationListener();
        ~NotificationListener();

        // False until the (possibly long-delayed, possibly never
        // resolving) access request succeeds.
        bool AccessGranted() const;

        void PumpEvents();

        NotificationBus& Bus() { return _bus; }

        // Implementation detail, public only so the worker-thread
        // functions in NotificationListener.cpp can share it; not part
        // of this class's actual interface.
        struct SharedState;

    private:
        std::shared_ptr<SharedState> _state;

        NotificationBus _bus;
    };
}
