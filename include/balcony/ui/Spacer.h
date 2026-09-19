#pragma once

#include "balcony/ui/VisualComponent.h"

// Pure layout primitive: reserves space and, by default, draws nothing
// and costs nothing on the GPU. Bounds, hit-testing, and the optional
// background/border are all inherited from VisualComponent unchanged --
// a Spacer with a background/border set is effectively a divider.
// See CLAUDE.md section 5.
namespace balcony::ui
{
    class Spacer : public VisualComponent
    {
    };
}
