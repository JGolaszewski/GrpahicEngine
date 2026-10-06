add_library(RenderBackendD3D11 STATIC
    "../GraphicEngineCore/include/backends/D3D11Backend.h"
    "../GraphicEngineCore/include/windows/GLFWWindow.h"
    "../GraphicEngineCore/src/backends/D3D11Backend.cpp"
    "../GraphicEngineCore/src/windows/GLFWWindow.cpp"
)

target_include_directories(RenderBackendD3D11 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../GraphicEngineCore/include"
)

target_link_libraries(RenderBackendD3D11 PUBLIC
    GraphicEngineCore
    nvrhi_d3d11
    glfw
    d3d11
    dxgi
)

target_compile_features(RenderBackendD3D11 PUBLIC cxx_std_23)
add_library(GraphicEnginePlatformD3D11 ALIAS RenderBackendD3D11)
