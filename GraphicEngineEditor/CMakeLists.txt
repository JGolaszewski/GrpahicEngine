set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(Editor
    "main.cpp"
    "Editor.h"
    "Editor.cpp"
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
    "events/EditorEvents.h"
    "events/EntitySelectedEvent.h"
    "events/EntityDeselectedEvent.h"
    "events/SceneChangedEvent.h"
    "events/ViewportChangedEvent.h"
    "events/ViewportHoveredEvent.h"
    "events/ViewportResizeRequestedEvent.h"
    "events/CameraSettingsChangedEvent.h"
    "events/MaterialChangedEvent.h"
    "events/AddObjectRequestedEvent.h"
    "events/CommandExecuteRequestedEvent.h"
    "events/DebugRenderToggledEvent.h"
    "commands/Command.h"
    "commands/CommandHistory.h"
    "commands/CommandHistory.cpp"
    "commands/TransformEntityCommand.h"
    "commands/TransformEntityCommand.cpp"
)

set_target_properties(Editor PROPERTIES
    OUTPUT_NAME "Editor"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(Editor PRIVATE GraphicEngineCore)

target_include_directories(Editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(Editor CompileShaders)
