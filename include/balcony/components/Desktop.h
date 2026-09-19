#pragma once

#include "balcony/ui/Container.h"

// The desktop surface. Just a named Container -- no background, no icon
// layout, nothing preset. What lives on it and where is decided entirely
// by whoever composes it. See CLAUDE.md sections 2 and 13.
namespace balcony::components
{
    class Desktop : public balcony::ui::Container
    {
    };
}
