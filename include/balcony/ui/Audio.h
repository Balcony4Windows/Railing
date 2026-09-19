#pragma once

#include "balcony/ui/Component.h"

#include <string>
#include <string_view>

// Plays a short audio clip. Deliberately independent of the D3D12
// renderer -- audio has nothing to share with the visual primitives, so
// it doesn't participate in PrimitiveRenderer at all. Still a Component
// (Update/Draw inherited as no-ops) so it can sit in a Container
// alongside visual primitives; something else decides when to Play() it.
// See CLAUDE.md section 5 on exposing components without inventing
// needless coupling.
namespace balcony::ui
{
    class Audio : public Component
    {
    public:
        bool SetSource(std::wstring_view path);
        void Play(bool loop = false);
        void Stop();

    private:
        std::wstring _path;
    };
}
