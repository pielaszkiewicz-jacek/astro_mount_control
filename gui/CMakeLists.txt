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

file(GLOB_RECURSE PROTO_FILES ${CMAKE_SOURCE_DIR}/../proto/*.proto)
set(PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})
protobuf_generate(PROTO_PATH ${CMAKE_SOURCE_DIR}/.. PROTOS ${PROTO_FILES}
    LANGUAGE cpp PROTOC_OUT_DIR ${PROTO_GEN_DIR} OUT_VAR PROTO_SRCS)
protobuf_generate(PROTO_PATH ${CMAKE_SOURCE_DIR}/.. PROTOS ${PROTO_FILES}
    LANGUAGE grpc PLUGIN protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
    GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PROTOC_OUT_DIR ${PROTO_GEN_DIR}
    OUT_VAR PROTO_GRPC_SRCS)

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
    ${PANEL_SOURCES}
    ${WIDGET_SOURCES}
    ${PROTO_SRCS}
)

target_include_directories(astro_mount_gui PRIVATE
    ${PROTO_GEN_DIR} src ${CMAKE_SOURCE_DIR}/..)

target_link_libraries(astro_mount_gui PRIVATE
    Qt5::Widgets Qt5::Network Qt5::Svg
    gRPC::grpc++ protobuf::libprotobuf)
