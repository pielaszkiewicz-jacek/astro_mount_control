#include <gtest/gtest.h>
#include "controllers/canopen_factory.h"

using namespace astro_mount::controllers;

TEST(CanOpenFactory, CreatesSocketCanBackend) {
    auto iface = CanOpenFactory::create("canopensocket");
    ASSERT_NE(iface, nullptr);
    EXPECT_FALSE(iface->isInitialized());
}

TEST(CanOpenFactory, EmptyLibraryDefaultsToSocketCan) {
    auto iface = CanOpenFactory::create("");
    ASSERT_NE(iface, nullptr);
}

TEST(CanOpenFactory, ReportsSupportedLibraries) {
    const auto libs = CanOpenFactory::getSupportedLibraries();
    EXPECT_FALSE(libs.empty());
    bool found = false;
    for (const auto& lib : libs) {
        if (lib == "canopensocket") found = true;
    }
    EXPECT_TRUE(found);
}
