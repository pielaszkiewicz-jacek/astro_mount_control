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

# ============================================
# Protobuf / gRPC code generation
# ============================================

# Output directory for generated protobuf sources
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/gen)

# Ensure the output directory exists
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Collect all proto files
file(GLOB_RECURSE PROTO_FILES
    ${CMAKE_SOURCE_DIR}/proto/*.proto
    ${CMAKE_SOURCE_DIR}/db/proto/*.proto
    ${CMAKE_SOURCE_DIR}/dome/proto/*.proto
    ${CMAKE_SOURCE_DIR}/derotator/proto/*.proto
)

# Generate protobuf message sources (.pb.cc / .pb.h)
protobuf_generate(
    PROTO_PATH ${CMAKE_SOURCE_DIR}
    PROTOS ${PROTO_FILES}
    LANGUAGE cpp
    PROTOC_OUT_DIR ${PROTO_GEN_DIR}
    OUT_VAR PROTO_SRCS
)

# Generate gRPC service sources (.grpc.pb.cc / .grpc.pb.h)
protobuf_generate(
    PROTO_PATH ${CMAKE_SOURCE_DIR}
    PROTOS ${PROTO_FILES}
    LANGUAGE grpc
    PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
    PROTOC_OUT_DIR ${PROTO_GEN_DIR}
    OUT_VAR PROTO_GRPC_SRCS
)

# Combine all generated sources
list(APPEND PROTO_SRCS ${PROTO_GRPC_SRCS})

# Collect generated headers for dependency tracking
file(GLOB PROTO_HDRS
    ${PROTO_GEN_DIR}/proto/*.pb.h
    ${PROTO_GEN_DIR}/proto/*.grpc.pb.h
    ${PROTO_GEN_DIR}/db/proto/*.pb.h
    ${PROTO_GEN_DIR}/db/proto/*.grpc.pb.h
)

# Create a custom target to ensure proto files are generated before any target that needs them
add_custom_target(generate_proto DEPENDS ${PROTO_SRCS})

# ============================================

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

# Find libcurl for HTTP/SMTP (weather API sources + notification channels)
find_package(CURL REQUIRED)

# Find OpenSSL for TLS (MQTT notification channel — N5)
find_package(OpenSSL)
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found (${OPENSSL_VERSION}) — MQTT TLS support enabled")
    add_compile_definitions(MQTT_TLS_ENABLED)
else()
    message(STATUS "OpenSSL NOT found — MQTT TLS will be unavailable (use_tls ignored)")
endif()

# CMake's FindSQLite3 module (>= 3.14) creates the imported target
# SQLite::SQLite3.  Create a compatibility alias SQLite3::SQLite3 so the
# canonical name used throughout this project always resolves, regardless
# of the CMake version in use.
if(TARGET SQLite::SQLite3 AND NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 INTERFACE IMPORTED)
    set_property(TARGET SQLite3::SQLite3 PROPERTY INTERFACE_LINK_LIBRARIES SQLite::SQLite3)
endif()

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

# Source files
set(CORE_SOURCES
    src/core/astronomical_calculations.cpp
    src/http_client.cpp
    src/hal/hal_factory.cpp
    src/hal/simulated_hal/simulated_hal.cpp
    src/hal/serial_hal/serial_hal.cpp
    src/hal/ethernet_hal/ethernet_hal.cpp
    src/hal/gamepad_hal/gamepad_hal.cpp
    src/hal/gamepad_hal/gamepad_input_evdev.cpp
    src/hal/mf7025v2_hal/mf7025v2_hal.cpp
    src/controllers/mf7025v2_interface.cpp
)

set(DEROTATOR_SOURCES
    src/hal/derotator_hal/derotator_hal.cpp
    src/hal/derotator_hal/derotator_simulated.cpp
    src/hal/derotator_hal/derotator_tmc5160.cpp
    src/hal/derotator_hal/derotator_home.cpp
    src/models/field_rotation_model.cpp
    src/controllers/derotator_controller.cpp
)

set(DOME_SOURCES
    src/hal/dome_hal/dome_hal.cpp
    src/hal/dome_hal/dome_serial.cpp
    src/hal/dome_hal/dome_rolloff.cpp
    src/hal/dome_hal/dome_simulated.cpp
    src/controllers/dome_controller.cpp
)

set(CAMERA_SOURCES
    src/hal/camera_hal/camera_hal.cpp
    src/hal/camera_hal/camera_zwo.cpp
    src/hal/camera_hal/camera_simulated.cpp
)

set(FOCUSER_SOURCES
    src/hal/focuser_hal/focuser_hal.cpp
    src/hal/focuser_hal/focuser_simulated.cpp
)

set(SEQUENCER_SOURCES
    src/sequencer/observation_plan.cpp
    src/sequencer/observation_sequencer.cpp
)

set(POWER_SOURCES
    src/hal/power_hal/power_hal.cpp
    src/hal/power_hal/power_simulated.cpp
    src/controllers/power_manager.cpp
)

set(PEC_SOURCES
    src/models/pec_model.cpp
)

set(MODEL_SOURCES
    src/models/tpoint_model.cpp
    src/models/kalman_filter.cpp
    src/models/ephemeris_tracker.cpp
    src/models/focus_curve.cpp
    src/models/exposure_planner.cpp
)

set(ST4_SOURCES
    src/hal/st4_hal/st4_hal.cpp
    src/hal/st4_hal/st4_simulated.cpp
    src/hal/st4_hal/st4_gpio_sysfs.cpp
    src/controllers/st4_guider.cpp
    src/controllers/st4_calibration.cpp
)

set(CONTROLLER_SOURCES
    src/controllers/mount_controller.cpp
    src/controllers/weather_client.cpp
    src/controllers/lx200_server.cpp
)

set(API_SOURCES
    src/api/grpc_server.cpp
    src/api/service_impl.cpp
)

set(CONFIG_SOURCES
    src/config/configuration.cpp
    src/config/config_monitor.cpp
)

set(WEATHER_SOURCES
    src/weather/weather_monitor.cpp
    src/weather/weather_rules.cpp
    src/weather/sensors/rain_sensor.cpp
    src/weather/sensors/wind_sensor.cpp
    src/weather/sensors/cloud_sensor.cpp
    src/weather/sensors/gps_receiver.cpp
    src/weather/sources/openweathermap_source.cpp
    src/weather/sources/weathergov_source.cpp
    src/weather/sources/imgw_source.cpp
)

set(NOTIFICATION_SOURCES
    src/notifications/notification_engine.cpp
    src/notifications/channels/email_channel.cpp
    src/notifications/channels/webhook_channel.cpp
    src/notifications/channels/mqtt_channel.cpp
    src/notifications/notification_service.cpp
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

# Create library
add_library(astro_mount_core STATIC
    ${CORE_SOURCES}
    ${CAMERA_SOURCES}
    ${DEROTATOR_SOURCES}
    ${DOME_SOURCES}
    ${FOCUSER_SOURCES}
    ${PEC_SOURCES}
    ${POWER_SOURCES}
    ${ST4_SOURCES}
    ${SEQUENCER_SOURCES}
    ${MODEL_SOURCES}
    ${CONTROLLER_SOURCES}
    ${CONFIG_SOURCES}
    ${LOGGING_SOURCES}
    ${NOTIFICATION_SOURCES}
    ${WEATHER_SOURCES}
)

# Ensure proto files are generated before compiling astro_mount_core
add_dependencies(astro_mount_core generate_proto)

target_link_libraries(astro_mount_core
    sofa_static
    gRPC::grpc++
    protobuf::libprotobuf
    nlohmann_json::nlohmann_json
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
    $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
    Eigen3::Eigen
    SQLite3::SQLite3
    CURL::libcurl
    $<$<BOOL:${OpenSSL_FOUND}>:OpenSSL::SSL>
    $<$<BOOL:${OpenSSL_FOUND}>:OpenSSL::Crypto>
)

# Add include directories
target_include_directories(astro_mount_core PUBLIC
    ${PROTO_GEN_DIR}
    ${PROTO_GEN_DIR}/db/proto
    ${CMAKE_SOURCE_DIR}
    ${EIGEN3_INCLUDE_DIR}
)

# In-process subsystem service sources (dome, derotator, focuser, st4_guider,
# pec, camera, pulley) that are hosted inside the mount controller process
# instead of separate executables.
set(MERGED_SERVICE_SOURCES
    dome/src/dome_service_impl.cpp
    dome/src/dome_factory.cpp
    derotator/src/derotator_service_impl.cpp
    derotator/src/derotator_factory.cpp
    focuser/src/focuser_service_impl.cpp
    st4guider/src/st4_guider_service_impl.cpp
    pec/src/pec_service_impl.cpp
    camera/src/camera_service_impl.cpp
    pulley/src/pulley_service_impl.cpp
    pidcal/src/pid_calibration_service_impl.cpp
)

# Create executable — hosts the mount controller gRPC service plus the
# dome, derotator and focuser services in-process.
add_executable(astro_mount_controller
    ${MAIN_SOURCES}
    ${API_SOURCES}
    ${MERGED_SERVICE_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_mount_controller PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(astro_mount_controller
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

add_dependencies(astro_mount_controller generate_proto)

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

# ============================================
# Weather server
# ============================================
set(WEATHER_SERVER_SOURCES
    weather/src/main.cpp
    weather/src/weather_server.cpp
    weather/src/weather_service_impl.cpp
    weather/src/weather_factory.cpp
)

add_executable(astro_weather_server
    ${WEATHER_SERVER_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_weather_server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(astro_weather_server
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

add_dependencies(astro_weather_server generate_proto)
install(TARGETS astro_weather_server
    RUNTIME DESTINATION bin
)

# ============================================
# Power server
# ============================================
set(POWER_SERVER_SOURCES
    power/src/main.cpp
    power/src/power_server.cpp
    power/src/power_service_impl.cpp
    power/src/power_factory.cpp
)

add_executable(astro_power_server
    ${POWER_SERVER_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_power_server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(astro_power_server
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

add_dependencies(astro_power_server generate_proto)
install(TARGETS astro_power_server
    RUNTIME DESTINATION bin
)

# ============================================
# Sequencer server
# ============================================
set(SEQUENCER_SERVER_SOURCES
    sequencer/src/main.cpp
    sequencer/src/sequencer_server.cpp
    sequencer/src/sequencer_service_impl.cpp
)

add_executable(astro_sequencer_server
    ${SEQUENCER_SERVER_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_sequencer_server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(astro_sequencer_server
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

add_dependencies(astro_sequencer_server generate_proto)

install(TARGETS astro_sequencer_server
    RUNTIME DESTINATION bin
)

# ============================================
# Derotator server
# ============================================
set(DEROTATOR_SERVER_SOURCES
    derotator/src/main.cpp
    derotator/src/derotator_server.cpp
    derotator/src/derotator_service_impl.cpp
    derotator/src/derotator_factory.cpp
)

add_executable(astro_derotator_server
    ${DEROTATOR_SERVER_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_derotator_server PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(astro_derotator_server
    astro_mount_core
    gRPC::grpc++
    protobuf::libprotobuf
)

add_dependencies(astro_derotator_server generate_proto)

install(TARGETS astro_derotator_server
    RUNTIME DESTINATION bin
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

    add_executable(test_focus_curve
        tests/test_focus_curve.cpp
    )

    target_link_libraries(test_focus_curve
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME FocusCurveTest
        COMMAND test_focus_curve)

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
    set_tests_properties(GrpcIntegrationTest PROPERTIES TIMEOUT 1800)

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

    add_executable(test_field_rotation_st4
        tests/test_field_rotation_st4.cpp
    )

    target_link_libraries(test_field_rotation_st4
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME FieldRotationSt4Test
        COMMAND test_field_rotation_st4)

    add_executable(test_derotator
        tests/test_derotator.cpp
    )

    target_link_libraries(test_derotator
        astro_mount_core
        GTest::gtest_main
    )

    add_test(NAME DerotatorTest
        COMMAND test_derotator)

    add_executable(test_weather
        tests/test_weather.cpp
        ${PROTO_SRCS}
    )

    target_link_libraries(test_weather
        astro_mount_core
        GTest::gtest_main
        gRPC::grpc++
        protobuf::libprotobuf
    )

    add_dependencies(test_weather generate_proto)

    add_test(NAME WeatherTest
        COMMAND test_weather)

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