cmake_minimum_required (VERSION 3.15)


# Hot reload enable for MSVC
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("GraphicEngine")

set(NVRHI_WITH_DX11 ON)
set(ENGINE_MAX_LIGHTS 8 CACHE STRING "Maximum number of lights supported by CPU and GPU lighting buffers")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For SourceTrail
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_XCODE_GENERATE_SCHEME ON)
if(MSVC)
    set(CMAKE_VS_GLOBALS "VcpkgEnabled=true")
endif()

find_package(directxmath CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")

add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

target_include_directories(imgui PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(imgui PUBLIC
    glfw
    d3d11
)

add_subdirectory(vendor/nvrhi)

add_subdirectory ("GraphicEngineCore")
add_subdirectory ("GraphicEngineEditor")

add_subdirectory ("SandboxApp")
