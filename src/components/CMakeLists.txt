add_library(balcony_components STATIC
    Components.cpp
    DesktopEnvironment.cpp
)

target_include_directories(balcony_components PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(balcony_components PRIVATE
    BALCONY_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts/"
)

target_link_libraries(balcony_components
    PUBLIC
        balcony_core
        balcony_ui
        balcony_renderer
        balcony_windows
        balcony_lua
        balcony_persistence
    PRIVATE
        balcony_compiler_options
)
