file(GLOB_RECURSE ENGINE_RENDERING_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EngineRendering OBJECT ${ENGINE_RENDERING_SOURCES})
configure_engine_module(EngineRendering rendering)
target_link_libraries(EngineRendering PRIVATE nvrhi Microsoft::DirectXMath)
