include(GoogleTest)

function(engine_configure_test_target TARGET_NAME)
    engine_enable_strict_msvc_checks(${TARGET_NAME})
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET ${TARGET_NAME} PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    file(GLOB_RECURSE ENGINE_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
    )
    add_executable(GraphicEngineUnitTests ${ENGINE_UNIT_TEST_SOURCES})
    # Compile the private encoding helpers into the test binary so tests can
    # exercise them without exporting implementation details from the engine DLL.
    target_sources(GraphicEngineUnitTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../modules/rendering/src/RenderOutputImageWriters.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicEngineEditor/modules/application/src/EditorProjectWorkflow.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicEngineEditor/modules/application/src/EditorSceneDocument.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicEngineEditor/modules/panels/src/AssetDropAssignment.cpp"
    )
    target_link_libraries(GraphicEngineUnitTests PRIVATE
        GraphicEngineCore
        EditorCommands
        EditorPlatform
        imgui
        nvrhi_d3d11
        d3d11
        nlohmann_json::nlohmann_json
        GTest::gtest_main
        OpenImageIO::OpenImageIO
    )
    target_include_directories(GraphicEngineUnitTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../modules/rendering/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicEngineEditor/modules"
    )
    file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/SandboxApp/Project" ENGINE_SANDBOX_PROJECT_ROOT)
    target_compile_definitions(GraphicEngineUnitTests PRIVATE
        ENGINE_SANDBOX_PROJECT_ROOT="${ENGINE_SANDBOX_PROJECT_ROOT}"
    )
    target_compile_features(GraphicEngineUnitTests PRIVATE cxx_std_23)
    engine_configure_test_target(GraphicEngineUnitTests)
    if(MSVC)
        # MSVC /analyze reports C6326 from GoogleTest's typed matchers when
        # constants are compared during template instantiation.
        target_compile_options(GraphicEngineUnitTests PRIVATE /wd6326)
    endif()
    gtest_discover_tests(GraphicEngineUnitTests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "unit"
    )

    # Legacy harnesses remain separate executables, but share CTest, labels,
    # build policy, and the IDE test explorer with the GoogleTest suite.
    add_executable(AssetManagerTests AssetManagerTests.cpp)
    target_link_libraries(AssetManagerTests PRIVATE GraphicEngineCore)
    target_compile_features(AssetManagerTests PRIVATE cxx_std_23)
    engine_configure_test_target(AssetManagerTests)
    add_test(NAME AssetManagerTests COMMAND AssetManagerTests)
    set_tests_properties(AssetManagerTests PROPERTIES LABELS "integration")

    add_executable(BrdfReferenceTests BrdfReferenceTests.cpp)
    target_link_libraries(BrdfReferenceTests PRIVATE GraphicEngineCore Microsoft::DirectXMath nvrhi)
    target_compile_features(BrdfReferenceTests PRIVATE cxx_std_23)
    engine_configure_test_target(BrdfReferenceTests)
    add_test(NAME BrdfReferenceTests COMMAND BrdfReferenceTests)
    set_tests_properties(BrdfReferenceTests PROPERTIES LABELS "unit")

    # These named targets provide stable entry points in Visual Studio, Ninja,
    # and CI. CTest still exposes each individual test to Test Explorer.
    add_custom_target(engine-unit-tests
        DEPENDS GraphicEngineUnitTests BrdfReferenceTests
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L unit
        USES_TERMINAL
        COMMENT "Running all engine unit tests"
    )
    add_custom_target(engine-integration-tests
        DEPENDS AssetManagerTests
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L integration
        USES_TERMINAL
        COMMENT "Running all engine integration tests"
    )
    add_custom_target(engine-tests
        DEPENDS GraphicEngineUnitTests AssetManagerTests BrdfReferenceTests
        COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
        USES_TERMINAL
        COMMENT "Running all registered engine tests"
    )
endif()

option(ENGINE_BUILD_BENCHMARKS "Build opt-in Google Benchmark workloads" OFF)
if(ENGINE_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
    add_executable(GraphicEngineBenchmarks benchmarks/ArchetypeEcsBenchmarks.cpp)
    target_link_libraries(GraphicEngineBenchmarks PRIVATE GraphicEngineCore benchmark::benchmark_main)
    target_compile_features(GraphicEngineBenchmarks PRIVATE cxx_std_23)
    engine_configure_test_target(GraphicEngineBenchmarks)

    if(BUILD_TESTING)
        if(TARGET engine-tests)
            add_dependencies(engine-tests GraphicEngineBenchmarks)
        endif()
        add_test(NAME GraphicEngineBenchmarks
            COMMAND GraphicEngineBenchmarks
                --benchmark_min_time=0.1
                --benchmark_out=${CMAKE_BINARY_DIR}/benchmarks.json
                --benchmark_out_format=json
        )
        set_tests_properties(GraphicEngineBenchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE)
        add_custom_target(engine-benchmark-tests
            DEPENDS GraphicEngineBenchmarks
            COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure -L benchmark
            USES_TERMINAL
            COMMENT "Running engine benchmark smoke tests"
        )
    endif()
endif()
