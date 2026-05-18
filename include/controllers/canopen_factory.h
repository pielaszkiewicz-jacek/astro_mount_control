#ifndef CANOPEN_FACTORY_H
#define CANOPEN_FACTORY_H

#include "controllers/icanopen_interface.h"
#include <memory>
#include <string>

namespace astro_mount {
namespace controllers {

/**
 * @brief Factory for creating CANopen interface implementations
 * 
 * Creates appropriate CANopen implementation based on configuration:
 * - "mock": Simulated CANopen interface for testing/development
 * - "canopensocket": Real CANopen using CANopenSocket library
 * - "libedssharp": Real CANopen using libedssharp library
 * - "canfestival": Real CANopen using CANFestival library
 */
class CanOpenFactory {
public:
    /**
     * @brief Create CANopen interface instance
     * @param library_name Library name: "mock", "canopensocket", "libedssharp", "canfestival"
     * @return Shared pointer to CANopen interface, nullptr if library not supported
     */
    static std::unique_ptr<ICanOpenInterface> create(const std::string& library_name);

    /**
     * @brief Create CANopen interface based on configuration
     * @param config Configuration containing library name
     * @return Shared pointer to CANopen interface, nullptr if creation failed
     */
    static std::unique_ptr<ICanOpenInterface> create(const ICanOpenInterface::Config& config);

    /**
     * @brief Get list of supported CANopen libraries
     * @return Vector of supported library names
     */
    static std::vector<std::string> getSupportedLibraries();

    /**
     * @brief Check if library is supported
     * @param library_name Library name to check
     * @return True if library is supported
     */
    static bool isLibrarySupported(const std::string& library_name);

    /**
     * @brief Get default library name based on system configuration
     * @return Default library name ("mock" if HAVE_CANOPEN=OFF, "canopensocket" if HAVE_CANOPEN=ON)
     */
    static std::string getDefaultLibrary();
};

} // namespace controllers
} // namespace astro_mount

#endif // CANOPEN_FACTORY_H