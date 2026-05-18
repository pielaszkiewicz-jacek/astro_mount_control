cmake_minimum_required(VERSION 3.15)
project(AstroMountController VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/core
    ${CMAKE_SOURCE_DIR}/include/models
    ${CMAKE_SOURCE_DIR}/include/controllers
    ${CMAKE_SOURCE_DIR}/include/api
    ${CMAKE_SOURCE_DIR}/include/config
    ${CMAKE_SOURCE_DIR}/include/logging
    ${CMAKE_SOURCE_DIR}/include/exceptions
    ${CMAKE_SOURCE_DIR}/db/include
    ${CMAKE_SOURCE_DIR}/db/proto
    ${CMAKE_SOURCE_DIR}/sofa
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Find dependencies
find_package(PkgConfig REQUIRED)

# Use SOFA library directly from the project (sofa directory)
file(GLOB SOFA_SOURCES sofa/*.c)
add_library(sofa_static STATIC ${SOFA_SOURCES})
target_include_directories(sofa_static PUBLIC ${CMAKE_SOURCE_DIR}/sofa)

# Find gRPC
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# Find CanOpen (assuming libcanopen is available)
find_library(CANOPEN_LIB canopen)
if(NOT CANOPEN_LIB)
    message(WARNING "CanOpen library not found. CanOpen support will be disabled.")
    set(HAVE_CANOPEN OFF)
else()
    set(HAVE_CANOPEN ON)
endif()

# Find JSON library (nlohmann/json)
find_package(nlohmann_json REQUIRED)

# Find spdlog for logging (optional)
find_package(spdlog)
if(NOT spdlog_FOUND)
    message(WARNING "spdlog library not found via CMake, attempting direct library search...")
    # When spdlog CMake config is not available (e.g. CMAKE_DISABLE_FIND_PACKAGE_spdlog),
    # try to find it via path-based search instead.
    # The fallback MUST mirror the proper Config mode's target properties:
    #   INTERFACE_COMPILE_DEFINITIONS: "SPDLOG_SHARED_LIB;SPDLOG_COMPILED_LIB;SPDLOG_FMT_EXTERNAL"
    #   INTERFACE_LINK_LIBRARIES:      "Threads::Threads;fmt::fmt"
    find_path(SPDLOG_HEADERS_DIR "spdlog/spdlog.h")
    if(SPDLOG_HEADERS_DIR)
        message(STATUS "spdlog headers found at ${SPDLOG_HEADERS_DIR}")
        # Find the shared spdlog library directly
        find_library(SPDLOG_LIBRARY spdlog)
        if(SPDLOG_LIBRARY)
            message(STATUS "spdlog library found at ${SPDLOG_LIBRARY}")
            # Threads is needed by spdlog (for std::mutex, std::thread)
            find_package(Threads REQUIRED)
            # fmt is needed when SPDLOG_FMT_EXTERNAL is set
            find_package(fmt REQUIRED)
            # Create an imported target for spdlog, mirroring the proper Config mode
            add_library(spdlog::spdlog UNKNOWN IMPORTED)
            set_target_properties(spdlog::spdlog PROPERTIES
                IMPORTED_LOCATION "${SPDLOG_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SPDLOG_HEADERS_DIR}"
                INTERFACE_COMPILE_DEFINITIONS "SPDLOG_SHARED_LIB;SPDLOG_COMPILED_LIB;SPDLOG_FMT_EXTERNAL"
                INTERFACE_LINK_LIBRARIES "Threads::Threads;fmt::fmt"
            )
            set(spdlog_FOUND TRUE)
        else()
            message(WARNING "spdlog library binary not found. Logging may have linker errors.")
        endif()
    else()
        message(WARNING "spdlog headers not found. Logging will use std::cout.")
    endif()
endif()

# Find Eigen for matrix operations
find_package(Eigen3 REQUIRED)

# Find SQLite3
find_package(SQLite3 REQUIRED)

# Find uuid
find_library(UUID_LIB uuid)
if(NOT UUID_LIB)
    message(WARNING "uuid library not found")
endif()

# Find GTest for tests
find_package(GTest)
if(NOT GTest_FOUND)
    message(WARNING "GTest not found. Tests will not be built.")
endif()

# Add definitions
add_definitions(-DHAVE_CANOPEN=${HAVE_CANOPEN})

# Source files
set(CORE_SOURCES
    src/core/astronomical_calculations.cpp
    src/hal/hal_factory.cpp
    src/hal/simulated_hal/simulated_hal.cpp
    src/hal/canopen_hal/canopen_hal.cpp
    src/hal/serial_hal/serial_hal.cpp
    src/hal/ethernet_hal/ethernet_hal.cpp
    src/hal/gamepad_hal/gamepad_hal.cpp
    src/hal/gamepad_hal/gamepad_input_evdev.cpp
)

set(MODEL_SOURCES
    src/models/tpoint_model.cpp
    src/models/kalman_filter.cpp
    src/models/ephemeris_tracker.cpp
)

set(CONTROLLER_SOURCES
    src/controllers/mount_controller.cpp
    src/controllers/canopen_interface.cpp
    src/controllers/canopen_factory.cpp
)

set(API_SOURCES
    src/api/grpc_server.cpp
    src/api/service_impl.cpp
)

set(CONFIG_SOURCES
    src/config/configuration.cpp
    src/config/config_monitor.cpp
)

set(LOGGING_SOURCES
    src/logging/logger.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

set(DB_SOURCES
    db/src/object_database_service.cpp
    db/src/main.cpp
)

# Protobuf files (use existing generated files)
set(PROTO_SRCS
    proto/mount_controller.pb.cc
    proto/mount_controller.grpc.pb.cc
    db/proto/object_database.pb.cc
    db/proto/object_database.grpc.pb.cc
)

set(PROTO_HDRS
    proto/mount_controller.pb.h
    proto/mount_controller.grpc.pb.h
    db/proto/object_database.pb.h
    db/proto/object_database.grpc.pb.h
)

# Create library
add_library(astro_mount_core STATIC
    ${CORE_SOURCES}
    ${MODEL_SOURCES}
    ${CONTROLLER_SOURCES}
    ${CONFIG_SOURCES}
    ${LOGGING_SOURCES}
)

target_link_libraries(astro_mount_core
    sofa_static
    gRPC::grpc++
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
    $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
    Eigen3::Eigen
    SQLite::SQLite3
)

# Add include directories
target_include_directories(astro_mount_core PUBLIC 
    ${CMAKE_SOURCE_DIR}/proto
    ${CMAKE_SOURCE_DIR}/db/proto
    ${EIGEN3_INCLUDE_DIR}
)

if(HAVE_CANOPEN)
    target_link_libraries(astro_mount_core ${CANOPEN_LIB})
endif()

# Create executable
add_executable(astro_mount_controller
    ${MAIN_SOURCES}
    ${API_SOURCES}
    ${PROTO_SRCS}
)

target_link_libraries(astro_mount_controller
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

# Create object database server executable
add_executable(astro_object_database_server
    ${DB_SOURCES}
    ${PROTO_SRCS}
)

target_link_libraries(astro_object_database_server
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
    ${UUID_LIB}
)

# Create tests
enable_testing()

if(GTest_FOUND)
    add_executable(test_astronomical_calculations
        tests/test_astronomical_calculations.cpp
    )

    target_link_libraries(test_astronomical_calculations
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME AstronomicalCalculationsTest
        COMMAND test_astronomical_calculations)

    add_executable(test_tpoint_model
        tests/test_tpoint_model.cpp
    )

    target_link_libraries(test_tpoint_model
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME TPOINTModelTest
        COMMAND test_tpoint_model)

    add_executable(test_kalman_filter
        tests/test_kalman_filter.cpp
    )

    target_link_libraries(test_kalman_filter
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME KalmanFilterTest
        COMMAND test_kalman_filter)

    add_executable(test_grpc_integration
        tests/test_grpc_integration.cpp
        src/api/service_impl.cpp
        ${PROTO_SRCS}
    )

    target_link_libraries(test_grpc_integration
        astro_mount_core
        gRPC::grpc++
        protobuf::libprotobuf
        GTest::gtest_main
    )

    add_test(NAME GrpcIntegrationTest
        COMMAND test_grpc_integration)

    add_executable(test_subarcsecond_accuracy
        tests/test_subarcsecond_accuracy.cpp
    )

    target_link_libraries(test_subarcsecond_accuracy
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME SubArcsecondAccuracyTest
        COMMAND test_subarcsecond_accuracy)

    add_executable(test_configuration
        tests/test_configuration.cpp
    )

    target_link_libraries(test_configuration
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME ConfigurationTest
        COMMAND test_configuration)

    add_executable(test_mount_controller
        tests/test_mount_controller.cpp
        ${PROTO_SRCS}
    )

    target_link_libraries(test_mount_controller
        astro_mount_core
        gRPC::grpc++
        protobuf::libprotobuf
        GTest::gtest_main
    )

    add_test(NAME MountControllerTest
        COMMAND test_mount_controller)

    add_executable(test_ephemeris_tracker
        tests/test_ephemeris_tracker.cpp
        ${PROTO_SRCS}
    )

    target_link_libraries(test_ephemeris_tracker
        astro_mount_core
        gRPC::grpc++
        protobuf::libprotobuf
        GTest::gtest_main
    )

    add_test(NAME EphemerisTrackerTest
        COMMAND test_ephemeris_tracker)

    add_executable(test_hal_integration
        tests/test_hal_integration.cpp
        ${PROTO_SRCS}
    )

    target_link_libraries(test_hal_integration
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME HALIntegrationTest
        COMMAND test_hal_integration)

    add_executable(test_gamepad_hal
        tests/test_gamepad_hal.cpp
    )

    target_link_libraries(test_gamepad_hal
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME GamepadHALTest
        COMMAND test_gamepad_hal)

    add_executable(test_watchdog
        tests/test_watchdog.cpp
    )

    target_link_libraries(test_watchdog
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME WatchdogTest
        COMMAND test_watchdog)

    add_executable(test_logger
        tests/test_logger.cpp
    )

    target_link_libraries(test_logger
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME LoggerTest
        COMMAND test_logger)

    add_executable(test_config_monitor
        tests/test_config_monitor.cpp
    )

    target_link_libraries(test_config_monitor
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME ConfigMonitorTest
        COMMAND test_config_monitor)

    add_executable(test_canopen_factory
        tests/test_canopen_factory.cpp
    )

    target_link_libraries(test_canopen_factory
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME CanOpenFactoryTest
        COMMAND test_canopen_factory)

    add_executable(test_serial_hal
        tests/test_serial_hal.cpp
    )

    target_link_libraries(test_serial_hal
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME SerialHALTest
        COMMAND test_serial_hal)

    add_executable(test_ethernet_hal
        tests/test_ethernet_hal.cpp
    )

    target_link_libraries(test_ethernet_hal
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME EthernetHALTest
        COMMAND test_ethernet_hal)

    add_executable(test_canopen_hal
        tests/test_canopen_hal.cpp
    )

    target_link_libraries(test_canopen_hal
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME CanOpenHALTest
        COMMAND test_canopen_hal)
endif()

# Install targets
install(TARGETS astro_mount_controller
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install systemd service
install(FILES scripts/astro-mount-controller.service
    DESTINATION /lib/systemd/system
)

# Install configuration
install(DIRECTORY config/
    DESTINATION /etc/astro-mount-controller
)