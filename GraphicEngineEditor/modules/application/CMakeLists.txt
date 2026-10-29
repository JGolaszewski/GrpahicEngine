file(GLOB_RECURSE EDITOR_APPLICATION_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EditorApplication OBJECT ${EDITOR_APPLICATION_SOURCES})
configure_editor_module(EditorApplication application)
if(ENGINE_ENABLE_PRISM_INFERENCE)
    file(TO_CMAKE_PATH "${ENGINE_PRISM_RUNTIME_ROOT}" ENGINE_PRISM_RUNTIME_ROOT_FOR_CXX)
    target_compile_definitions(EditorApplication PRIVATE
        "ENGINE_PRISM_CONFIGURED_RUNTIME_ROOT=\"${ENGINE_PRISM_RUNTIME_ROOT_FOR_CXX}\""
    )
endif()
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
