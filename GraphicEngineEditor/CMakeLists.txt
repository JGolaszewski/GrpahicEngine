set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

file(GLOB_RECURSE EDITOR_MODULE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/include/*.h"
)

function(configure_editor_module TARGET MODULE_NAME)
    target_include_directories(${TARGET}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/modules"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/${MODULE_NAME}/include"
    )
    target_compile_features(${TARGET} PUBLIC cxx_std_23)
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
