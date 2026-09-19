find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(balcony_core STATIC
    Core.cpp
    Clock.cpp
    CpuMonitor.cpp
    MemoryMonitor.cpp
    PowerMonitor.cpp
    NetworkMonitor.cpp
    GpuMonitor.cpp
    NotificationListener.cpp
    SystemMonitors.cpp
    LuaBindings.cpp
    Launcher.cpp
    IconExtractor.cpp
    WindowEnumerator.cpp
)

target_include_directories(balcony_core
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${LUA_INCLUDE_DIR}
)

target_link_libraries(balcony_core
    PUBLIC sol2::sol2
    PRIVATE
        pdh
        dxcore
        wlanapi
        WindowsApp
        shell32
        gdi32
        user32
        dwmapi
        ${LUA_LIBRARIES}
        balcony_compiler_options
)
