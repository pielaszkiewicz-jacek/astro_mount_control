cmake_minimum_required(VERSION 3.16)
project(astro_mount_indi_driver VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================
# Dependencies
# ============================================

find_package(INDI REQUIRED)
find_package(protobuf REQUIRED)
find_package(gRPC REQUIRED)

# ============================================
# Proto generation
# ============================================

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../proto/mount_controller.proto)
set(PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)

file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

# Generate protobuf sources
set(PROTO_SOURCES
    ${PROTO_BINARY_DIR}/mount_controller.pb.cc
    ${PROTO_BINARY_DIR}/mount_controller.grpc.pb.cc
)

add_custom_command(
    OUTPUT ${PROTO_SOURCES}
    COMMAND protoc
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../proto
        --cpp_out=${PROTO_BINARY_DIR}
        --grpc_out=${PROTO_BINARY_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf/gRPC sources from mount_controller.proto"
)

# ============================================
# Library: MountGrpcClient (standalone, reusable)
# ============================================

add_library(mount_grpc_client STATIC
    MountGrpcClient.cpp
    ${PROTO_SOURCES}
)

target_include_directories(mount_grpc_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTO_BINARY_DIR}
    ${PROTOBUF_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
)

target_link_libraries(mount_grpc_client PUBLIC
    ${PROTOBUF_LIBRARIES}
    ${GRPC_LIBRARIES}
    grpc++
    protobuf::libprotobuf
)

# ============================================
# Executable: INDI Driver
# ============================================

add_executable(astro_mount_indi_driver
    astro_mount_driver.cpp
    IndiPropertyMapper.cpp
)

target_link_libraries(astro_mount_indi_driver
    mount_grpc_client
    ${INDI_LIBRARIES}
    ${INDI_DATA_DIR}
    pthread
)

target_include_directories(astro_mount_indi_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${INDI_INCLUDE_DIRS}
)

# ============================================
# Install rules
# ============================================

install(TARGETS astro_mount_indi_driver
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

# Copy driver to INDI directory for indiserver discovery
install(TARGETS astro_mount_indi_driver
    RUNTIME DESTINATION ${INDI_DATA_DIR}/drivers
    OPTIONAL
)
