include(GoogleTest)

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)

    file(GLOB_RECURSE ENGINE_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
    )
    add_executable(GraphicEngineUnitTests ${ENGINE_UNIT_TEST_SOURCES})
    target_link_libraries(GraphicEngineUnitTests PRIVATE GraphicEngineCore GTest::gtest_main)
    target_compile_features(GraphicEngineUnitTests PRIVATE cxx_std_23)
    engine_enable_strict_msvc_checks(GraphicEngineUnitTests)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set_property(TARGET GraphicEngineUnitTests PROPERTY CXX_CLANG_TIDY "${ENGINE_CLANG_TIDY_COMMAND}")
    endif()
    gtest_discover_tests(GraphicEngineUnitTests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "unit"
    )

    if(TEST AssetManagerTests)
        set_tests_properties(AssetManagerTests PROPERTIES LABELS "integration")
    endif()
    if(TEST BrdfReferenceTests)
        set_tests_properties(BrdfReferenceTests PROPERTIES LABELS "unit")
    endif()
endif()

option(ENGINE_BUILD_BENCHMARKS "Build opt-in Google Benchmark workloads" OFF)
if(ENGINE_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
    add_executable(GraphicEngineBenchmarks benchmarks/TransformBenchmarks.cpp)
    target_link_libraries(GraphicEngineBenchmarks PRIVATE GraphicEngineCore benchmark::benchmark_main)
    target_compile_features(GraphicEngineBenchmarks PRIVATE cxx_std_23)
    engine_enable_strict_msvc_checks(GraphicEngineBenchmarks)
    add_test(NAME GraphicEngineBenchmarks
        COMMAND GraphicEngineBenchmarks --benchmark_min_time=0.1 --benchmark_out=${CMAKE_BINARY_DIR}/benchmarks.json --benchmark_out_format=json
    )
    set_tests_properties(GraphicEngineBenchmarks PROPERTIES LABELS "benchmark" RUN_SERIAL TRUE)
endif()
