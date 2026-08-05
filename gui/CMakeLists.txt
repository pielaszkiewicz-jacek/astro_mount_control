cmake_minimum_required(VERSION 3.16)
project(AstroMountGui VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 COMPONENTS Widgets Network Svg Charts QUIET)
if(NOT Qt5_FOUND)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Charts)
endif()

find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

# Reuse proto-generated sources from the main build — avoids redundant
# protoc invocation and sidesteps protobuf_generate() PROTO_PATH issues
# in CMake ≥4.x subdirectory contexts.
set(MAIN_BUILD_DIR ${CMAKE_SOURCE_DIR}/../build)
set(PROTO_GEN_DIR ${MAIN_BUILD_DIR}/gen)

if(NOT EXISTS ${PROTO_GEN_DIR})
    message(FATAL_ERROR "Main build proto output not found at ${PROTO_GEN_DIR}. "
            "Build the top-level project first: cd ../.. && cmake -B build && cmake --build build")
endif()

file(GLOB_RECURSE PROTO_SRCS
    ${PROTO_GEN_DIR}/proto/*.pb.cc
    ${PROTO_GEN_DIR}/proto/*.grpc.pb.cc
)

# Qt resource file for stylesheet
set(RESOURCE_FILES resources.qrc)

set(PANEL_SOURCES
    src/panels/mount_panel.cpp src/panels/mount_panel.h
    src/panels/status_panel.cpp src/panels/status_panel.h
    src/panels/calibration_wizard.cpp src/panels/calibration_wizard.h
    src/panels/sequencer_panel.cpp src/panels/sequencer_panel.h
    src/panels/dome_panel.cpp src/panels/dome_panel.h
    src/panels/focuser_panel.cpp src/panels/focuser_panel.h
    src/panels/camera_panel.cpp src/panels/camera_panel.h
    src/panels/weather_panel.cpp src/panels/weather_panel.h
    src/panels/derotator_panel.cpp src/panels/derotator_panel.h
    src/panels/pec_panel.cpp src/panels/pec_panel.h
    src/panels/power_panel.cpp src/panels/power_panel.h
    src/panels/notifications_panel.cpp src/panels/notifications_panel.h
    src/panels/settings_dialog.cpp src/panels/settings_dialog.h
    src/panels/settings_panel.cpp src/panels/settings_panel.h
    src/panels/database_panel.cpp src/panels/database_panel.h
)

set(WIDGET_SOURCES
    src/widgets/sky_map.cpp src/widgets/sky_map.h
    src/widgets/star_chart.cpp src/widgets/star_chart.h
    src/widgets/focus_graph.cpp src/widgets/focus_graph.h
    src/widgets/weather_plot.cpp src/widgets/weather_plot.h
)

add_executable(astro_mount_gui
    src/main.cpp
    src/main_window.cpp
    src/grpc_client.cpp
    src/db_grpc_client.cpp
    ${RESOURCE_FILES}
    ${PANEL_SOURCES}
    ${WIDGET_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_mount_gui PRIVATE
    ${PROTO_GEN_DIR} ${PROTO_GEN_DIR}/proto src ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(astro_mount_gui PRIVATE
    Qt5::Widgets Qt5::Network Qt5::Svg
    gRPC::grpc++ protobuf::libprotobuf)
