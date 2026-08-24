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
        COMMAND fxc /DENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS} /T ${PROFILE} /E ${ENTRY} /Fo ${OUTPUT_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}
    )

    list(APPEND COMPILED_SHADERS ${OUTPUT_FILE})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${COMPILED_SHADERS})

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS 
    "include/*.h" 
    "include/*.hpp"
)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS 
    "src/*.cpp"
)

add_library(GraphicEngineCore SHARED 
    ${CORE_HEADERS}
    ${CORE_SOURCES}
 "include/renderer/ViewportBinding.h" "src/renderer/render_graph/RenderView.cpp" )

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

target_compile_definitions(GraphicEngineCore PRIVATE
  EXPORTING_ENGINE_CORE
  ENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS}
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET GraphicEngineCore PROPERTY CXX_STANDARD 23)
endif()
