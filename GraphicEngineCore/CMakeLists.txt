find_program(FXC_EXECUTABLE NAMES fxc REQUIRED)

set(ENGINE_ASSET_ROOT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" CACHE PATH
    "Runtime root containing compiled engine assets")

set(SHADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_textured_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_ps.hlsl"
)
file(GLOB SHADER_INCLUDE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsli"
)

set(SHADER_OUTPUT_DIR "${ENGINE_ASSET_ROOT}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

set(COMPILED_SHADERS)
foreach(SHADER_FILE IN LISTS SHADER_FILES)
    get_filename_component(FILE_NAME "${SHADER_FILE}" NAME_WE)
    set(OUTPUT_FILE "${SHADER_OUTPUT_DIR}/${FILE_NAME}.dxbc")

    if(FILE_NAME MATCHES "_vs$")
        set(PROFILE "vs_5_0")
        set(ENTRY "main_vs")
    elseif(FILE_NAME MATCHES "_ps$")
        set(PROFILE "ps_5_0")
        set(ENTRY "main_ps")
    else()
        message(FATAL_ERROR "Shader '${SHADER_FILE}' has no supported stage suffix")
    endif()

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND "${FXC_EXECUTABLE}" /nologo /DENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS}
                /I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                /T "${PROFILE}" /E "${ENTRY}" /Fo "${OUTPUT_FILE}" "${SHADER_FILE}"
        DEPENDS "${SHADER_FILE}" ${SHADER_INCLUDE_FILES}
        VERBATIM
    )
    list(APPEND COMPILED_SHADERS "${OUTPUT_FILE}")
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})

set(CORE_HEADERS
    include/AssetManager.h
    include/AssetImporter.h
    include/Application.h
    include/backends/D3D11Backend.h
    include/components/Transform.h
    include/CoreExport.h
    include/EditorCameraController.h
    include/events/Event.h
    include/Logging.h
    include/PipelineCache.h
    include/PrimitiveFactory.h
    include/RenderBackend.h
    include/ViewportTarget.h
    include/renderer/Camera.h
    include/renderer/DebugGeometry.h
    include/renderer/Entity.h
    include/renderer/Light.h
    include/renderer/Material.h
    include/renderer/MaterialSystem.h
    include/renderer/Mesh.h
    include/renderer/RenderDevice.h
    include/renderer/ResourceManager.h
    include/renderer/render_graph/FrameBlackboard.h
    include/renderer/render_graph/RenderCache.h
    include/renderer/render_graph/RenderContext.h
    include/renderer/render_graph/RenderGraph.h
    include/renderer/render_graph/RenderPass.h
    include/renderer/render_graph/RenderView.h
    include/renderer/render_graph/RGBuilder.h
    include/renderer/render_graph/RGResourcePool.h
    include/renderer/render_graph/RGResources.h
    include/renderer/RenderPacket.h
    include/renderer/ShaderFactory.h
    include/renderer/Viewport.h
    include/renderer/ViewportBinding.h
    include/renderer/brdf/Brdf.h
    include/RenderSystem.h
    include/Scene.h
    include/SceneDocument.h
    include/SceneRenderer.h
    include/Window.h
    include/windows/GLFWWindow.h
)

set(CORE_SOURCES
    src/AssetManager.cpp
    src/AssetImporter.cpp
    src/Application.cpp
    src/backends/D3D11Backend.cpp
    src/components/Transform.cpp
    src/EditorCameraController.cpp
    src/PipelineCache.cpp
    src/PrimitiveFactory.cpp
    src/renderer/Camera.cpp
    src/renderer/DebugGeometry.cpp
    src/renderer/Entity.cpp
    src/renderer/Light.cpp
    src/renderer/Material.cpp
    src/renderer/MaterialSystem.cpp
    src/renderer/Mesh.cpp
    src/renderer/RenderDevice.cpp
    src/renderer/ResourceManager.cpp
    src/renderer/render_graph/RenderContext.cpp
    src/renderer/render_graph/RenderGraph.cpp
    src/renderer/render_graph/RenderPass.cpp
    src/renderer/render_graph/RenderView.cpp
    src/renderer/render_graph/RGBuilder.cpp
    src/renderer/render_graph/RGResourcePool.cpp
    src/renderer/render_graph/RGResources.cpp
    src/renderer/ShaderFactory.cpp
    src/renderer/Viewport.cpp
    src/ViewportTarget.cpp
    src/RenderSystem.cpp
    src/Scene.cpp
    src/SceneDocument.cpp
    src/SceneRenderer.cpp
    src/windows/GLFWWindow.cpp
)

add_library(GraphicEngineCore SHARED ${CORE_HEADERS} ${CORE_SOURCES})
add_dependencies(GraphicEngineCore CompileShaders)

target_link_libraries(GraphicEngineCore PUBLIC
    nvrhi
    nvrhi_d3d11
    Microsoft::DirectXMath
    glfw
    d3d11
    dxgi
    imgui
)

target_link_libraries(GraphicEngineCore PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(GraphicEngineCore SYSTEM PRIVATE
    "${ENGINE_TINYGLTF_INCLUDE_DIR}"
    "${ENGINE_STB_INCLUDE_DIR}"
)

target_include_directories(GraphicEngineCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_definitions(GraphicEngineCore PRIVATE
    EXPORTING_ENGINE_CORE
    ENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS}
    ENGINE_DEFAULT_ASSET_ROOT="${ENGINE_ASSET_ROOT}"
)

if(CMAKE_VERSION VERSION_GREATER 3.12)
    set_property(TARGET GraphicEngineCore PROPERTY CXX_STANDARD 23)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${CORE_HEADERS} ${CORE_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking core C++ formatting"
    )
else()
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; format target is unavailable"
    )
endif()

find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(CLANG_TIDY_EXECUTABLE)
    add_custom_target(lint
        COMMAND "${CLANG_TIDY_EXECUTABLE}" -p="${CMAKE_BINARY_DIR}" ${CORE_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running clang-tidy on core sources"
    )
else()
    add_custom_target(lint
        COMMAND ${CMAKE_COMMAND} -E echo "clang-tidy not found; lint target is unavailable"
    )
endif()

add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running engine tests"
)

if(BUILD_TESTING)
    add_executable(BrdfReferenceTests tests/BrdfReferenceTests.cpp)
    target_include_directories(BrdfReferenceTests PRIVATE include)
    target_link_libraries(BrdfReferenceTests PRIVATE GraphicEngineCore Microsoft::DirectXMath nvrhi)
    add_test(NAME BrdfReferenceTests COMMAND BrdfReferenceTests)

    add_executable(AssetManagerTests tests/AssetManagerTests.cpp)
    target_include_directories(AssetManagerTests PRIVATE include)
    target_link_libraries(AssetManagerTests PRIVATE GraphicEngineCore)
    add_test(NAME AssetManagerTests COMMAND AssetManagerTests)
endif()
