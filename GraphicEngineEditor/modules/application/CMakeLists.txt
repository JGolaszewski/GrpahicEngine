file(GLOB_RECURSE EDITOR_APPLICATION_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EditorApplication OBJECT ${EDITOR_APPLICATION_SOURCES})
configure_editor_module(EditorApplication application)
target_link_libraries(EditorApplication PRIVATE
    GraphicEngineCore
    RenderBackendD3D11
    EditorEvents
    EditorCommands
    EditorPanels
    EditorPlatform
    imgui
    nlohmann_json::nlohmann_json
)
