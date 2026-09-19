#pragma once

#include <sol/forward.hpp>

namespace balcony::renderer
{
    class Renderer;
}

// Registers the UI primitives (Text, Image, Animation, Spacer,
// Container, Tooltip, Audio) as Lua-usable types, plus the RectF/Color
// value types they take. Call once against a LuaRuntime's State()
// before running any script that composes UI.
//
// Methods that need engine objects (loading a texture from a file,
// rasterizing text, ...) are wrapped so Lua only ever sees plain values
// (strings, numbers) -- GraphicsDevice/CommandQueue/PrimitiveRenderer
// are never exposed to Lua itself, matching CLAUDE.md section 7's
// "Lua composes, C++ implements."
//
// Lifetime note: components Lua creates are owned by Lua's garbage
// collector. Container::Add only ever stores a raw, non-owning pointer
// (true for C++ callers too -- see Container's own header), so a script
// must keep a reference to anything it Add()s alive for as long as the
// container uses it (e.g. by holding it in a table). This mirrors the
// existing C++ contract rather than inventing a new one; a real
// ownership model across the Lua/C++ boundary is still an open
// question (see CLAUDE.md section 21, Q8).
namespace balcony::ui
{
    void RegisterLuaBindings(sol::state& lua, balcony::renderer::Renderer& renderer);
}
