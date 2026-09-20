#pragma once

// A single process-wide "the screen needs to be repainted" signal.
//
// Why this exists: the render loop used to draw and Present every
// single vsync tick unconditionally, whether or not anything on screen
// had actually changed -- burning a full frame of CPU+GPU work 60
// times a second even while the desktop sat completely idle. A real
// compositor only repaints on damage. This is Balcony's (deliberately
// small) equivalent: every mutating call on the primitive set
// (VisualComponent/Container/Text/Image/Tooltip/Animation/Canvas)
// raises this flag; main.cpp's loop only renders when it's set, and
// blocks efficiently (via MsgWaitForMultipleObjects) rather than
// busy-spinning when it isn't. See CLAUDE.md section 4's "efficient
// invalidation/redraw behavior."
//
// Lives in core (not ui, where most of the call sites are) because
// balcony_windows -- which also needs to raise it, for raw input
// events -- doesn't depend on balcony_ui, and shouldn't start to; core
// is the common ancestor both already depend on. A plain global, not
// per-window state: there is exactly one DE surface/render loop in
// this process (CLAUDE.md section 3), so there's nothing to
// disambiguate, and everything that touches it (Win32 message
// dispatch, Lua execution, rendering) runs on the same single thread,
// so no synchronization is needed.
namespace balcony::core
{
    void RequestRedraw();

    // Peek without clearing -- used to decide whether the main loop
    // should skip its idle wait this iteration (something's already
    // pending, e.g. a Canvas mid-animation) rather than delay it by up
    // to one poll interval.
    bool HasPendingRedraw();

    // Peek and clear in one step -- called once per main-loop iteration
    // to decide whether to actually render this time.
    bool ConsumeRedrawRequest();
}
