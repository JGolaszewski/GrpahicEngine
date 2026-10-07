add_library(EditorEvents INTERFACE)
target_include_directories(EditorEvents INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(EditorEvents INTERFACE GraphicEngineCore)
