set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(ENGINE_EDITOR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB_RECURSE EDITOR_MODULE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/include/*.h"
)

function(configure_editor_module TARGET MODULE_NAME)
    engine_enable_strict_msvc_checks(${TARGET})
    target_include_directories(${TARGET}
        PUBLIC
            "${ENGINE_EDITOR_SOURCE_DIR}/modules"
            "${ENGINE_EDITOR_SOURCE_DIR}/modules/${MODULE_NAME}/include"
    )
    target_compile_features(${TARGET} PUBLIC cxx_std_23)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET ${TARGET} PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

add_subdirectory(modules/events)
add_subdirectory(modules/commands)
add_subdirectory(modules/panels)
add_subdirectory(modules/platform)
add_subdirectory(modules/application)

set(EDITOR_MODULE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules")

add_executable(Editor
    $<TARGET_OBJECTS:EditorApplication>
    $<TARGET_OBJECTS:EditorCommands>
    $<TARGET_OBJECTS:EditorPanels>
    ${EDITOR_MODULE_HEADERS}
)
engine_enable_strict_msvc_checks(Editor)
set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "Editor"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
target_compile_features(Editor PRIVATE cxx_std_23)
target_link_libraries(Editor PRIVATE
    GraphicEngineCore
    RenderBackendD3D11
    EditorPlatform
    EditorEvents
    imgui
    nlohmann_json::nlohmann_json
    comdlg32
    shell32
    ole32
)
target_include_directories(Editor PRIVATE
    "${EDITOR_MODULE_INCLUDE_DIRS}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/application/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/commands/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/events/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/panels/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/include"
)
add_dependencies(Editor CompileShaders)

if(ENGINE_ENABLE_PRISM_INFERENCE)
    add_custom_target(run-editor-prism
        COMMAND "${CMAKE_COMMAND}" -E env
            "GRAPHICENGINE_PRISM_RUNTIME_DIR=${ENGINE_PRISM_RUNTIME_ROOT}"
            "$<TARGET_FILE:Editor>"
        DEPENDS Editor
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:Editor>"
        USES_TERMINAL
        VERBATIM
        COMMENT "Launching Editor with the staged PRISM runtime"
    )
endif()

set(EDITOR_HEADER_CHECK_SOURCES)
set(EDITOR_HEADER_CHECK_DIR "${CMAKE_CURRENT_BINARY_DIR}/public_header_checks")
file(MAKE_DIRECTORY "${EDITOR_HEADER_CHECK_DIR}")
foreach(HEADER IN LISTS EDITOR_MODULE_HEADERS)
    string(REGEX REPLACE "^.*/include/" "" HEADER_INCLUDE "${HEADER}")
    string(REPLACE "/" "_" HEADER_ID "${HEADER_INCLUDE}")
    set(HEADER_CHECK_SOURCE "${EDITOR_HEADER_CHECK_DIR}/${HEADER_ID}.cpp")
    file(WRITE "${HEADER_CHECK_SOURCE}" "#include \"${HEADER_INCLUDE}\"\n")
    list(APPEND EDITOR_HEADER_CHECK_SOURCES "${HEADER_CHECK_SOURCE}")
endforeach()
add_library(EditorPublicHeaderChecks OBJECT ${EDITOR_HEADER_CHECK_SOURCES})
engine_enable_strict_msvc_checks(EditorPublicHeaderChecks)
target_include_directories(EditorPublicHeaderChecks PRIVATE
    "${EDITOR_MODULE_INCLUDE_DIRS}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/application/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/commands/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/events/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/panels/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/include"
)
target_link_libraries(EditorPublicHeaderChecks PRIVATE
    GraphicEngineCore
    RenderBackendD3D11
    EditorPlatform
    EditorEvents
    imgui
)
