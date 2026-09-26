#include "controllers/canopen_factory.h"
#include "controllers/canopen_interface.h"

namespace astro_mount {
namespace controllers {

std::unique_ptr<ICanOpenInterface> CanOpenFactory::create(const std::string& library) {
    if (library.empty() || library == "canopensocket") {
        return std::make_unique<CanOpenInterface>();
    }
    // Additional backends (mock / canfestival / libedssharp) can be added here.
    return nullptr;
}

std::vector<std::string> CanOpenFactory::getSupportedLibraries() {
    return {"canopensocket"};
}

} // namespace controllers
} // namespace astro_mount
