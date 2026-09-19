add_library(balcony_windows STATIC
    WindowsIntegration.cpp
)

target_include_directories(balcony_windows PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(balcony_windows
    PUBLIC
        balcony_core
    PRIVATE
        user32
        balcony_compiler_options
)
