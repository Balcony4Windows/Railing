#pragma once

#include "balcony/ui/Container.h"

// The taskbar area. Empty by default: no content, no background, no
// border -- there are no widgets yet, so this is a placeholder.
// Background color and border are available (inherited from
// VisualComponent via Container, same as every other component) but off
// until the composer sets them explicitly. See CLAUDE.md section 13.
namespace balcony::components
{
    class Taskbar : public balcony::ui::Container
    {
    };
}
