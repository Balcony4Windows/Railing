#include "balcony/core/NotificationListener.h"

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Notifications::Management;

namespace balcony::core
{
    struct NotificationListener::SharedState
    {
        std::atomic<bool> accessGranted{false};
        std::mutex mutex;
        std::vector<NotificationEvent> pending;
    };

    namespace
    {
        void HandleNotificationChanged(
            const std::shared_ptr<NotificationListener::SharedState>& state,
            UserNotificationListener const& sender,
            UserNotificationChangedEventArgs const& args)
        {
            if (args.ChangeKind() != UserNotificationChangedKind::Added)
            {
                return;
            }

            try
            {
                auto notification = sender.GetNotification(args.UserNotificationId());
                if (!notification)
                {
                    return;
                }

                auto binding = notification.Notification().Visual().GetBinding(L"ToastGeneric");
                if (!binding)
                {
                    return;
                }

                auto textElements = binding.GetTextElements();
                const uint32_t count = textElements.Size();

                NotificationEvent event;
                event.appName = notification.AppInfo().DisplayInfo().DisplayName().c_str();

                if (count > 0)
                {
                    event.title = textElements.GetAt(0).Text().c_str();
                }

                for (uint32_t i = 1; i < count; ++i)
                {
                    if (!event.message.empty())
                    {
                        event.message += L"\n";
                    }
                    event.message += textElements.GetAt(i).Text().c_str();
                }

                std::lock_guard<std::mutex> lock(state->mutex);
                state->pending.push_back(std::move(event));
            }
            catch (const winrt::hresult_error&)
            {
                // The notification can be removed/expire between the
                // change event and this fetch; just drop it.
            }
        }

        // Runs entirely on its own detached thread. Never joined, never
        // waited on: see the header comment for why.
        void RunWorker(std::shared_ptr<NotificationListener::SharedState> state)
        {
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);

                auto listener = UserNotificationListener::Current();
                const auto status = listener.RequestAccessAsync().get();

                if (status != UserNotificationListenerAccessStatus::Allowed)
                {
                    return;
                }

                state->accessGranted.store(true, std::memory_order_relaxed);

                listener.NotificationChanged(
                    [state](UserNotificationListener const& sender, UserNotificationChangedEventArgs const& args)
                    {
                        HandleNotificationChanged(state, sender, args);
                    });

                // Keep this thread (and `listener`) alive for the life of
                // the process so the subscription above keeps servicing
                // callbacks. There is deliberately no shutdown signal:
                // see the header comment.
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::hours(24));
                }
            }
            catch (const winrt::hresult_error&)
            {
                // Notification access/activation simply isn't available
                // on this system/configuration; the feature just stays
                // off rather than taking down the DE over it.
            }
        }
    }

    NotificationListener::NotificationListener()
        : _state(std::make_shared<SharedState>())
    {
        std::thread(RunWorker, _state).detach();
    }

    NotificationListener::~NotificationListener() = default;

    bool NotificationListener::AccessGranted() const
    {
        return _state->accessGranted.load(std::memory_order_relaxed);
    }

    void NotificationListener::PumpEvents()
    {
        std::vector<NotificationEvent> drained;
        {
            std::lock_guard<std::mutex> lock(_state->mutex);
            drained.swap(_state->pending);
        }

        for (const auto& event : drained)
        {
            _bus.Publish(event);
        }
    }
}
