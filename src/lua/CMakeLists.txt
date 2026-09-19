find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(balcony_lua STATIC
    LuaRuntime.cpp
)

target_include_directories(balcony_lua
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${LUA_INCLUDE_DIR}
)

target_link_libraries(balcony_lua
    PUBLIC
        balcony_core
        sol2::sol2
    PRIVATE
        ${LUA_LIBRARIES}
        balcony_compiler_options
)
