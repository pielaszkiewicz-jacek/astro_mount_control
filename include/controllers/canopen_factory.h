#pragma once
// CANopen interface factory — mirrors the MF7025v2 factory pattern.

#include "controllers/icanopen_interface.h"

#include <memory>
#include <string>
#include <vector>

namespace astro_mount {
namespace controllers {

class CanOpenFactory {
public:
    // Creates a concrete ICanOpenInterface for the given library name:
    //   "canopensocket" — Linux SocketCAN implementation (default)
    //   "mock"          — simulated implementation (no hardware)
    static std::unique_ptr<ICanOpenInterface> create(const std::string& library);

    static std::vector<std::string> getSupportedLibraries();
};

} // namespace controllers
} // namespace astro_mount
