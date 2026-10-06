set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(Editor
    "main.cpp"
    "Editor.h"
    "Editor.cpp"
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

target_link_libraries(Editor PRIVATE GraphicEngineCore comdlg32 shell32 ole32)

target_include_directories(Editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(Editor CompileShaders)
