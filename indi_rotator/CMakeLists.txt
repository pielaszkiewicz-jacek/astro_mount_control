cmake_minimum_required(VERSION 3.16)
project(astro_mount_indi_rotator_driver VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================
# Dependencies
# ============================================

find_package(INDI REQUIRED)

# ============================================
# Executable: INDI Rotator Driver
# ============================================

add_executable(astro_mount_indi_rotator_driver
    astro_mount_rotator_driver.cpp
)

target_link_libraries(astro_mount_indi_rotator_driver
    mount_grpc_client
    ${INDI_LIBRARIES}
    ${INDI_DATA_DIR}
    pthread
)

target_include_directories(astro_mount_indi_rotator_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../indi
    ${INDI_INCLUDE_DIRS}
)

# ============================================
# Install rules
# ============================================

install(TARGETS astro_mount_indi_rotator_driver
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
)

# Copy driver to INDI directory for indiserver discovery
install(TARGETS astro_mount_indi_rotator_driver
    RUNTIME DESTINATION ${INDI_DATA_DIR}/drivers
    OPTIONAL
)
