cmake_minimum_required (VERSION 3.15)


# Hot reload enable for MSVC
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("GraphicEngine")

set(NVRHI_WITH_DX11 ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

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

add_subdirectory(vendor/imgui)
add_subdirectory(vendor/nvrhi)

add_subdirectory ("GraphicEngineCore")
add_subdirectory ("GraphicEngineEditor")
