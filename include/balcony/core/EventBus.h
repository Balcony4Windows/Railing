#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

// Generic event bus: one instantiation per event type, rather than a
// bespoke dispatcher per event. Widgets/Lua subscribe to the specific bus
// for the event they care about. See CLAUDE.md section 6.
namespace balcony::core
{
    template <typename Event>
    class EventBus
    {
    public:
        using Handler = std::function<void(const Event&)>;
        using SubscriptionId = size_t;

        SubscriptionId Subscribe(Handler handler)
        {
            const SubscriptionId id = _nextId++;
            _handlers.emplace_back(id, std::move(handler));
            return id;
        }

        void Unsubscribe(SubscriptionId id)
        {
            _handlers.erase(
                std::remove_if(_handlers.begin(), _handlers.end(),
                                [id](const auto& entry) { return entry.first == id; }),
                _handlers.end());
        }

        void Publish(const Event& event) const
        {
            for (const auto& entry : _handlers)
            {
                entry.second(event);
            }
        }

    private:
        std::vector<std::pair<SubscriptionId, Handler>> _handlers;
        SubscriptionId _nextId = 0;
    };
}
