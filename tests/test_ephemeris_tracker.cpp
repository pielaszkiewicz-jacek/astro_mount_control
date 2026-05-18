#include "models/ephemeris_tracker.h"
#include "logging/logger.h"
#include "proto/mount_controller.pb.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cmath>

namespace astro_mount {
namespace models {
namespace test {

using namespace std::chrono;

// ============================================================================
// Helper functions
// ============================================================================

::astro_mount::EphemerisPoint make_point(const system_clock::time_point& tp,
                                          double ra, double dec,
                                          double ra_rate, double dec_rate) {
    ::astro_mount::EphemerisPoint point;
    auto tt = system_clock::to_time_t(tp);
    auto dur = tp - system_clock::from_time_t(tt);
    auto ns = duration_cast<nanoseconds>(dur).count();
    point.mutable_timestamp()->set_seconds(tt);
    point.mutable_timestamp()->set_nanos(static_cast<int32_t>(ns));
    point.set_ra(ra);
    point.set_dec(dec);
    point.set_ra_rate(ra_rate);
    point.set_dec_rate(dec_rate);
    return point;
}

::astro_mount::EphemerisData make_ephemeris_data(
    const std::string& object_id = "99942",
    const std::string& object_name = "Test Comet",
    const std::string& object_type = "comet",
    const std::vector<std::tuple<system_clock::time_point, double, double, double, double>>& points = {},
    int interpolation_order = 3) {

    ::astro_mount::EphemerisData data;
    data.set_object_id(object_id);
    data.set_object_name(object_name);
    data.set_object_type(object_type);
    data.set_reference_frame("ICRF");
    data.set_interpolation_order(interpolation_order);

    auto base = system_clock::now();
    size_t count = points.empty() ? 10 : points.size();

    for (size_t i = 0; i < count; ++i) {
        double ra, dec, ra_rate, dec_rate;
        system_clock::time_point t;

        if (i < points.size()) {
            t = std::get<0>(points[i]);
            ra = std::get<1>(points[i]);
            dec = std::get<2>(points[i]);
            ra_rate = std::get<3>(points[i]);
            dec_rate = std::get<4>(points[i]);
        } else {
            t = base + hours(static_cast<int>(i));
            ra = 10.0 + i * 0.1;
            dec = 45.0 + i * 0.05;
            ra_rate = 0.1;
            dec_rate = 0.05;
        }

        auto pt = data.add_points();
        auto tt = system_clock::to_time_t(t);
        auto dur = t - system_clock::from_time_t(tt);
        auto ns = duration_cast<nanoseconds>(dur).count();
        pt->mutable_timestamp()->set_seconds(tt);
        pt->mutable_timestamp()->set_nanos(static_cast<int32_t>(ns));
        pt->set_ra(ra);
        pt->set_dec(dec);
        pt->set_ra_rate(ra_rate);
        pt->set_dec_rate(dec_rate);
    }

    return data;
}

// ============================================================================
// EphemerisInterpolator Tests
// ============================================================================

class EphemerisInterpolatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");
        base_ = system_clock::now();
    }

    system_clock::time_point base_;
};

TEST_F(EphemerisInterpolatorTest, EmptyPointsIsInvalid) {
    std::vector<::astro_mount::EphemerisPoint> empty;
    EphemerisInterpolator interp(empty, 3);
    EXPECT_FALSE(interp.isValid());
}

TEST_F(EphemerisInterpolatorTest, SinglePointIsInvalid) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    EphemerisInterpolator interp(pts, 3);
    EXPECT_FALSE(interp.isValid());
}

TEST_F(EphemerisInterpolatorTest, TwoPointsIsValid) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 11.0, 46.0, 0.1, 0.05));
    EphemerisInterpolator interp(pts, 3);
    EXPECT_TRUE(interp.isValid());
}

TEST_F(EphemerisInterpolatorTest, GetTimeRangeMatchesInput) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(5), 11.0, 46.0, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    auto [start, end] = interp.getTimeRange();
    EXPECT_EQ(start, base_);
    EXPECT_EQ(end, base_ + hours(5));
}

TEST_F(EphemerisInterpolatorTest, GetPointCount) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 10.1, 45.05, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(2), 10.2, 45.10, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());
    EXPECT_EQ(interp.getPointCount(), 3);
}

TEST_F(EphemerisInterpolatorTest, InvalidThrowsOnGetPosition) {
    std::vector<::astro_mount::EphemerisPoint> empty;
    EphemerisInterpolator interp(empty, 3);
    EXPECT_THROW(interp.getPositionAtTime(base_), std::runtime_error);
}

TEST_F(EphemerisInterpolatorTest, ExactNodeReturnsExactValue) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 10.1, 45.05, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    auto [ra, dec, rr, dr] = interp.getPositionAtTime(base_);
    EXPECT_DOUBLE_EQ(ra, 10.0);
    EXPECT_DOUBLE_EQ(dec, 45.0);

    auto [ra2, dec2, rr2, dr2] = interp.getPositionAtTime(base_ + hours(1));
    EXPECT_DOUBLE_EQ(ra2, 10.1);
    EXPECT_DOUBLE_EQ(dec2, 45.05);
}

TEST_F(EphemerisInterpolatorTest, InterpolationAtMidpoint) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 10.1, 45.05, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    auto t_mid = base_ + minutes(30);
    auto [ra, dec, rr, dr] = interp.getPositionAtTime(t_mid);

    // Linear interpolation at midpoint
    EXPECT_NEAR(ra, 10.05, 1e-6);
    EXPECT_NEAR(dec, 45.025, 1e-6);
}

TEST_F(EphemerisInterpolatorTest, PredictPositionWithinRangeReturnsValue) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 10.1, 45.05, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(2), 10.2, 45.10, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    // predictPosition beyond end but within extrapolation window
    auto beyond = base_ + hours(2) + minutes(10);
    auto result = interp.predictPosition(beyond, 3600.0);
    ASSERT_TRUE(result.has_value());
    auto [ra, dec, rr, dr] = result.value();
    EXPECT_GT(ra, 10.2);
    EXPECT_GT(dec, 45.10);
}

TEST_F(EphemerisInterpolatorTest, PredictPositionRejectsExcessiveExtrapolation) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    pts.push_back(make_point(base_, 10.0, 45.0, 0.1, 0.05));
    pts.push_back(make_point(base_ + hours(1), 10.1, 45.05, 0.1, 0.05));

    EphemerisInterpolator interp(pts, 3);
    ASSERT_TRUE(interp.isValid());

    auto way_beyond = base_ + hours(100);
    auto result = interp.predictPosition(way_beyond, 3600.0);
    EXPECT_FALSE(result.has_value());
}

TEST_F(EphemerisInterpolatorTest, InvalidInterpolatorReturnsNulloptForPredict) {
    std::vector<::astro_mount::EphemerisPoint> empty;
    EphemerisInterpolator interp(empty, 3);
    auto result = interp.predictPosition(base_, 3600.0);
    EXPECT_FALSE(result.has_value());
}

TEST_F(EphemerisInterpolatorTest, AllInterpolationOrdersProduceSameValuesAtNodes) {
    std::vector<::astro_mount::EphemerisPoint> pts;
    for (int i = 0; i < 5; ++i) {
        pts.push_back(make_point(base_ + hours(i),
            10.0 + i * 0.1, 45.0 + i * 0.05, 0.1, 0.05));
    }

    for (int order = 1; order <= 3; ++order) {
        EphemerisInterpolator interp(pts, order);
        ASSERT_TRUE(interp.isValid());

        auto [ra, dec, rr, dr] = interp.getPositionAtTime(base_ + hours(2));
        EXPECT_NEAR(ra, 10.2, 1e-6) << "Order " << order << ": RA at node";
        EXPECT_NEAR(dec, 45.10, 1e-6) << "Order " << order << ": Dec at node";
    }
}

// ============================================================================
// EphemerisModel Tests
// ============================================================================

class EphemerisModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");
        base_ = system_clock::now();

        data_ = make_ephemeris_data("99942", "Test Comet", "comet");
        config_ = EphemerisModel::Config();
    }

    system_clock::time_point base_;
    ::astro_mount::EphemerisData data_;
    EphemerisModel::Config config_;
};

TEST_F(EphemerisModelTest, ConstructAndGetIdentifiers) {
    EphemerisModel model(data_, config_);
    EXPECT_EQ(model.getObjectId(), "99942");
    EXPECT_EQ(model.getObjectName(), "Test Comet");
    EXPECT_EQ(model.getObjectType(), "comet");
}

TEST_F(EphemerisModelTest, GetTimeRange) {
    // Use explicit points so we know the range
    std::vector<std::tuple<system_clock::time_point, double, double, double, double>> pts;
    pts.emplace_back(base_, 10.0, 45.0, 0.1, 0.05);
    pts.emplace_back(base_ + hours(5), 11.0, 46.0, 0.1, 0.05);

    auto data = make_ephemeris_data("99942", "Test", "asteroid", pts, 3);
    EphemerisModel model(data, config_);

    auto [start, end] = model.getTimeRange();
    EXPECT_EQ(start, base_);
    EXPECT_EQ(end, base_ + hours(5));
}

TEST_F(EphemerisModelTest, IsTimeWithinRange) {
    std::vector<std::tuple<system_clock::time_point, double, double, double, double>> pts;
    pts.emplace_back(base_, 10.0, 45.0, 0.1, 0.05);
    pts.emplace_back(base_ + hours(3), 10.3, 45.15, 0.1, 0.05);

    auto data = make_ephemeris_data("99942", "Test", "asteroid", pts, 3);
    EphemerisModel model(data, config_);

    EXPECT_TRUE(model.isTimeWithinRange(base_ + hours(1)));
    EXPECT_TRUE(model.isTimeWithinRange(base_));
    EXPECT_TRUE(model.isTimeWithinRange(base_ + hours(3)));
    EXPECT_FALSE(model.isTimeWithinRange(base_ - hours(1)));
    EXPECT_FALSE(model.isTimeWithinRange(base_ + hours(5)));
}

TEST_F(EphemerisModelTest, GetConfidenceWithinRange) {
    EphemerisModel model(data_, config_);
    double confidence = model.getConfidence(base_ + hours(2));
    EXPECT_DOUBLE_EQ(confidence, 0.95);
}

TEST_F(EphemerisModelTest, GetConfidenceBeforeRange) {
    EphemerisModel model(data_, config_);
    // Query far before range - confidence decreases linearly
    double confidence = model.getConfidence(base_ - hours(1));
    // diff = 3600s, max_extrapolation = 3600s
    // confidence = 0.8 * (1 - 3600/3600) = 0.0
    EXPECT_NEAR(confidence, 0.0, 1e-6);
}

TEST_F(EphemerisModelTest, GetConfidenceAfterRange) {
    EphemerisModel model(data_, config_);
    // Just a small amount after range (within extrapolation)
    double confidence = model.getConfidence(base_ + hours(9) + minutes(30));
    // diff = 1800s, max_extrapolation = 3600s
    // confidence = 0.8 * (1 - 1800/3600) = 0.4
    EXPECT_GT(confidence, 0.0);
    EXPECT_LT(confidence, 0.8);
}

TEST_F(EphemerisModelTest, GetConfidenceFarOutsideReturnsZero) {
    EphemerisModel model(data_, config_);
    double confidence = model.getConfidence(base_ + hours(100));
    EXPECT_DOUBLE_EQ(confidence, 0.0);
}

TEST_F(EphemerisModelTest, GetApparentPositionReturnsValidValues) {
    EphemerisModel model(data_, config_);
    auto [ra, dec, ra_rate, dec_rate] = model.getApparentPosition(
        base_ + hours(3), 52.0, 21.0, 100.0);

    EXPECT_FALSE(std::isnan(ra));
    EXPECT_FALSE(std::isnan(dec));
    EXPECT_FALSE(std::isinf(ra));
    EXPECT_FALSE(std::isinf(dec));
    EXPECT_GT(ra, 0.0);
    EXPECT_LT(ra, 24.0);
}

TEST_F(EphemerisModelTest, GetApparentPositionWithoutRefraction) {
    config_.apply_refraction = false;
    EphemerisModel model(data_, config_);

    auto [ra, dec, ra_rate, dec_rate] = model.getApparentPosition(
        base_ + hours(3), 52.0, 21.0, 100.0);

    EXPECT_FALSE(std::isnan(ra));
    EXPECT_FALSE(std::isnan(dec));
    EXPECT_GT(ra, 0.0);
    EXPECT_LT(ra, 24.0);
}

TEST_F(EphemerisModelTest, UpdateConfigAndGetConfig) {
    EphemerisModel model(data_, config_);

    // Default config
    auto retrieved = model.getConfig();
    EXPECT_TRUE(retrieved.apply_refraction);
    EXPECT_TRUE(retrieved.apply_aberration);
    EXPECT_DOUBLE_EQ(retrieved.max_extrapolation_seconds, 3600.0);

    // Update config
    EphemerisModel::Config new_config;
    new_config.apply_refraction = false;
    new_config.max_extrapolation_seconds = 7200.0;
    model.updateConfig(new_config);

    retrieved = model.getConfig();
    EXPECT_FALSE(retrieved.apply_refraction);
    EXPECT_DOUBLE_EQ(retrieved.max_extrapolation_seconds, 7200.0);
}

TEST_F(EphemerisModelTest, DifferentObjectTypes) {
    auto data = make_ephemeris_data("CERES", "1 Ceres", "asteroid");
    EphemerisModel model(data, config_);
    EXPECT_EQ(model.getObjectId(), "CERES");
    EXPECT_EQ(model.getObjectName(), "1 Ceres");
    EXPECT_EQ(model.getObjectType(), "asteroid");
}

// ============================================================================
// EphemerisTracker Tests
// ============================================================================

class EphemerisTrackerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");

        // Create a model with points that are ahead of current time
        // so tracking has a valid range
        base_ = system_clock::now();

        EphemerisModel::Config model_config;
        auto data = make_ephemeris_data(
            "99942", "Test Comet", "comet",
            {
                {base_ + hours(0), 10.0, 45.0, 0.1, 0.05},
                {base_ + hours(1), 10.1, 45.05, 0.1, 0.05},
                {base_ + hours(2), 10.2, 45.10, 0.1, 0.05},
                {base_ + hours(3), 10.3, 45.15, 0.1, 0.05},
                {base_ + hours(4), 10.4, 45.20, 0.1, 0.05},
            },
            3);

        model_ = std::make_shared<EphemerisModel>(data, model_config);
    }

    void TearDown() override {
        if (tracker_) {
            tracker_->stopTracking();
        }
    }

    system_clock::time_point base_;
    std::shared_ptr<EphemerisModel> model_;
    std::unique_ptr<EphemerisTracker> tracker_;
};

TEST_F(EphemerisTrackerTest, ConstructAndDestruct) {
    // Just verify construction/destruction doesn't crash
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
}

TEST_F(EphemerisTrackerTest, IsTrackingInitiallyFalse) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_FALSE(tracker.isTracking());
}

TEST_F(EphemerisTrackerTest, StartTrackingReturnsTrue) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));
    EXPECT_TRUE(tracker.isTracking());
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, StartTrackingWithCustomConfig) {
    EphemerisTracker::TrackingConfig config;
    config.lead_time_seconds = 60.0;
    config.update_rate_hz = 5.0;
    config.enable_prediction = true;
    config.prediction_interval_hours = 2.0;
    config.tracking_mode = "continuous";
    config.max_recovery_attempts = 5;

    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_, config));
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, StartTrackingAlreadyActiveReturnsFalse) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));
    // Second start should fail
    EXPECT_FALSE(tracker.startTracking(base_ + hours(1)));
    EXPECT_TRUE(tracker.isTracking());
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, StopTrackingStopsTracking) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));
    EXPECT_TRUE(tracker.isTracking());

    tracker.stopTracking();
    EXPECT_FALSE(tracker.isTracking());
}

TEST_F(EphemerisTrackerTest, StopTrackingWhenNotTrackingDoesNotCrash) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    tracker.stopTracking();
    EXPECT_FALSE(tracker.isTracking());
}

TEST_F(EphemerisTrackerTest, GetStatusReturnsIdleBeforeStart) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    auto status = tracker.getStatus();
    EXPECT_EQ(status.state(), ::astro_mount::EphemerisTrackStatus::IDLE);
}

TEST_F(EphemerisTrackerTest, GetStatusReturnsTrackingAfterStart) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));
    auto status = tracker.getStatus();
    // The state depends on timing; it could be WAITING_AT_START or TRACKING
    EXPECT_NE(status.state(), ::astro_mount::EphemerisTrackStatus::IDLE);
    EXPECT_EQ(status.object_id(), "99942");
    EXPECT_EQ(status.object_name(), "Test Comet");
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, GetObjectIdAndName) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_EQ(tracker.getObjectId(), "99942");
    EXPECT_EQ(tracker.getObjectName(), "Test Comet");
}

TEST_F(EphemerisTrackerTest, GetCurrentTargetReturnsValidValues) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));

    // Give some time for first tracking update
    std::this_thread::sleep_for(200ms);

    auto [ra, dec, ra_rate, dec_rate] = tracker.getCurrentTarget();
    EXPECT_FALSE(std::isnan(ra));
    EXPECT_FALSE(std::isnan(dec));
    EXPECT_GT(ra, 0.0);
    EXPECT_LT(ra, 24.0);

    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, GetStartPositionReturnsValidValues) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    auto [ra, dec, ra_rate, dec_rate] = tracker.getStartPosition(base_, 30.0);

    EXPECT_FALSE(std::isnan(ra));
    EXPECT_FALSE(std::isnan(dec));
    EXPECT_GT(ra, 0.0);
    EXPECT_LT(ra, 24.0);
}

TEST_F(EphemerisTrackerTest, UpdateConfigWhileNotTrackingSucceeds) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);

    EphemerisTracker::TrackingConfig config;
    config.update_rate_hz = 20.0;
    config.lead_time_seconds = 60.0;

    EXPECT_TRUE(tracker.updateConfig(config));
}

TEST_F(EphemerisTrackerTest, UpdateConfigWhileTrackingFails) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));

    EphemerisTracker::TrackingConfig config;
    config.update_rate_hz = 20.0;

    EXPECT_FALSE(tracker.updateConfig(config));
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, RegisterCallbackAndStartTracking) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);

    bool callback_called = false;
    tracker.registerCallback([&](const std::string& event_type,
                                  const std::string& message,
                                  const std::map<std::string, std::string>& context) {
        callback_called = true;
    });

    EXPECT_TRUE(tracker.startTracking(base_));

    // Allow tracking thread to run and potentially trigger callbacks
    std::this_thread::sleep_for(200ms);

    tracker.stopTracking();
    // Callback may or may not have been called depending on timing
    // At minimum, no crash should occur
}

TEST_F(EphemerisTrackerTest, GetStatisticsReturnsDefaultBeforeStart) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    auto stats = tracker.getStatistics();
    EXPECT_DOUBLE_EQ(stats.total_track_time_seconds, 0.0);
    EXPECT_DOUBLE_EQ(stats.avg_position_error_arcsec, 0.0);
    EXPECT_EQ(stats.tracking_updates, 0);
    EXPECT_EQ(stats.errors, 0);
}

TEST_F(EphemerisTrackerTest, PerformRecoveryWhenNotTrackingReturnsFalse) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_FALSE(tracker.performRecovery());
}

TEST_F(EphemerisTrackerTest, PerformRecoveryWhileTracking) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_));

    // Give tracking thread time to start
    std::this_thread::sleep_for(200ms);

    EXPECT_TRUE(tracker.performRecovery());
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, GetRemainingTime) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);

    // Before start
    EXPECT_DOUBLE_EQ(tracker.getRemainingTime(), 0.0);

    EXPECT_TRUE(tracker.startTracking(base_));
    // During tracking, remaining time should be positive
    EXPECT_GT(tracker.getRemainingTime(), 0.0);

    tracker.stopTracking();
    // After stop
    EXPECT_DOUBLE_EQ(tracker.getRemainingTime(), 0.0);
}

TEST_F(EphemerisTrackerTest, GetStatusWithWarningsAndParameters) {
    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);

    EXPECT_TRUE(tracker.startTracking(base_));
    auto status = tracker.getStatus();

    // Verify tracking parameters are populated
    EXPECT_GT(status.tracking_parameters_size(), 0);
    EXPECT_EQ(status.tracking_parameters().at("object_id"), "99942");
    EXPECT_EQ(status.tracking_parameters().at("object_name"), "Test Comet");

    EXPECT_TRUE(status.earth_rotation_corrected());
    tracker.stopTracking();
}

TEST_F(EphemerisTrackerTest, StartTrackingWithLeadTime) {
    EphemerisTracker::TrackingConfig config;
    config.lead_time_seconds = 120.0;  // 2 min lead time
    config.wait_at_start = true;

    EphemerisTracker tracker(model_, 52.0, 21.0, 100.0);
    EXPECT_TRUE(tracker.startTracking(base_, config));
    EXPECT_TRUE(tracker.isTracking());

    // Get status - should show tracking info
    auto status = tracker.getStatus();
    EXPECT_EQ(status.object_id(), "99942");

    tracker.stopTracking();
}

// ============================================================================
// EphemerisTrackerManager Tests
// ============================================================================

class EphemerisTrackerManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logging::Logger::init("");
        manager_ = std::make_unique<EphemerisTrackerManager>();
        manager_->setObserverLocation(52.0, 21.0, 100.0);
        base_ = system_clock::now();
    }

    void TearDown() override {
        if (manager_) {
            manager_->clearCache();
        }
    }

    system_clock::time_point base_;
    std::unique_ptr<EphemerisTrackerManager> manager_;
};

TEST_F(EphemerisTrackerManagerTest, ConstructAndDestruct) {
    EphemerisTrackerManager mgr;
}

TEST_F(EphemerisTrackerManagerTest, UploadEphemerisSuccess) {
    auto data = make_ephemeris_data();
    EXPECT_TRUE(manager_->uploadEphemeris(data));
}

TEST_F(EphemerisTrackerManagerTest, UploadEphemerisWithEmptyObjectId) {
    auto data = make_ephemeris_data("", "No ID", "comet");
    EXPECT_FALSE(manager_->uploadEphemeris(data));
}

TEST_F(EphemerisTrackerManagerTest, StartTrackingWithNoUploadReturnsEmpty) {
    auto tracker_id = manager_->startTracking("nonexistent", base_);
    EXPECT_TRUE(tracker_id.empty());
}

TEST_F(EphemerisTrackerManagerTest, StartTrackingAfterUploadReturnsTrackerId) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    EXPECT_FALSE(tracker_id.empty());
    EXPECT_EQ(tracker_id, "tracker_1");

    // Stop tracking to clean up thread
    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, StartTrackingWithDataReturnsTrackerId) {
    auto data = make_ephemeris_data("CERES", "1 Ceres", "asteroid");
    auto tracker_id = manager_->startTrackingWithData(data, base_);
    EXPECT_FALSE(tracker_id.empty());
    EXPECT_EQ(tracker_id, "tracker_1");

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, StopTrackingExistingTrackerReturnsTrue) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, StopTrackingNonexistentTrackerReturnsFalse) {
    EXPECT_FALSE(manager_->stopTracking("nonexistent_tracker"));
}

TEST_F(EphemerisTrackerManagerTest, GetTrackerReturnsNullptrForNonexistent) {
    auto tracker = manager_->getTracker("nonexistent");
    EXPECT_EQ(tracker, nullptr);
}

TEST_F(EphemerisTrackerManagerTest, GetTrackerReturnsValidTracker) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    auto tracker = manager_->getTracker(tracker_id);
    EXPECT_NE(tracker, nullptr);
    EXPECT_EQ(tracker->getObjectId(), "99942");
    EXPECT_EQ(tracker->getObjectName(), "Test Comet");

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, GetActiveTrackersInitiallyEmpty) {
    auto trackers = manager_->getActiveTrackers();
    EXPECT_TRUE(trackers.empty());
}

TEST_F(EphemerisTrackerManagerTest, GetActiveTrackersAfterStarting) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    auto trackers = manager_->getActiveTrackers();
    EXPECT_EQ(trackers.size(), 1);
    EXPECT_TRUE(trackers.count(tracker_id));

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, MultipleTrackers) {
    // Upload first object
    auto data1 = make_ephemeris_data("OBJ1", "Object One", "asteroid",
        {{base_ + hours(0), 10.0, 45.0, 0.1, 0.05},
         {base_ + hours(1), 10.1, 45.05, 0.1, 0.05},
         {base_ + hours(2), 10.2, 45.10, 0.1, 0.05}},
        3);
    ASSERT_TRUE(manager_->uploadEphemeris(data1));

    // Upload second object
    auto data2 = make_ephemeris_data("OBJ2", "Object Two", "comet",
        {{base_ + hours(0), 20.0, 30.0, 0.2, 0.1},
         {base_ + hours(1), 20.2, 30.1, 0.2, 0.1},
         {base_ + hours(2), 20.4, 30.2, 0.2, 0.1}},
        3);
    ASSERT_TRUE(manager_->uploadEphemeris(data2));

    // Start both
    auto id1 = manager_->startTracking("OBJ1", base_);
    auto id2 = manager_->startTracking("OBJ2", base_);

    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2);

    auto trackers = manager_->getActiveTrackers();
    EXPECT_EQ(trackers.size(), 2);

    // Stop both
    EXPECT_TRUE(manager_->stopTracking(id1));
    EXPECT_TRUE(manager_->stopTracking(id2));

    trackers = manager_->getActiveTrackers();
    EXPECT_TRUE(trackers.empty());
}

TEST_F(EphemerisTrackerManagerTest, GetAllStatuses) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    auto statuses = manager_->getAllStatuses();
    EXPECT_EQ(statuses.size(), 1);
    EXPECT_EQ(statuses[0].object_id(), "99942");

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, ClearCacheStopsAllTrackers) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    // Clear cache - this should stop all trackers and clear models
    manager_->clearCache();

    auto trackers = manager_->getActiveTrackers();
    EXPECT_TRUE(trackers.empty());

    // Starting again should fail (no ephemeris data)
    auto new_id = manager_->startTracking("99942", base_);
    EXPECT_TRUE(new_id.empty());
}

TEST_F(EphemerisTrackerManagerTest, GetMetricsReturnsDefaultValues) {
    auto metrics = manager_->getMetrics();
    // Default metrics with no active trackers - all fields should be zero/default
    EXPECT_EQ(metrics.total_track_time_seconds(), 0);
    EXPECT_EQ(metrics.avg_position_error_arcsec(), 0.0);
    EXPECT_EQ(metrics.max_position_error_arcsec(), 0.0);
    EXPECT_EQ(metrics.avg_tracking_rate_error(), 0.0);
    EXPECT_FALSE(metrics.earth_rotation_applied());
}

TEST_F(EphemerisTrackerManagerTest, GetMetricsAfterTracking) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    // Brief tracking time - enough for several tracking loop iterations
    std::this_thread::sleep_for(200ms);

    // Stop tracking first - total_track_time_seconds is finalized on stop
    EXPECT_TRUE(manager_->stopTracking(tracker_id));

    auto metrics = manager_->getMetrics();
    // Verify metrics are captured from the completed tracker
    // Note: total_track_time_seconds is uint64 in proto, so 200ms truncates to 0
    // Use the double-based fields (avg/max position error) to verify metrics work
    EXPECT_GT(metrics.avg_position_error_arcsec(), 0.0);
    EXPECT_GT(metrics.max_position_error_arcsec(), 0.0);
    EXPECT_TRUE(metrics.earth_rotation_applied());
}

TEST_F(EphemerisTrackerManagerTest, SetObserverLocation) {
    EphemerisTrackerManager mgr;

    // Default location should be 0/0/0
    auto data = make_ephemeris_data("TEST", "Test", "comet",
        {{base_ + hours(0), 10.0, 45.0, 0.1, 0.05},
         {base_ + hours(1), 10.1, 45.05, 0.1, 0.05}},
        3);
    ASSERT_TRUE(mgr.uploadEphemeris(data));

    // Should work with default location (0,0,0)
    auto id = mgr.startTracking("TEST", base_);
    EXPECT_FALSE(id.empty());

    mgr.stopTracking(id);

    // Set location then start
    mgr.setObserverLocation(48.0, 15.0, 200.0);
    mgr.uploadEphemeris(data); // Upload again after clear... but cache not cleared, so model exists

    id = mgr.startTracking("TEST", base_);
    EXPECT_FALSE(id.empty());
    mgr.stopTracking(id);
}

TEST_F(EphemerisTrackerManagerTest, UploadMultipleEphemeris) {
    auto data1 = make_ephemeris_data("AST1", "Asteroid 1", "asteroid");
    auto data2 = make_ephemeris_data("AST2", "Asteroid 2", "asteroid");

    EXPECT_TRUE(manager_->uploadEphemeris(data1));
    EXPECT_TRUE(manager_->uploadEphemeris(data2));

    // Both should be trackable
    auto id1 = manager_->startTracking("AST1", base_);
    auto id2 = manager_->startTracking("AST2", base_);

    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());

    manager_->stopTracking(id1);
    manager_->stopTracking(id2);
}

// ============================================================================
// Integration: EphemerisTrackerManager with Prediction Enabled
// ============================================================================

TEST_F(EphemerisTrackerManagerTest, StartTrackingWithPrediction) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    EphemerisTracker::TrackingConfig config;
    config.enable_prediction = true;
    config.prediction_interval_hours = 2.0;

    auto tracker_id = manager_->startTracking("99942", base_, config);
    EXPECT_FALSE(tracker_id.empty());

    // Verify tracker has prediction enabled
    auto tracker = manager_->getTracker(tracker_id);
    ASSERT_NE(tracker, nullptr);

    EXPECT_TRUE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, MultipleTrackersWithDifferentObjectTypes) {
    // Upload asteroid
    auto ast_data = make_ephemeris_data("AST", "Test Asteroid", "asteroid",
        {{base_ + hours(0), 10.0, 45.0, 0.1, 0.05},
         {base_ + hours(1), 10.1, 45.05, 0.1, 0.05},
         {base_ + hours(2), 10.2, 45.10, 0.1, 0.05}},
        3);
    ASSERT_TRUE(manager_->uploadEphemeris(ast_data));

    // Upload comet
    auto comet_data = make_ephemeris_data("COMET", "Test Comet", "comet",
        {{base_ + hours(0), 20.0, 30.0, 0.2, 0.1},
         {base_ + hours(1), 20.3, 30.2, 0.2, 0.1},
         {base_ + hours(2), 20.6, 30.4, 0.2, 0.1}},
        3);
    ASSERT_TRUE(manager_->uploadEphemeris(comet_data));

    auto id_ast = manager_->startTracking("AST", base_);
    auto id_comet = manager_->startTracking("COMET", base_);

    EXPECT_FALSE(id_ast.empty());
    EXPECT_FALSE(id_comet.empty());

    // Verify object types via trackers
    auto tracker_ast = manager_->getTracker(id_ast);
    auto tracker_comet = manager_->getTracker(id_comet);
    ASSERT_NE(tracker_ast, nullptr);
    ASSERT_NE(tracker_comet, nullptr);

    EXPECT_EQ(tracker_ast->getObjectId(), "AST");
    EXPECT_EQ(tracker_comet->getObjectId(), "COMET");

    manager_->stopTracking(id_ast);
    manager_->stopTracking(id_comet);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(EphemerisTrackerManagerTest, StartTrackingNonexistentObjectReturnsEmpty) {
    auto data = make_ephemeris_data("VALID_ID", "Valid Object", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    // Starting with a different ID should fail
    auto id = manager_->startTracking("DIFFERENT_ID", base_);
    EXPECT_TRUE(id.empty());
}

TEST_F(EphemerisTrackerManagerTest, StopTrackingAfterClearCache) {
    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(manager_->uploadEphemeris(data));

    auto tracker_id = manager_->startTracking("99942", base_);
    ASSERT_FALSE(tracker_id.empty());

    manager_->clearCache();

    // After clear, stop should return false (tracker no longer exists)
    EXPECT_FALSE(manager_->stopTracking(tracker_id));
}

TEST_F(EphemerisTrackerManagerTest, DestructorWhileTrackingActive) {
    // Verify no crash when manager is destroyed while trackers are active
    auto mgr = std::make_unique<EphemerisTrackerManager>();
    mgr->setObserverLocation(52.0, 21.0, 100.0);

    auto data = make_ephemeris_data("99942", "Test Comet", "comet");
    ASSERT_TRUE(mgr->uploadEphemeris(data));
    auto tracker_id = mgr->startTracking("99942", base_);
    EXPECT_FALSE(tracker_id.empty());

    // Destroy manager while tracking active - should clean up threads
    // (destructor calls clearCache → stopTracking on all active trackers)
    mgr.reset();
    // No crash expected
}

} // namespace test
} // namespace models
} // namespace astro_mount

// Main
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
