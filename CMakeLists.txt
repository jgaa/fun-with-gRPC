cmake_minimum_required(VERSION 3.24)
project(fun-with-grpc
    DESCRIPTION "gRPC experiments"
    HOMEPAGE_URL https://github.com/jgaa/fun-with-gRPC
    VERSION 0.3.0
    LANGUAGES CXX)

option(WITH_CLI "Enable the non-QT applications" ON)
option(WITH_QT "Enable QT client" OFF)

add_definitions(-DVERSION=\"${CMAKE_PROJECT_VERSION}\")

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(FUN_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)

if (WITH_CLI)
    find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
        system
        program_options
        date_time
        )

    add_library(boost INTERFACE IMPORTED)
    set_property(TARGET boost PROPERTY
        INTERFACE_INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIR})
endif()

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake/3rdparty.cmake)

if (WITH_CLI)
    add_subdirectory(src/grpc)
    add_subdirectory(src/async-server)
    add_subdirectory(src/async-client)
    add_subdirectory(src/callback-server)
    add_subdirectory(src/callback-client)
endif()

if (WITH_QT)
    add_subdirectory(src/qt_client)
endif()
