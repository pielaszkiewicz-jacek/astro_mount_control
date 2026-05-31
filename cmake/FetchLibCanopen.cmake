# =============================================================================
# cmake/FetchLibCanopen.cmake
#
# Pobiera CANopenNode z GitHub przez ExternalProject.
# Użycie: make libcanopen  →  git clone https://github.com/CANopenNode/CANopenNode.git
#
# CANopenNode to embedded stack CANopen (CiA 301). Nie jest samodzielnie
# kompilowany — dostarcza nagłówki i kod źródłowy używane przez nakładkę
# w lib/canopen_wrapper/.
# =============================================================================

include(ExternalProject)

set(CANOPENNODE_PREFIX ${CMAKE_BINARY_DIR}/external/canopennode)

ExternalProject_Add(libcanopen
    GIT_REPOSITORY https://github.com/CANopenNode/CANopenNode.git
    GIT_TAG master
    PREFIX ${CANOPENNODE_PREFIX}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    LOG_DOWNLOAD ON
)

message(STATUS "CANopenNode: make libcanopen → git clone https://github.com/CANopenNode/CANopenNode.git")
message(STATUS "  sources: ${CANOPENNODE_PREFIX}/src/libcanopen/")
