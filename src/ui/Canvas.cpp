#include "balcony/ui/Canvas.h"

#include "balcony/core/Invalidation.h"
#include "balcony/renderer/PrimitiveRenderer.h"

#include <Windows.h>

namespace balcony::ui
{
    void Canvas::Draw(balcony::renderer::PrimitiveRenderer& renderer) const
    {
        VisualComponent::Draw(renderer);

        if (!_onDraw)
        {
            return;
        }

        if (_animated)
        {
            balcony::core::RequestRedraw();
        }

        _currentRenderer = &renderer;
        // _onDraw is a Lua callback wrapped as a protected call by its
        // Lua binding (see ui::ProtectedCallback in LuaBindings.cpp) --
        // a Lua-side error there is already caught and logged, never
        // thrown here. This is just a last-resort net for a genuine
        // C++-level exception, which should never actually happen.
        try
        {
            _onDraw();
        }
        catch (const std::exception& e)
        {
            OutputDebugStringA(e.what());
            OutputDebugStringA("\n");
        }
        catch (...)
        {
        }
        _currentRenderer = nullptr;
    }

    void Canvas::DrawRect(const balcony::renderer::RectF& rect, const balcony::renderer::ColorRGBA& color) const
    {
        if (!_currentRenderer || !IsInitialized())
        {
            return;
        }

        _currentRenderer->DrawQuad(rect, FillTextureSrvIndex(), balcony::renderer::RectF{0.0f, 0.0f, 1.0f, 1.0f}, color);
    }
}
