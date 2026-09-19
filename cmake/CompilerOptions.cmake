add_library(balcony_compiler_options INTERFACE)

target_compile_features(balcony_compiler_options INTERFACE cxx_std_20)

target_compile_definitions(balcony_compiler_options INTERFACE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(balcony_compiler_options INTERFACE
    /W4
    /permissive-
    /utf-8
    /arch:AVX2
    /Oi
    $<$<CONFIG:Release>:/fp:fast>
    $<$<CONFIG:Release>:/GL>
)

target_link_options(balcony_compiler_options INTERFACE
    $<$<CONFIG:Release>:/LTCG>
)
