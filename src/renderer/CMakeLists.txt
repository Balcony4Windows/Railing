find_package(directx-headers CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)

add_library(balcony_renderer STATIC
    GraphicsDevice.cpp
    CommandQueue.cpp
    DescriptorHeap.cpp
    SwapChain.cpp
    PrimitiveRenderer.cpp
    Texture.cpp
    Renderer.cpp
)

target_include_directories(balcony_renderer PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(balcony_renderer
    PUBLIC
        balcony_core
        Microsoft::DirectX-Headers
        Microsoft::DirectXMath
        d3d12
        dxgi
        d3dcompiler
    PRIVATE
        windowscodecs
        ole32
        balcony_compiler_options
)
