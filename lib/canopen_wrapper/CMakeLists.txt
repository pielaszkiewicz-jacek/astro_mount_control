# =============================================================================
# lib/canopen_wrapper/CMakeLists.txt
#
# Lekka nakładka CANopen na SocketCAN (CiA 301 + CiA 402).
# Źródła w normalnych plikach .h/.cpp — pełne wsparcie IDE.
# =============================================================================

add_library(canopen SHARED
    src/canopen.cpp
)

target_include_directories(canopen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(canopen PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    OUTPUT_NAME "canopen"
)

message(STATUS "  libcanopen wrapper: ${CMAKE_CURRENT_SOURCE_DIR}")
