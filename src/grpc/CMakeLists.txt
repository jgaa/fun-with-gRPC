project(grpctest LANGUAGES CXX)

# --- protobuf and grpc
# Put it in a library 'proto' for simplicity.
#
# This approach assumes that protobuf is installed where
# CMake can find it. Tested under Kubuntu 23.04 with grpc/protobuf
# installed with apt.

INCLUDE(FindProtobuf)
FIND_PACKAGE(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
set(GRPC_LIB gRPC::grpc gRPC::grpc++)

set(PROTO_FILES
    route_guide.proto ${GRPC_SRC_FILES}
)

add_library(proto ${PROTO_FILES})
target_link_libraries(proto
    PUBLIC
        protobuf::libprotobuf
        ${GRPC_LIB}
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET proto LANGUAGE cpp)
protobuf_generate(TARGET proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

###### grpctest app

#add_executable(${PROJECT_NAME}
#    ${PROJECT_NAME}.cpp
#    ${GLAD_ROOT}/include/glad/grpc.hpp
#)

#set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

#add_dependencies(${PROJECT_NAME}
#    proto
#    glad
#    )

#target_include_directories(${PROJECT_NAME}
#    PRIVATE
#    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
#    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
#    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
#    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
#    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include
#    )

#target_link_libraries(${PROJECT_NAME}
#    glad
#    ${Boost_LIBRARIES}
#    ${CMAKE_THREAD_LIBS_INIT}
#    $<BUILD_INTERFACE:${Protobuf_LIBRARIES}>
#    $<BUILD_INTERFACE:proto>
#)

