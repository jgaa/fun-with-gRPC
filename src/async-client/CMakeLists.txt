project(async-client)

add_executable(${PROJECT_NAME}
    ${PROJECT_NAME}.cpp
    unary-client.hpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

add_dependencies(${PROJECT_NAME}
    proto
    logfault
    boost
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${FUN_ROOT}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include>
    )

target_link_libraries(${PROJECT_NAME}
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    $<BUILD_INTERFACE:${Protobuf_LIBRARIES}>
    $<BUILD_INTERFACE:proto>
)
