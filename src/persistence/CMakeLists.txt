find_package(cereal CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)

add_library(balcony_persistence STATIC
    Persistence.cpp
    LuaBindings.cpp
)

target_include_directories(balcony_persistence
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${LUA_INCLUDE_DIR}
)

target_link_libraries(balcony_persistence
    PUBLIC
        balcony_core
        # Persistence.h (a public header) uses cereal::make_nvp directly,
        # so consumers of balcony_persistence need cereal's include dirs
        # too -- not just Persistence.cpp itself.
        cereal::cereal
        sol2::sol2
    PRIVATE
        shell32
        ${LUA_LIBRARIES}
        balcony_compiler_options
)
