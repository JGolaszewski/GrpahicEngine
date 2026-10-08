file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/include/*.h"
)
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/*/src/*.cpp"
)

function(configure_engine_module TARGET MODULE_NAME)
    target_include_directories(${TARGET}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/modules"
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/${MODULE_NAME}/include"
    )
    target_compile_definitions(${TARGET} PRIVATE
        EXPORTING_ENGINE_CORE
        ENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS}
        ENGINE_DEFAULT_ASSET_ROOT="${ENGINE_ASSET_ROOT}"
        ENGINE_ENABLE_LOGGING=$<BOOL:${ENGINE_ENABLE_LOGGING}>
        ENGINE_LOG_LEVEL_MIN=$<IF:$<CONFIG:Debug>,${ENGINE_LOG_LEVEL_MIN_DEBUG_EFFECTIVE},${ENGINE_LOG_LEVEL_MIN_RELEASE_EFFECTIVE}>
    )
    target_compile_features(${TARGET} PUBLIC cxx_std_23)
    target_compile_options(${TARGET} PRIVATE
        "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>"
    )
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET ${TARGET} PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

add_subdirectory(modules/runtime)
add_subdirectory(modules/assets)
add_subdirectory(modules/scene)
add_subdirectory(modules/rendering)

set(CORE_MODULE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/modules")

add_library(GraphicEngineCore SHARED ${CORE_HEADERS}
    $<TARGET_OBJECTS:EngineRuntime>
    $<TARGET_OBJECTS:EngineAssets>
    $<TARGET_OBJECTS:EngineScene>
    $<TARGET_OBJECTS:EngineRendering>
)
target_link_libraries(GraphicEngineCore PUBLIC
    nvrhi
    Microsoft::DirectXMath
)
target_link_libraries(GraphicEngineCore PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(GraphicEngineCore SYSTEM PRIVATE
    "${ENGINE_TINYGLTF_INCLUDE_DIR}"
    "${ENGINE_STB_INCLUDE_DIR}"
)
target_include_directories(GraphicEngineCore PUBLIC
    "${CORE_MODULE_INCLUDE_DIRS}"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/runtime/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/assets/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/scene/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/rendering/include"
)
target_compile_options(GraphicEngineCore PUBLIC
    "$<$<CXX_COMPILER_ID:MSVC>:/Zc:preprocessor>"
)
target_compile_definitions(GraphicEngineCore PRIVATE
    ENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS}
    ENGINE_DEFAULT_ASSET_ROOT="${ENGINE_ASSET_ROOT}"
)
target_compile_definitions(GraphicEngineCore PUBLIC
    ENGINE_ENABLE_LOGGING=$<BOOL:${ENGINE_ENABLE_LOGGING}>
    ENGINE_LOG_LEVEL_MIN=$<IF:$<CONFIG:Debug>,${ENGINE_LOG_LEVEL_MIN_DEBUG_EFFECTIVE},${ENGINE_LOG_LEVEL_MIN_RELEASE_EFFECTIVE}>
)
set_property(TARGET GraphicEngineCore PROPERTY CXX_STANDARD 23)
if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    set_property(TARGET GraphicEngineCore PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
endif()

set(CORE_HEADER_CHECK_SOURCES)
set(CORE_HEADER_CHECK_DIR "${CMAKE_CURRENT_BINARY_DIR}/public_header_checks")
file(MAKE_DIRECTORY "${CORE_HEADER_CHECK_DIR}")
foreach(HEADER IN LISTS CORE_HEADERS)
    string(REGEX REPLACE "^.*/include/" "" HEADER_INCLUDE "${HEADER}")
    string(REPLACE "/" "_" HEADER_ID "${HEADER_INCLUDE}")
    set(HEADER_CHECK_SOURCE "${CORE_HEADER_CHECK_DIR}/${HEADER_ID}.cpp")
    file(WRITE "${HEADER_CHECK_SOURCE}" "#include \"${HEADER_INCLUDE}\"\n")
    list(APPEND CORE_HEADER_CHECK_SOURCES "${HEADER_CHECK_SOURCE}")
endforeach()
add_library(CorePublicHeaderChecks OBJECT ${CORE_HEADER_CHECK_SOURCES})
target_link_libraries(CorePublicHeaderChecks PRIVATE GraphicEngineCore)
target_compile_definitions(CorePublicHeaderChecks PRIVATE ENGINE_MAX_LIGHTS=${ENGINE_MAX_LIGHTS})

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(NOT CLANG_FORMAT_EXECUTABLE AND DEFINED ENV{VSINSTALLDIR})
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format
        HINTS "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin")
endif()
file(GLOB_RECURSE EDITOR_STYLE_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/GraphicEngineEditor/*.h"
    "${PROJECT_SOURCE_DIR}/GraphicEngineEditor/*.cpp"
)
if(CLANG_FORMAT_EXECUTABLE)
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${CORE_HEADERS} ${CORE_SOURCES} ${EDITOR_STYLE_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking core and editor C++ formatting"
    )
else()
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found; format target is unavailable"
    )
endif()

file(GLOB_RECURSE ENGINE_TIDY_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/GraphicEngineCore/modules/*/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/GraphicEngineCore/tests/*.cpp"
    "${PROJECT_SOURCE_DIR}/GraphicEngineEditor/modules/*/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/GraphicEngineBackends/D3D11/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/SandboxApp/*.cpp"
)
if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    if(WIN32)
        set(ENGINE_CLANG_TIDY_WRAPPER_ARGS)
        string(JOIN ";" ENGINE_CLANG_TIDY_SOURCE_ARGUMENT ${ENGINE_TIDY_SOURCES})
        if(MSVC)
            list(APPEND ENGINE_CLANG_TIDY_WRAPPER_ARGS -UseClDriver)
        endif()
        add_custom_target(lint ALL
            COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                "${ENGINE_CLANG_TIDY_WRAPPER}"
                -CompilationDatabase "${CMAKE_BINARY_DIR}/compile_commands.json"
                -ClangTidy "${ENGINE_CLANG_TIDY_EXECUTABLE}"
                ${ENGINE_CLANG_TIDY_WRAPPER_ARGS}
                -Sources "${ENGINE_CLANG_TIDY_SOURCE_ARGUMENT}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Running clang-tidy on project C++ sources"
            VERBATIM
        )
    else()
        add_custom_target(lint ALL
            COMMAND ${ENGINE_CLANG_TIDY_COMMAND} --quiet -p "${CMAKE_BINARY_DIR}" ${ENGINE_TIDY_SOURCES}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Running clang-tidy on project C++ sources"
            VERBATIM
        )
    endif()
    add_dependencies(EngineRuntime lint)
    add_dependencies(EngineAssets lint)
    add_dependencies(EngineScene lint)
    add_dependencies(EngineRendering lint)
    add_dependencies(GraphicEngineCore lint)
endif()

add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Running engine tests"
)

if(BUILD_TESTING)
    add_executable(BrdfReferenceTests tests/BrdfReferenceTests.cpp)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET BrdfReferenceTests PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
    target_link_libraries(BrdfReferenceTests PRIVATE GraphicEngineCore Microsoft::DirectXMath nvrhi)
    add_test(NAME BrdfReferenceTests COMMAND BrdfReferenceTests)

    add_executable(AssetManagerTests tests/AssetManagerTests.cpp)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET AssetManagerTests PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
    target_link_libraries(AssetManagerTests PRIVATE GraphicEngineCore)
    add_test(NAME AssetManagerTests COMMAND AssetManagerTests)
endif()
