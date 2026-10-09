file(GLOB_RECURSE ENGINE_ASSET_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EngineAssets OBJECT ${ENGINE_ASSET_SOURCES})
configure_engine_module(EngineAssets assets)
target_link_libraries(EngineAssets PRIVATE
    nvrhi
    Microsoft::DirectXMath
    nlohmann_json::nlohmann_json
)
target_include_directories(EngineAssets SYSTEM PRIVATE
    "${ENGINE_TINYGLTF_INCLUDE_DIR}"
    "${ENGINE_STB_INCLUDE_DIR}"
)
