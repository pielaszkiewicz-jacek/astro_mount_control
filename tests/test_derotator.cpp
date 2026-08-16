#include <gtest/gtest.h>
#include "controllers/derotator_controller.h"
#include "hal/derotator_tmc5160.h"
#include "models/field_rotation_model.h"
#include <cmath>
#include <memory>
#include <array>
#include <thread>
#include <chrono>
#include <fstream>

using namespace astro_mount::controllers;
using namespace astro_mount::hal;
using namespace astro_mount::models;

// ============================================================================
// P5 — DerotatorController selects the field-rotation model by mount type
// ============================================================================

// Minimal mock HAL so DerotatorController is testable without hardware.
namespace {
class MockDerotator : public DerotatorControl {
public:
    std::string name() const override { return "mock"; }
    bool connect() override { return true; }
    void disconnect() override {}
    bool home() override { return true; }
    bool setMode(DerotatorMode) override { return true; }
    bool setAngle(double) override { return true; }
    bool setRate(double) override { return true; }
    DerotatorStatus getStatus() override { return DerotatorStatus{}; }
    bool isMoving() const override { return false; }
    bool initialize() override { return true; }
};
} // anonymous namespace

TEST(DerotatorControllerP5Test, EquatorialReturnsZero) {
    DerotatorController ctl(std::make_unique<MockDerotator>());
    // An equatorial mount tracks the object → no field rotation (P5).
    ctl.setMountPosition(10.0, 20.0, 52.0, 2.0, 45.0,
                         MountKind::EQUATORIAL, {0.0, 0.0, 0.0, 1.0});
    auto fr = ctl.getFieldRotation();
    EXPECT_DOUBLE_EQ(fr.angle_deg, 0.0);
    EXPECT_DOUBLE_EQ(fr.rate_arcsec_s, 0.0);
}

TEST(DerotatorControllerP5Test, AltAzMatchesParallacticModel) {
    DerotatorController ctl(std::make_unique<MockDerotator>());
    ctl.setMountPosition(10.0, 20.0, 52.0, 2.0, 45.0,
                         MountKind::ALT_AZ, {0.0, 0.0, 0.0, 1.0});
    auto fr = ctl.getFieldRotation();
    auto expected = FieldRotationModel::calculateAltAz(2.0, 45.0, 52.0);
    EXPECT_NEAR(fr.angle_deg, expected.angle_deg, 1e-9);
    EXPECT_NEAR(fr.rate_arcsec_s, expected.rate_arcsec_s, 1e-9);
}

TEST(DerotatorControllerP5Test, CasualIdentityMatchesAltAz) {
    DerotatorController ctl(std::make_unique<MockDerotator>());
    // Identity quaternion → CASUAL reduces to the alt-az model.
    ctl.setMountPosition(30.0, 60.0, 52.0, 2.0, 45.0,
                         MountKind::CASUAL, {0.0, 0.0, 0.0, 1.0});
    auto fr = ctl.getFieldRotation();
    auto expected = FieldRotationModel::calculateCasual(
        30.0, 60.0, {0.0, 0.0, 0.0, 1.0}, 2.0, 45.0, 52.0);
    EXPECT_NEAR(fr.angle_deg, expected.angle_deg, 1e-9);
    EXPECT_NEAR(fr.rate_arcsec_s, expected.rate_arcsec_s, 1e-9);
}

TEST(DerotatorControllerP5Test, CasualAppliesOrientationQuaternion) {
    DerotatorController ctl(std::make_unique<MockDerotator>());
    std::array<double,4> id{0.0, 0.0, 0.0, 1.0};
    ctl.setMountPosition(30.0, 60.0, 52.0, 2.0, 45.0, MountKind::CASUAL, id);
    auto casual_id = ctl.getFieldRotation();

    // 45° roll of the mount about its own pointing axis (unit pointing vector
    // for axis1=30°, axis2=60°) — must roll the image by ≈45° (see
    // FieldRotationCasualTest.NonIdentityQuaternionChangesAngle).
    std::array<double,3> u = {
        std::cos(30.0 * M_PI / 180.0) * std::cos(60.0 * M_PI / 180.0),
        std::cos(30.0 * M_PI / 180.0) * std::sin(60.0 * M_PI / 180.0),
        std::sin(30.0 * M_PI / 180.0),
    };
    const double half = 22.5 * M_PI / 180.0;
    std::array<double,4> qr = {
        std::sin(half) * u[0], std::sin(half) * u[1], std::sin(half) * u[2], std::cos(half),
    };
    ctl.setMountPosition(30.0, 60.0, 52.0, 2.0, 45.0, MountKind::CASUAL, qr);
    auto casual_qr = ctl.getFieldRotation();

    EXPECT_NEAR(std::abs(casual_qr.angle_deg - casual_id.angle_deg), 45.0, 1e-6);
    EXPECT_NEAR(casual_qr.rate_arcsec_s, casual_id.rate_arcsec_s, 1e-6);
}

// ============================================================================
// P6 — Tmc5160Derotator position integration (current_position_deg)
// ============================================================================

// Write a small TMC5160 config to /tmp with a high max_speed so the position
// mode reaches its target quickly (deterministic test).
static std::string writeTmcConfig(const std::string& content) {
    static int counter = 0;
    std::string path = "/tmp/astro_tmc5160_test_" + std::to_string(++counter) + ".json";
    std::ofstream out(path);
    out << content;
    out.close();
    return path;
}

TEST(Tmc5160DerotatorP6Test, PositionIntegratesAfterSetAngle) {
    // High max_speed → position mode slews to target almost instantly.
    std::string cfg = writeTmcConfig(
        R"({"type":"TMC5160","simulate":true,"max_speed":100000.0,"gear_ratio":1.0,"microsteps":1,"steps_per_rev":200})");
    Tmc5160Derotator hal(cfg);
    ASSERT_TRUE(hal.connect());
    ASSERT_TRUE(hal.initialize());

    ASSERT_TRUE(hal.setAngle(30.0));
    // Allow the position model to advance (a few ms at 100000 deg/s).
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto s = hal.getStatus();
    EXPECT_NEAR(s.current_position_deg, 30.0, 1.0);
    EXPECT_FALSE(s.moving);

    // Move to a different angle — position must follow.
    ASSERT_TRUE(hal.setAngle(-45.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto s2 = hal.getStatus();
    EXPECT_NEAR(s2.current_position_deg, -45.0, 1.0);
}

TEST(Tmc5160DerotatorP6Test, VelocityIntegratesRate) {
    // Empty config path → simulation mode (defaults).
    Tmc5160Derotator hal("");
    ASSERT_TRUE(hal.connect());
    ASSERT_TRUE(hal.initialize());

    ASSERT_TRUE(hal.setRate(2.0));   // 2 deg/s
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto s = hal.getStatus();
    // position ≈ 2.0 deg/s × ~0.25 s ≈ 0.5 deg (with tolerance for scheduling).
    EXPECT_NEAR(s.current_position_deg, 0.5, 0.2);
    EXPECT_TRUE(s.moving);

    ASSERT_TRUE(hal.setRate(0.0));   // stop
    auto s2 = hal.getStatus();
    EXPECT_FALSE(s2.moving);
}
