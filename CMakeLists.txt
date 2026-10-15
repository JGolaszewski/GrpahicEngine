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

function(engine_enable_strict_msvc_checks TARGET)
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${TARGET} PRIVATE
            /W4
            /WX
            /EHsc
            /permissive-
            /Zc:preprocessor
        )
    endif()
endfunction()

find_program(ENGINE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)
if(NOT ENGINE_CLANG_TIDY_EXECUTABLE AND DEFINED ENV{VSINSTALLDIR})
    find_program(ENGINE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy
        HINTS "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin")
endif()
if(NOT ENGINE_CLANG_TIDY_EXECUTABLE)
    message(FATAL_ERROR "clang-tidy is required to configure this project; install it with the LLVM toolchain.")
endif()
set(ENGINE_CLANG_TIDY_COMMAND "${ENGINE_CLANG_TIDY_EXECUTABLE}")
if(MSVC)
    list(APPEND ENGINE_CLANG_TIDY_COMMAND "--extra-arg-before=--driver-mode=cl")
endif()

if(WIN32)
    set(ENGINE_CLANG_TIDY_WRAPPER "${PROJECT_SOURCE_DIR}/cmake/RunClangTidy.ps1")
endif()

set(NVRHI_WITH_DX11 ON)
set(ENGINE_MAX_LIGHTS 8 CACHE STRING "Maximum number of lights supported by CPU and GPU lighting buffers")
option(ENGINE_ENABLE_LOGGING "Compile engine logging calls" ON)
set(ENGINE_LOG_LEVEL_MIN 0 CACHE STRING "Global minimum log level (0=Trace, 1=Debug, 2=Info, 3=Warn, 4=Error, 5=Fatal, 6=None)")
set_property(CACHE ENGINE_LOG_LEVEL_MIN PROPERTY STRINGS 0 1 2 3 4 5 6)
set(ENGINE_LOG_LEVEL_MIN_DEBUG "" CACHE STRING "Optional Debug log cutoff override; empty uses ENGINE_LOG_LEVEL_MIN")
set(ENGINE_LOG_LEVEL_MIN_RELEASE "" CACHE STRING "Optional Release/Shipping log cutoff override; empty uses ENGINE_LOG_LEVEL_MIN")
set_property(CACHE ENGINE_LOG_LEVEL_MIN_DEBUG PROPERTY STRINGS 0 1 2 3 4 5 6)
set_property(CACHE ENGINE_LOG_LEVEL_MIN_RELEASE PROPERTY STRINGS 0 1 2 3 4 5 6)
if(ENGINE_LOG_LEVEL_MIN_DEBUG STREQUAL "")
    set(ENGINE_LOG_LEVEL_MIN_DEBUG_EFFECTIVE "${ENGINE_LOG_LEVEL_MIN}")
else()
    set(ENGINE_LOG_LEVEL_MIN_DEBUG_EFFECTIVE "${ENGINE_LOG_LEVEL_MIN_DEBUG}")
endif()
if(ENGINE_LOG_LEVEL_MIN_RELEASE STREQUAL "")
    set(ENGINE_LOG_LEVEL_MIN_RELEASE_EFFECTIVE "${ENGINE_LOG_LEVEL_MIN}")
else()
    set(ENGINE_LOG_LEVEL_MIN_RELEASE_EFFECTIVE "${ENGINE_LOG_LEVEL_MIN_RELEASE}")
endif()

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
find_package(OpenImageIO CONFIG REQUIRED)

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

target_include_directories(imgui SYSTEM PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(imgui PUBLIC
    glfw
    d3d11
)

add_subdirectory(vendor/nvrhi)

# NVRHI is vendored, so suppress diagnostics from its public headers in consumers.
get_target_property(NVRHI_PUBLIC_INCLUDE_DIRS nvrhi INTERFACE_INCLUDE_DIRECTORIES)
if(NVRHI_PUBLIC_INCLUDE_DIRS)
    set_property(TARGET nvrhi APPEND PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${NVRHI_PUBLIC_INCLUDE_DIRS}")
endif()

add_subdirectory ("GraphicEngineCore")
add_subdirectory ("GraphicEngineBackends")
add_subdirectory ("GraphicEngineEditor")
add_subdirectory ("GraphicEngineCore/tests")

add_subdirectory ("SandboxApp")

install(TARGETS GraphicEngineCore RenderBackendD3D11 Editor Sandbox
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY "${ENGINE_ASSET_ROOT}/shaders/" DESTINATION bin/shaders OPTIONAL)

include(CPack)
