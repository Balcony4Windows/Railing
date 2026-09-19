#pragma once

#include "balcony/ui/Container.h"

// The Start Menu area. Just a named Container -- no background, no app
// list, nothing preset. Composition (what's in it, where, how it's
// styled) is decided entirely by whoever builds it. See CLAUDE.md
// section 14.
namespace balcony::components
{
    class StartMenu : public balcony::ui::Container
    {
    };
}
