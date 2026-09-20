find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(balcony_ui STATIC
    VisualComponent.cpp
    Image.cpp
    Animation.cpp
    Text.cpp
    Audio.cpp
    Container.cpp
    Tooltip.cpp
    Canvas.cpp
    LuaBindings.cpp
)

target_include_directories(balcony_ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(balcony_ui
    PUBLIC
        balcony_core
        balcony_renderer
        sol2::sol2
    PRIVATE
        gdi32
        winmm
        ${LUA_LIBRARIES}
        balcony_compiler_options
)
