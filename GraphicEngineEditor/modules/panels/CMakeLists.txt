file(GLOB_RECURSE EDITOR_PANEL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EditorPanels OBJECT ${EDITOR_PANEL_SOURCES})
configure_editor_module(EditorPanels panels)
target_link_libraries(EditorPanels PRIVATE
    GraphicEngineCore
    RenderBackendD3D11
    EditorEvents
    EditorCommands
    imgui
    nlohmann_json::nlohmann_json
)
