file(GLOB_RECURSE EDITOR_COMMAND_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EditorCommands OBJECT ${EDITOR_COMMAND_SOURCES})
configure_editor_module(EditorCommands commands)
target_link_libraries(EditorCommands PRIVATE GraphicEngineCore EditorEvents nlohmann_json::nlohmann_json)
