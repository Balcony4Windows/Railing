find_package(cereal CONFIG REQUIRED)

add_library(balcony_persistence STATIC
    Persistence.cpp
)

target_include_directories(balcony_persistence PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(balcony_persistence
    PUBLIC
        balcony_core
    PRIVATE
        cereal::cereal
        balcony_compiler_options
)
