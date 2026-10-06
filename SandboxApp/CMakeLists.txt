set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(Sandbox 
	"main.cpp"
)

set_target_properties(Sandbox PROPERTIES
    OUTPUT_NAME "Sandbox"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(Sandbox PRIVATE GraphicEngineCore GraphicEnginePlatformD3D11)
target_include_directories(Sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies(Sandbox CompileShaders)
