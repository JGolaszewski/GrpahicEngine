cmake_minimum_required (VERSION 3.15)

# Visual Studio can configure a CMake project directly instead of using its
# configure preset. In that mode, recover the vcpkg settings before project()
# initializes the toolchain.
if(CMAKE_HOST_WIN32)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
        set(ENGINE_VCPKG_TOOLCHAIN "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        if(EXISTS "${ENGINE_VCPKG_TOOLCHAIN}")
            set(CMAKE_TOOLCHAIN_FILE "${ENGINE_VCPKG_TOOLCHAIN}" CACHE FILEPATH
                "vcpkg toolchain used by the engine")
        endif()
    endif()

    if(NOT DEFINED VCPKG_TARGET_TRIPLET)
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
            set(VCPKG_TARGET_TRIPLET "x86-windows" CACHE STRING
                "vcpkg target triplet used by the engine")
        else()
            set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING
                "vcpkg target triplet used by the engine")
        endif()
    endif()
endif()


# Hot reload enable for MSVC
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()


project ("GraphicEngine")

include(CTest)

set(NVRHI_WITH_DX11 ON)
set(ENGINE_MAX_LIGHTS 8 CACHE STRING "Maximum number of lights supported by CPU and GPU lighting buffers")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(ENGINE_ASSET_ROOT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" CACHE PATH
    "Runtime root containing compiled engine assets")

# set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For SourceTrail
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_XCODE_GENERATE_SCHEME ON)
if(MSVC)
    set(CMAKE_VS_GLOBALS "VcpkgEnabled=true")
endif()

# Make manifest-mode packages discoverable even when the IDE regenerates after
# switching to a different vcpkg toolchain file.
if(DEFINED VCPKG_TARGET_TRIPLET)
    if(DEFINED VCPKG_INSTALLED_DIR)
        set(ENGINE_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    else()
        set(ENGINE_VCPKG_PREFIX "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
    endif()
    if(EXISTS "${ENGINE_VCPKG_PREFIX}")
        list(PREPEND CMAKE_PREFIX_PATH "${ENGINE_VCPKG_PREFIX}")
    endif()
endif()

find_package(directxmath CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# tinygltf and stb are header-only vcpkg packages without CMake config targets.
find_path(ENGINE_TINYGLTF_INCLUDE_DIR tiny_gltf.h)
find_path(ENGINE_STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb)
if(NOT ENGINE_TINYGLTF_INCLUDE_DIR OR NOT ENGINE_STB_INCLUDE_DIR)
    message(FATAL_ERROR "tinygltf and stb headers must be available from the vcpkg manifest")
endif()

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
add_subdirectory ("GraphicEnginePlatformD3D11")
add_subdirectory ("GraphicEngineEditor")

add_subdirectory ("SandboxApp")

install(TARGETS GraphicEngineCore RenderBackendD3D11 Editor Sandbox
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY "${ENGINE_ASSET_ROOT}/shaders/" DESTINATION bin/shaders OPTIONAL)

include(CPack)
