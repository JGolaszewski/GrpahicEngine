file(GLOB_RECURSE ENGINE_SCENE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EngineScene OBJECT ${ENGINE_SCENE_SOURCES})
configure_engine_module(EngineScene scene)
target_link_libraries(EngineScene PRIVATE
    nvrhi
    Microsoft::DirectXMath
    nlohmann_json::nlohmann_json
)
