find_program(FXC_EXECUTABLE NAMES fxc REQUIRED)

set(SHADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_textured_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/display_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/display_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_vs.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_ps.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_mask_ps.hlsl"
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
        message(FATAL_ERROR "Shader '${FILE_NAME}' has no supported stage suffix")
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

add_library(RenderBackendD3D11 STATIC
    include/backends/D3D11Backend.h
    include/windows/GLFWWindow.h
    src/backends/D3D11Backend.cpp
    src/windows/GLFWWindow.cpp
)
engine_enable_strict_msvc_checks(RenderBackendD3D11)

target_include_directories(RenderBackendD3D11 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(RenderBackendD3D11 PUBLIC
    GraphicEngineCore
    nvrhi_d3d11
    glfw
    imgui
    d3d11
    dxgi
)

target_compile_features(RenderBackendD3D11 PUBLIC cxx_std_23)
if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    set_property(TARGET RenderBackendD3D11 PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
endif()
add_library(GraphicEnginePlatformD3D11 ALIAS RenderBackendD3D11)
