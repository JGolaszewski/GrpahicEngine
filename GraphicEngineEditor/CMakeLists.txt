set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(EditorUI STATIC
    "platform/EditorUiIntegration.h"
    "platform/EditorUiIntegration.cpp"
)
target_include_directories(EditorUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EditorUI PRIVATE GraphicEngineCore RenderBackendD3D11 imgui)
target_compile_features(EditorUI PRIVATE cxx_std_23)

add_executable(Editor
    "main.cpp"
    "Editor.h"
    "Editor.cpp"
    "EditorSceneDocument.h"
    "EditorSceneDocument.cpp"
    "platform/EditorFileDialogs.h"
    "platform/EditorFileDialogs.cpp"
    "panels/EditorPanel.h"
    "panels/HierarchyPanel.h"
    "panels/HierarchyPanel.cpp"
    "panels/InspectorPanel.h"
    "panels/InspectorPanel.cpp"
    "panels/ScenePanel.h"
    "panels/ScenePanel.cpp"
    "panels/MainBar.h"
    "panels/MainBar.cpp"
    "panels/StatisticsPanel.h"
    "panels/StatisticsPanel.cpp"
    "panels/AssetBrowserPanel.h"
    "panels/AssetBrowserPanel.cpp"
    "events/EditorEvents.h"
    "events/EntitySelectedEvent.h"
    "events/EntityDeselectedEvent.h"
    "events/SceneChangedEvent.h"
    "events/ViewportChangedEvent.h"
    "events/ViewportClickedEvent.h"
    "events/ViewportHoveredEvent.h"
    "events/ViewportResizeRequestedEvent.h"
    "events/CameraSettingsChangedEvent.h"
    "events/MaterialChangedEvent.h"
    "events/AddObjectRequestedEvent.h"
    "events/CommandExecuteRequestedEvent.h"
    "events/DebugRenderToggledEvent.h"
    "events/SceneFileActionRequestedEvent.h"
    "commands/Command.h"
    "commands/CommandHistory.h"
    "commands/CommandHistory.cpp"
    "commands/TransformEntityCommand.h"
    "commands/TransformEntityCommand.cpp"
    "commands/ChangeMaterialPropertiesCommand.h"
    "commands/ChangeMaterialPropertiesCommand.cpp"
    "commands/EntityMembershipCommand.h"
    "commands/EntityMembershipCommand.cpp"
    "commands/ChangeMaterialTextureCommand.h"
    "commands/ChangeMaterialTextureCommand.cpp"
    "commands/ReplaceEntityCommand.h"
    "commands/ReplaceEntityCommand.cpp"
    "events/RemoveEntityRequestedEvent.h"
    "events/SceneModifiedEvent.h"
    "events/AssetImportRequestedEvent.h"
    "events/AssetReimportRequestedEvent.h"
    "events/MaterialTextureAssignmentRequestedEvent.h"
    "events/ModelReassignmentRequestedEvent.h"
)

set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "Editor"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(Editor PRIVATE GraphicEngineCore RenderBackendD3D11 EditorUI imgui comdlg32 shell32 ole32)

target_include_directories(Editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(Editor CompileShaders)

file(GLOB_RECURSE EDITOR_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
set(EDITOR_HEADER_CHECK_SOURCES)
set(EDITOR_HEADER_CHECK_DIR "${CMAKE_CURRENT_BINARY_DIR}/public_header_checks")
file(MAKE_DIRECTORY "${EDITOR_HEADER_CHECK_DIR}")
foreach(HEADER IN LISTS EDITOR_PUBLIC_HEADERS)
    file(RELATIVE_PATH HEADER_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}" "${HEADER}")
    string(REPLACE "/" "_" HEADER_ID "${HEADER_INCLUDE}")
    set(HEADER_CHECK_SOURCE "${EDITOR_HEADER_CHECK_DIR}/${HEADER_ID}.cpp")
    file(WRITE "${HEADER_CHECK_SOURCE}" "#include \"${HEADER_INCLUDE}\"\n")
    list(APPEND EDITOR_HEADER_CHECK_SOURCES "${HEADER_CHECK_SOURCE}")
endforeach()
add_library(EditorPublicHeaderChecks OBJECT ${EDITOR_HEADER_CHECK_SOURCES})
target_include_directories(EditorPublicHeaderChecks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(EditorPublicHeaderChecks PRIVATE
    GraphicEngineCore
    RenderBackendD3D11
    EditorUI
    imgui
)
