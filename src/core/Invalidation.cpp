#include "balcony/core/Invalidation.h"

namespace balcony::core
{
    namespace
    {
        // Starts true so the very first iteration of the main loop
        // always renders at least one frame.
        bool g_needsRedraw = true;
    }

    void RequestRedraw()
    {
        g_needsRedraw = true;
    }

    bool HasPendingRedraw()
    {
        return g_needsRedraw;
    }

    bool ConsumeRedrawRequest()
    {
        const bool pending = g_needsRedraw;
        g_needsRedraw = false;
        return pending;
    }
}
