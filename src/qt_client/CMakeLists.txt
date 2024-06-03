cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(QtGrpcExample VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Svg Test Protobuf Grpc Concurrent Test)

qt_policy(
    SET QTP0001 NEW
)

qt_standard_project_setup()

set (protofile "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/route_guide.proto")

qt_add_protobuf(RouteGuideLib
    QML
    QML_URI routeguide.pb
    PROTO_FILES ${protofile}
)

qt_add_grpc(GrpcClient CLIENT
    PROTO_FILES ${protofile}
)

qt_add_executable(${PROJECT_NAME}
    main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${FUN_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include>
    )

add_dependencies(${PROJECT_NAME}
    logfault
)

qt_add_qml_module(${PROJECT_NAME}
    URI appExample
    VERSION ${VERSION}
    QML_FILES
        Main.qml
    RESOURCES
    SOURCES
        ServerComm.h
        ServerComm.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(${PROJECT_NAME} PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.app${PROJECT_NAME}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_NAME qt_grpc_client
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_dependencies(${PROJECT_NAME} logfault GrpcClient RouteGuideLib)

target_link_libraries(GrpcClient
    PRIVATE
    RouteGuideLib
    Qt6::Core
    Qt6::Protobuf
    Qt6::Grpc
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        GrpcClient
        RouteGuideLib
        Qt6::Core
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Svg
        Qt6::Protobuf
        Qt6::Grpc
)

qt_import_qml_plugins(${PROJECT_NAME})
qt_finalize_executable(${PROJECT_NAME})
