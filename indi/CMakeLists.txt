cmake_minimum_required(VERSION 3.16)
project(astro_mount_indi_driver VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================
# Dependencies
# ============================================

find_package(PkgConfig REQUIRED)

# INDI is discovered via pkg-config (libindi.pc) — this is how Debian/Ubuntu
# (INDI 1.x/2.x) ship it. The CMake package config (INDIConfig.cmake) is only
# provided by newer builds, so we prefer pkg-config and fall back to
# find_package(INDI) for those.
pkg_check_modules(INDI_PKG QUIET libindi)

if(INDI_PKG_FOUND)
    set(INDI_INCLUDE_DIRS ${INDI_PKG_INCLUDE_DIRS})
    set(INDI_LIBRARIES ${INDI_PKG_LIBRARIES})
    set(INDI_LIBRARY_DIRS ${INDI_PKG_LIBRARY_DIRS})
    # The drivers directory is not exposed by pkg-config; default to the
    # standard location and allow override with -DINDI_DATA_DIR=...
    set(INDI_DATA_DIR "/usr/share/indi" CACHE PATH "INDI drivers directory")
    message(STATUS "INDI found via pkg-config (${INDI_PKG_VERSION})")
else()
    find_package(INDI QUIET)
    if(NOT INDI_FOUND)
        message(FATAL_ERROR
            "INDI library not found.\n"
            "Install it, e.g.:  sudo apt install libindi-dev\n"
            "or point CMAKE_PREFIX_PATH at an INDI installation "
            "that provides libindi.pc / INDIConfig.cmake.")
    endif()
    # find_package(INDI) sets INDI_INCLUDE_DIRS/INDI_LIBRARIES/INDI_DATA_DIR;
    # INDI_LIBRARY_DIRS may be unset — leave it empty (system lib dirs are used).
    if(NOT DEFINED INDI_DATA_DIR)
        set(INDI_DATA_DIR "/usr/share/indi")
    endif()
endif()

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# libnova — runtime library (libnova-0.16) is usually already installed as a
# dependency of libindi; the dev headers may be missing on minimal systems.
# We look for the headers via pkg-config or a provided directory.
pkg_check_modules(LIBNOVA QUIET libnova)
if(NOT LIBNOVA_FOUND AND NOT DEFINED LIBNOVA_INCLUDE_DIR)
    set(LIBNOVA_INCLUDE_DIR "" CACHE PATH "libnova include directory (e.g. /usr/include)")
endif()
if(LIBNOVA_FOUND)
    set(LIBNOVA_INCLUDE_DIRS ${LIBNOVA_INCLUDE_DIRS})
endif()
if(LIBNOVA_INCLUDE_DIR)
    set(LIBNOVA_INCLUDE_DIRS ${LIBNOVA_INCLUDE_DIR})
endif()

# The gRPC C++ plugin path — the imported target is the reliable way to locate
# it (some gRPC builds do not set the GRPC_CPP_PLUGIN variable).
get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
if(NOT GRPC_CPP_PLUGIN)
    set(GRPC_CPP_PLUGIN "")
endif()

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

target_link_directories(astro_mount_indi_driver PRIVATE
    ${INDI_LIBRARY_DIRS}
)

target_link_libraries(astro_mount_indi_driver
    mount_grpc_client
    ${INDI_LIBRARIES}
    pthread
)

target_include_directories(astro_mount_indi_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${INDI_INCLUDE_DIRS}
    ${LIBNOVA_INCLUDE_DIRS}
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
