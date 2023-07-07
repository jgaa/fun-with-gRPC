project(proto LANGUAGES CXX)

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

file(GLOB PROTO_FILES "${PROJECT_SOURCE_DIR}/*.proto")
message("   using PROTO_FILES: ${PROTO_FILES}")

add_library(${PROJECT_NAME} STATIC ${PROTO_FILES})
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:protobuf::libprotobuf>
        $<BUILD_INTERFACE:${GRPC_LIB}>
)

target_include_directories(proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET ${PROJECT_NAME} LANGUAGE cpp)
protobuf_generate(TARGET ${PROJECT_NAME} LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

