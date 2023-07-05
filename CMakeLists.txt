cmake_minimum_required(VERSION 3.24)
project(fun-with-grpc
    DESCRIPTION "gRPC experiments"
    HOMEPAGE_URL https://github.com/jgaa/fun-with-gRPC
    VERSION 0.0.1
    LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(FUN_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

include(GNUInstallDirs)

find_package(Threads REQUIRED)

find_package(GTest REQUIRED)

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    )

add_library(boost INTERFACE IMPORTED)
set_property(TARGET boost PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake/3rdparty.cmake)

add_subdirectory(src/grpc)
add_subdirectory(src/async-server)

#add_subdirectory(tests)
