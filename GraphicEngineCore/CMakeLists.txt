set(SHADER_FILES
    "shaders/color_vs.hlsl"
    "shaders/color_ps.hlsl"
)

set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(COMPILED_SHADERS "")
foreach(SHADER_FILE ${SHADER_FILES})
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME_WE)
    set(OUTPUT_FILE "${SHADER_OUTPUT_DIR}/${FILE_NAME}.dxbc")

    if("${FILE_NAME}" MATCHES "_vs$")
        set(PROFILE "vs_5_0")
        set(ENTRY "main_vs")
    elseif("${FILE_NAME}" MATCHES "_ps$")
        set(PROFILE "ps_5_0")
        set(ENTRY "main_ps")
    else()
        continue()
    endif()

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND fxc /T ${PROFILE} /E ${ENTRY} /Fo ${OUTPUT_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})

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
    
    "include/Logging.h" 
    "include/renderer/Light.h" 
    "src/renderer/Light.cpp"

 "include/renderer/MaterialSystem.h" "src/renderer/MaterialSystem.cpp")

target_link_libraries(GraphicEngineCore PUBLIC 
    nvrhi 
    nvrhi_d3d11
    Microsoft::DirectXMath
    glfw
    d3d11
    dxgi
    imgui
)

target_include_directories(GraphicEngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(GraphicEngineCore PRIVATE EXPORTING_ENGINE_CORE)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET GraphicEngineCore PROPERTY CXX_STANDARD 23)
endif()