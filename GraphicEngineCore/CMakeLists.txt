add_library(GraphicEngineCore SHARED 
    "include/CoreExport.h"
    "include/events/Event.h"
    "include/Window.h" 

    "include/RenderBackend.h" 

    "include/backends/D3D11Backend.h" 
    "src/backends/D3D11Backend.cpp"

    "include/windows/GLFWWindow.h" 
    "src/windows/GLFWWindow.cpp"

    "include/Application.h"
    "src/Application.cpp"
    
    "include/renderer/Mesh.h"
    "src/renderer/Mesh.cpp"

    "include/renderer/Camera.h"
    "src/renderer/Camera.cpp"
    
    "include/renderer/ShaderFactory.h"
    "src/renderer/ShaderFactory.cpp"
    
    "include/renderer/Material.h"
    "src/renderer/Material.cpp"
    
    "include/renderer/Entity.h" 
    "src/renderer/Entity.cpp" 
    
    "include/SceneRenderer.h" 
    "src/SceneRenderer.cpp" 
    
    "include/PipelineCache.h"
    "src/PipelineCache.cpp"
 
    "include/components/Transform.h"
    "src/components/Transform.cpp"
 "include/Logging.h" "include/renderer/Light.h" "src/renderer/Light.cpp")

target_link_libraries(GraphicEngineCore PUBLIC 
    nvrhi 
    nvrhi_d3d11
    Microsoft::DirectXMath
    glfw
    d3d11
    dxgi
    imgui::imgui
)

target_include_directories(GraphicEngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(GraphicEngineCore PRIVATE EXPORTING_ENGINE_CORE)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET GraphicEngineCore PROPERTY CXX_STANDARD 23)
endif()