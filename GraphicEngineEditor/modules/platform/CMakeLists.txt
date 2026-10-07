file(GLOB_RECURSE EDITOR_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EditorPlatform STATIC ${EDITOR_PLATFORM_SOURCES})
configure_editor_module(EditorPlatform platform)
target_link_libraries(EditorPlatform PRIVATE GraphicEngineCore RenderBackendD3D11 imgui)
