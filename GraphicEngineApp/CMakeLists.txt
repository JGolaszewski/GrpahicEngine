set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(SHADER_FILES
    "shaders/color_vs.hlsl"
    "shaders/color_ps.hlsl"
)

set(COMPILED_SHADERS "")

set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

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

add_executable(GraphicEngineApp 
"main.cpp" 
"SandboxApp.h" 
"SandboxApp.cpp"
"UIWin.h"
"UIWin.cpp"
)

set_target_properties(GraphicEngineApp PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(GraphicEngineCore PUBLIC 
    Microsoft::DirectXMath
    imgui::imgui
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET GraphicEngineApp PROPERTY CXX_STANDARD 23)
endif()

target_link_libraries(GraphicEngineApp PRIVATE GraphicEngineCore)

add_dependencies(GraphicEngineApp CompileShaders)