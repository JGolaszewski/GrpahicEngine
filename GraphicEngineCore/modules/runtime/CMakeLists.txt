file(GLOB_RECURSE ENGINE_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(EngineRuntime OBJECT ${ENGINE_RUNTIME_SOURCES})
configure_engine_module(EngineRuntime runtime)
target_link_libraries(EngineRuntime PRIVATE
    nvrhi
    Microsoft::DirectXMath
    nlohmann_json::nlohmann_json
)
if(ENGINE_ENABLE_PRISM_INFERENCE)
    target_compile_definitions(EngineRuntime PRIVATE ENGINE_ENABLE_PRISM_INFERENCE=1)
    target_link_libraries(EngineRuntime PRIVATE onnxruntime::onnxruntime)
else()
    target_compile_definitions(EngineRuntime PRIVATE ENGINE_ENABLE_PRISM_INFERENCE=0)
endif()
target_include_directories(EngineRuntime SYSTEM PRIVATE
    "${ENGINE_TINYGLTF_INCLUDE_DIR}"
    "${ENGINE_STB_INCLUDE_DIR}"
)
