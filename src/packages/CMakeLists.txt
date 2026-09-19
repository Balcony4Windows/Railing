add_library(balcony_packages STATIC
    PackageManager.cpp
)

target_include_directories(balcony_packages PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(balcony_packages
    PUBLIC
        balcony_core
        balcony_persistence
    PRIVATE
        balcony_compiler_options
)
