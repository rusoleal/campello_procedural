cmake_minimum_required(VERSION 3.22.1)

project(campello_procedural VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

option(CAMPELLO_PROCEDURAL_BUILD_TEST "Build the tests" OFF)

find_package(Threads REQUIRED)

add_library(${PROJECT_NAME}
    src/json.cpp
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
        test/nodes_test.cpp
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
