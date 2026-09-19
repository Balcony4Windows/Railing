set(BALCONY_ICON_PATH "${CMAKE_SOURCE_DIR}/assets/Balcony.ico")
configure_file(Balcony.rc.in "${CMAKE_CURRENT_BINARY_DIR}/Balcony.rc" @ONLY)

add_executable(Balcony WIN32
    main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/Balcony.rc"
)

target_include_directories(Balcony PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Balcony
    PRIVATE
        shell32
        balcony_core
        balcony_renderer
        balcony_windows
        balcony_ui
        balcony_lua
        balcony_persistence
        balcony_packages
        balcony_components
        balcony_compiler_options
)
