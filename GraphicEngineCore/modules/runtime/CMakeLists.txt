file(GLOB_RECURSE ENGINE_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EngineRuntime OBJECT ${ENGINE_RUNTIME_SOURCES})
configure_engine_module(EngineRuntime runtime)
target_link_libraries(EngineRuntime PRIVATE
    nvrhi
    Microsoft::DirectXMath
    nlohmann_json::nlohmann_json
)
target_include_directories(EngineRuntime SYSTEM PRIVATE
    "${ENGINE_TINYGLTF_INCLUDE_DIR}"
    "${ENGINE_STB_INCLUDE_DIR}"
)
