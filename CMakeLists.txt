cmake_minimum_required(VERSION 3.22.1)

project(campello_procedural VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

option(CAMPELLO_PROCEDURAL_BUILD_TEST "Build the tests" OFF)

# WebAssembly SIMD (-msimd128), on by default: every browser with WebGPU has
# it. Compiles this directory with it (the library and the third-party code it
# builds); the target passes it on to its consumers below.
option(CAMPELLO_PROCEDURAL_WASM_SIMD "WebAssembly builds: use SIMD (-msimd128)" ON)
if(EMSCRIPTEN AND CAMPELLO_PROCEDURAL_WASM_SIMD)
    add_compile_options(-msimd128)
    add_link_options(-msimd128)
endif()

find_package(Threads REQUIRED)

add_library(${PROJECT_NAME}
    src/json.cpp
    src/materialx.cpp
    src/nodes.cpp
    src/program.cpp
    src/value.cpp
    src/version.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CAMPELLO_PROCEDURAL_VERSION="${PROJECT_VERSION}"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Consumers compile and link with SIMD too (the link tells wasm-opt to accept it).
if(EMSCRIPTEN AND CAMPELLO_PROCEDURAL_WASM_SIMD)
    target_compile_options(${PROJECT_NAME} INTERFACE -msimd128)
    target_link_options(${PROJECT_NAME} INTERFACE -msimd128)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

install(TARGETS ${PROJECT_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY inc/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(CAMPELLO_PROCEDURAL_BUILD_TEST)
    include(dependencies/googletest.cmake)
    enable_testing()
    add_executable(${PROJECT_NAME}_test
        test/bake_test.cpp
        test/json_test.cpp
        test/materialx_test.cpp
        test/nodes_test.cpp
        test/tileable_test.cpp
    )
    target_link_libraries(${PROJECT_NAME}_test PRIVATE ${PROJECT_NAME} gtest_main)
    target_compile_definitions(${PROJECT_NAME}_test PRIVATE
        CAMPELLO_PROCEDURAL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/test")
    if(MSVC)
        target_compile_options(${PROJECT_NAME}_test PRIVATE /utf-8)
    endif()
    include(GoogleTest)
    gtest_discover_tests(${PROJECT_NAME}_test)
endif()
