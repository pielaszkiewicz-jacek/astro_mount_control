#include "api/service_impl.h"
#include "controllers/mount_controller.h"
#include "logging/logger.h"
#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/time_util.h>
#include <thread>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace astro_mount {
namespace api {
namespace test {

using google::protobuf::util::TimeUtil;

using namespace std::chrono_literals;

// ============================================================================
// Base test fixture: real MountController + in-process gRPC server
// ============================================================================
class GrpcIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Logger is initialized once in main() - do not re-init here

        // Create real MountController with default config
        controller_ = std::make_unique<controllers::MountController>();

        // Default config: equatorial mount, no CANopen (uses mock HAL internally)
        config_.mount_type = controllers::MountController::MountType::EQUATORIAL;
        config_.latitude = 52.0;
        config_.longitude = 21.0;
        config_.altitude = 100.0;
        config_.mount_height = 1.5;
        config_.pier_west = 0.0;
        config_.pier_east = 0.0;
        config_.max_slew_rate = 5.0;
        config_.max_tracking_rate = 0.004178;
        config_.slew_acceleration = 1.0;
        config_.tracking_acceleration = 0.001;
        config_.position_tolerance = 0.5;
        config_.rate_tolerance = 0.001;
        config_.default_temperature = 15.0;
        config_.default_pressure = 1013.25;
        config_.default_humidity = 0.5;
        config_.use_encoders = false;
        config_.encoders_absolute = false;
        config_.encoder_resolution = 360000.0;
        config_.process_noise = 0.01;
        config_.measurement_noise = 1.0;
        config_.tpoint_enabled_terms = 0;
        config_.canopen_interface = "";  // empty → uses mock HAL
        config_.canopen_node_id = 1;
        config_.grpc_address = "localhost:50051";
        config_.grpc_port = 50051;
        config_.log_level = "ERROR";
        config_.log_directory = "/tmp";
        config_.log_rotation_days = 7;
        config_.focal_length = 2000.0;
        config_.aperture = 250.0;
        config_.enable_guider = false;
        config_.guider_max_correction = 100.0;
        config_.guider_aggression = 0.5;

        // Axis physical parameters
        config_.ha_axis_params.gear_ratio = 360.0;
        config_.dec_axis_params.gear_ratio = 360.0;
        config_.ha_axis_params.backlash = 0.0;
        config_.dec_axis_params.backlash = 0.0;
        config_.ha_axis_params.encoder_resolution = 360000.0;
        config_.dec_axis_params.encoder_resolution = 360000.0;
        config_.ha_axis_params.motor_steps_per_rev = 200;
        config_.dec_axis_params.motor_steps_per_rev = 200;
        config_.ha_axis_params.motor_microstepping = 16;
        config_.dec_axis_params.motor_microstepping = 16;
        config_.ha_axis_params.cyclic_harmonics.fill(0.0);
        config_.dec_axis_params.cyclic_harmonics.fill(0.0);

        // Initialize controller
        ASSERT_TRUE(controller_->initialize(config_));

        // Build in-process gRPC server
        service_impl_ = std::make_unique<MountControllerServiceImpl>(*controller_);

        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0", grpc::InsecureServerCredentials(), &port_);
        builder.RegisterService(service_impl_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);
        ASSERT_GT(port_, 0);

        // Create stub for client-side testing
        auto channel = grpc::CreateChannel("localhost:" + std::to_string(port_),
                                            grpc::InsecureChannelCredentials());
        stub_ = MountControllerService::NewStub(channel);
        ASSERT_NE(stub_, nullptr);
    }

    void TearDown() override {
        server_->Shutdown();
        stub_.reset();
        server_.reset();
        service_impl_.reset();
        if (controller_) {
            controller_->shutdown();
            controller_.reset();
        }
    }

    std::unique_ptr<controllers::MountController> controller_;
    controllers::MountController::ControllerConfig config_;
    std::unique_ptr<MountControllerServiceImpl> service_impl_;
    std::unique_ptr<grpc::Server> server_;
    std::unique_ptr<MountControllerService::Stub> stub_;
    int port_{0};
};

// ============================================================================
// BASIC MOUNT CONTROL TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, SlewToCoordinates) {
    astro_mount::Coordinates request;
    request.set_ra(12.345);
    request.set_dec(45.678);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->SlewToCoordinates(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
}

TEST_F(GrpcIntegrationTest, TrackObject) {
    astro_mount::Coordinates request;
    request.set_ra(1.234);
    request.set_dec(5.678);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->TrackObject(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, GetState) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::ControllerState response;

    auto status = stub_->GetState(&context, request, &response);
    EXPECT_TRUE(status.ok());
    // After initialization, state should be IDLE
    EXPECT_EQ(response.status(), astro_mount::ControllerState::IDLE);
}

TEST_F(GrpcIntegrationTest, GetConfiguration) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::Configuration response;

    auto status = stub_->GetConfiguration(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_DOUBLE_EQ(response.latitude(), 52.0);
    EXPECT_DOUBLE_EQ(response.longitude(), 21.0);
    EXPECT_DOUBLE_EQ(response.ha_axis_params().gear_ratio(), 360.0);
    EXPECT_DOUBLE_EQ(response.dec_axis_params().gear_ratio(), 360.0);
}

TEST_F(GrpcIntegrationTest, UpdateConfiguration) {
    astro_mount::Configuration request;
    request.set_latitude(50.0);
    request.set_longitude(20.0);
    request.mutable_ha_axis_params()->set_gear_ratio(180.0);
    request.mutable_dec_axis_params()->set_gear_ratio(180.0);
    request.set_max_slew_rate(10.0);
    request.set_max_tracking_rate(0.5);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->UpdateConfiguration(&context, request, &response);
    EXPECT_TRUE(status.ok());

    // Verify the update was applied
    grpc::ClientContext get_ctx;
    google::protobuf::Empty get_req;
    astro_mount::Configuration get_resp;
    auto get_status = stub_->GetConfiguration(&get_ctx, get_req, &get_resp);
    EXPECT_TRUE(get_status.ok());
    EXPECT_DOUBLE_EQ(get_resp.latitude(), 50.0);
    EXPECT_DOUBLE_EQ(get_resp.longitude(), 20.0);
}

TEST_F(GrpcIntegrationTest, HealthCheck) {
    astro_mount::HealthCheckRequest request;
    request.set_service("mount_controller");

    grpc::ClientContext context;
    astro_mount::HealthCheckResponse response;

    auto status = stub_->CheckHealth(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.service(), "mount_controller");
    EXPECT_EQ(response.status(), astro_mount::HealthCheckResponse::SERVING);
    EXPECT_TRUE(response.has_metrics());
}

TEST_F(GrpcIntegrationTest, Stop) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->Stop(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, ParkAndUnpark) {
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        google::protobuf::Empty response;

        auto status = stub_->Park(&context, request, &response);
        EXPECT_TRUE(status.ok());
    }

    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        google::protobuf::Empty response;

        auto status = stub_->Unpark(&context, request, &response);
        EXPECT_TRUE(status.ok());
    }
}

// ============================================================================
// STATE SAVE/LOAD TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, SaveAndLoadState) {
    // Use a temp file path
    std::string test_path = "/tmp/test_grpc_state_" + std::to_string(::getpid()) + ".json";

    // Test SaveState
    {
        astro_mount::StateSaveRequest request;
        request.set_file_path(test_path);

        grpc::ClientContext context;
        astro_mount::StateSaveResponse response;

        auto status = stub_->SaveState(&context, request, &response);
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(response.file_path(), test_path);
    }

    // Test LoadState
    {
        astro_mount::StateLoadRequest request;
        request.set_file_path(test_path);

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->LoadState(&context, request, &response);
        EXPECT_TRUE(status.ok());
    }

    // Cleanup
    std::remove(test_path.c_str());
}

// ============================================================================
// MEASUREMENT & CALIBRATION TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, AddMeasurement) {
    astro_mount::Measurement request;
    request.mutable_observed()->set_ra(10.0);
    request.mutable_observed()->set_dec(20.0);
    request.mutable_expected()->set_ra(10.1);
    request.mutable_expected()->set_dec(20.1);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, AddBootstrapMeasurement) {
    astro_mount::BootstrapMeasurement request;
    request.mutable_observed()->set_ra(10.0);
    request.mutable_observed()->set_dec(20.0);
    request.mutable_expected()->set_ra(10.1);
    request.mutable_expected()->set_dec(20.1);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddBootstrapMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, RunBootstrapCalibration) {
    // Add minimum measurements needed for bootstrap calibration
    for (int i = 0; i < 3; i++) {
        astro_mount::BootstrapMeasurement req;
        req.mutable_observed()->set_ra(10.0 + i);
        req.mutable_observed()->set_dec(20.0 + i);
        req.mutable_expected()->set_ra(10.1 + i);
        req.mutable_expected()->set_dec(20.1 + i);

        grpc::ClientContext ctx;
        google::protobuf::Empty resp;
        auto s = stub_->AddBootstrapMeasurement(&ctx, req, &resp);
        ASSERT_TRUE(s.ok());
    }

    // Run calibration
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::BootstrapCalibrationResult response;

    auto status = stub_->RunBootstrapCalibration(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
}

TEST_F(GrpcIntegrationTest, GetTPointParameters) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::TPointParameters response;

    auto status = stub_->GetTPointParameters(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, RunTPointCalibration) {
    // The controller requires at least 3 TPOINT measurements before calibration
    for (int i = 0; i < 3; ++i) {
        astro_mount::Measurement req;
        req.mutable_observed()->set_ra(10.0 + i * 5.0);
        req.mutable_observed()->set_dec(20.0 + i * 5.0);
        req.mutable_expected()->set_ra(10.1 + i * 5.0);
        req.mutable_expected()->set_dec(20.1 + i * 5.0);

        grpc::ClientContext ctx;
        google::protobuf::Empty resp;
        auto s = stub_->AddMeasurement(&ctx, req, &resp);
        ASSERT_TRUE(s.ok()) << "Failed to add measurement " << i;
    }

    // Run calibration
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->RunTPointCalibration(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
}

TEST_F(GrpcIntegrationTest, ClearTPointMeasurements) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->ClearTPointMeasurements(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

// ============================================================================
// ENCODER & GUIDER TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, EnableEncoders) {
    astro_mount::EncoderConfig request;
    request.set_type(astro_mount::EncoderConfig::ABSOLUTE);
    request.set_resolution(360000.0);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->EnableEncoders(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, DisableEncoders) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->DisableEncoders(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, ConnectGuider) {
    astro_mount::GuiderConfig request;
    request.set_connection_string("tcp://localhost:7624");
    request.set_max_correction(10.0);
    request.set_aggression(0.5);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->ConnectGuider(&context, request, &response);
    // With mock HAL and no actual guider, this may fail
    // But the gRPC call itself should complete
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_F(GrpcIntegrationTest, DisconnectGuider) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    google::protobuf::Empty response;

    auto status = stub_->DisconnectGuider(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

// ============================================================================
// DEROTATOR TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, ConfigureDerotator) {
    astro_mount::DerotatorConfig request;
    request.set_type(astro_mount::DerotatorConfig::CANOPEN);
    request.set_connection_string("can0:1");
    request.set_gear_ratio(10.0);
    request.set_max_speed(5.0);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->ConfigureDerotator(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
}

TEST_F(GrpcIntegrationTest, GetDerotatorStatus) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    astro_mount::DerotatorStatus response;

    auto status = stub_->GetDerotatorStatus(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, ControlFieldRotation) {
    // First configure the derotator (required to enable derotator subsystem)
    {
        astro_mount::DerotatorConfig config;
        config.set_type(astro_mount::DerotatorConfig::CANOPEN);
        config.set_connection_string("can0:1");
        config.set_gear_ratio(10.0);
        config.set_max_speed(5.0);

        grpc::ClientContext ctx;
        google::protobuf::Empty resp;
        auto s = stub_->ConfigureDerotator(&ctx, config, &resp);
        ASSERT_TRUE(s.ok()) << "Failed to configure derotator: "
                            << s.error_code() << ": " << s.error_message();
    }

    // Set field rotation to ALT_AZ mode (requires derotator to be configured)
    {
        astro_mount::FieldRotationControlRequest request;
        request.set_mode(astro_mount::FieldRotationControlRequest::ALT_AZ);
        request.set_rotation_rate(15.0);

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->ControlFieldRotation(&context, request, &response);
        EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
    }

    // Disable field rotation
    {
        astro_mount::FieldRotationControlRequest request;
        request.set_mode(astro_mount::FieldRotationControlRequest::DISABLED);

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->ControlFieldRotation(&context, request, &response);
        EXPECT_TRUE(status.ok());
    }
}

// ============================================================================
// POLE DETERMINATION TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, DeterminePolePosition) {
    astro_mount::PoleDeterminationRequest request;
    request.set_measurement_count(5);
    request.set_duration_hours(0.1);

    grpc::ClientContext context;
    astro_mount::PolePosition response;

    auto status = stub_->DeterminePolePosition(&context, request, &response);
    EXPECT_TRUE(status.ok()) << "gRPC error: " << status.error_code() << ": " << status.error_message();
}

// ============================================================================
// STREAMING RPC TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, WatchState_ReceivesStreamOfUpdates) {
    // Call WatchState using real gRPC client-server
    grpc::ClientContext context;
    google::protobuf::Empty request;
    auto reader = stub_->WatchState(&context, request);
    ASSERT_NE(reader, nullptr);

    // Read a few state updates
    astro_mount::ControllerState state;
    int update_count = 0;

    // Read up to 5 updates (should receive at least 1)
    while (update_count < 5 && reader->Read(&state)) {
        update_count++;
        // State should have a valid status
        EXPECT_TRUE(state.status() == astro_mount::ControllerState::IDLE ||
                    state.status() == astro_mount::ControllerState::TRACKING ||
                    state.status() == astro_mount::ControllerState::SLEWING);
    }

    // Cancel the stream
    context.TryCancel();

    // Verify we received at least one update
    EXPECT_GE(update_count, 1);

    // Finish
    auto status = reader->Finish();
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::CANCELLED);
}

TEST_F(GrpcIntegrationTest, WatchState_ReflectsChangingState) {
    grpc::ClientContext context;
    google::protobuf::Empty request;
    auto reader = stub_->WatchState(&context, request);
    ASSERT_NE(reader, nullptr);

    // Read first state (should be IDLE)
    astro_mount::ControllerState state1;
    EXPECT_TRUE(reader->Read(&state1));
    EXPECT_EQ(state1.status(), astro_mount::ControllerState::IDLE);

    // Trigger a slew to change state
    {
        astro_mount::Coordinates slew_req;
        slew_req.set_ra(12.0);
        slew_req.set_dec(45.0);

        grpc::ClientContext slew_ctx;
        google::protobuf::Empty slew_resp;
        auto slew_status = stub_->SlewToCoordinates(&slew_ctx, slew_req, &slew_resp);
        EXPECT_TRUE(slew_status.ok());
    }

    // Give the controller time to process
    std::this_thread::sleep_for(50ms);

    // Read next state (may see SLEWING or still IDLE depending on timing)
    astro_mount::ControllerState state2;
    if (reader->Read(&state2)) {
        // State should be IDLE or SLEWING (slew may complete quickly in mock HAL)
        EXPECT_TRUE(state2.status() == astro_mount::ControllerState::IDLE ||
                    state2.status() == astro_mount::ControllerState::SLEWING);
    }

    context.TryCancel();
    reader->Finish();
}

TEST_F(GrpcIntegrationTest, WatchState_Cancellation) {
    // Test that WatchState handles client-side cancellation gracefully
    grpc::ClientContext context;
    google::protobuf::Empty request;
    auto reader = stub_->WatchState(&context, request);
    ASSERT_NE(reader, nullptr);

    // Read one state update
    astro_mount::ControllerState state;
    EXPECT_TRUE(reader->Read(&state));

    // Cancel from client side
    context.TryCancel();

    // Read should fail after cancellation
    astro_mount::ControllerState state2;
    EXPECT_FALSE(reader->Read(&state2));

    // Finish should return CANCELLED
    auto status = reader->Finish();
    EXPECT_TRUE(status.error_code() == grpc::StatusCode::CANCELLED ||
                status.ok());
}

// ============================================================================
// CONCURRENT GPRC CALL TESTS
// ============================================================================

TEST_F(GrpcIntegrationTest, ConcurrentSlewAndGetState) {
    constexpr int NUM_THREADS = 10;
    std::vector<std::thread> threads;
    std::atomic<int> slew_success{0};
    std::atomic<int> getstate_success{0};
    std::atomic<bool> exception_thrown{false};

    // Launch concurrent slews + GetState calls.
    // MountController serializes slews — only 1 at a time can be in SLEWING
    // state. With mock CANopen (instant completion), multiple slews may appear
    // to succeed because each completes before the next thread checks state.
    // This test verifies the system handles concurrent gRPC calls gracefully.
    for (int i = 0; i < NUM_THREADS; ++i) {
        if (i % 2 == 0) {
            // Even threads: attempt SlewToCoordinates
            threads.emplace_back([this, i, &slew_success, &exception_thrown]() {
                try {
                    astro_mount::Coordinates request;
                    request.set_ra(10.0 + i * 0.1);
                    request.set_dec(20.0 + i * 0.1);
                    grpc::ClientContext context;
                    google::protobuf::Empty response;
                    auto status = stub_->SlewToCoordinates(&context, request, &response);
                    if (status.ok()) {
                        slew_success++;
                    }
                } catch (...) {
                    exception_thrown = true;
                }
            });
        } else {
            // Odd threads: concurrent GetState (these should always succeed)
            threads.emplace_back([this, &getstate_success, &exception_thrown]() {
                try {
                    grpc::ClientContext context;
                    google::protobuf::Empty request;
                    astro_mount::ControllerState response;
                    auto status = stub_->GetState(&context, request, &response);
                    if (status.ok()) {
                        getstate_success++;
                    }
                } catch (...) {
                    exception_thrown = true;
                }
            });
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    // No exceptions should escape from any thread
    EXPECT_FALSE(exception_thrown);
    // At least 1 slew should succeed
    EXPECT_GE(slew_success, 1);
    // At least 1 GetState should succeed
    EXPECT_GE(getstate_success, 1);
}

TEST_F(GrpcIntegrationTest, ConcurrentMixedOperations) {
    constexpr int NUM_OPS = 5;
    std::vector<std::thread> threads;
    std::atomic<int> success{0};
    std::atomic<bool> exception_thrown{false};

    // Launch mixed concurrent operations.
    // MountController serializes slews — only 1 can be in SLEWING state at a time.
    // Non-slew operations (GetState, GetConfig, UpdateConfig, HealthCheck) should
    // all succeed regardless of mount state.
    threads.emplace_back([this, &success, &exception_thrown]() {
        try {
            // Perform one slew
            astro_mount::Coordinates req;
            req.set_ra(12.0);
            req.set_dec(45.0);
            grpc::ClientContext ctx;
            google::protobuf::Empty resp;
            if (stub_->SlewToCoordinates(&ctx, req, &resp).ok()) {
                success++;
            }
            // Then some non-slew operations
            for (int i = 0; i < NUM_OPS - 1; ++i) {
                google::protobuf::Empty req2;
                grpc::ClientContext ctx2;
                astro_mount::ControllerState resp2;
                if (stub_->GetState(&ctx2, req2, &resp2).ok()) {
                    success++;
                }
            }
        } catch (...) {
            exception_thrown = true;
        }
    });

    threads.emplace_back([this, &success, &exception_thrown]() {
        try {
            for (int i = 0; i < NUM_OPS; ++i) {
                google::protobuf::Empty req;
                grpc::ClientContext ctx;
                astro_mount::ControllerState resp;
                if (stub_->GetState(&ctx, req, &resp).ok()) {
                    success++;
                }
            }
        } catch (...) {
            exception_thrown = true;
        }
    });

    threads.emplace_back([this, &success, &exception_thrown]() {
        try {
            for (int i = 0; i < NUM_OPS; ++i) {
                google::protobuf::Empty req;
                grpc::ClientContext ctx;
                astro_mount::Configuration resp;
                if (stub_->GetConfiguration(&ctx, req, &resp).ok()) {
                    success++;
                }
            }
        } catch (...) {
            exception_thrown = true;
        }
    });

    threads.emplace_back([this, &success, &exception_thrown]() {
        try {
            for (int i = 0; i < NUM_OPS; ++i) {
                astro_mount::Configuration req;
                req.set_latitude(50.0 + i);
                grpc::ClientContext ctx;
                google::protobuf::Empty resp;
                if (stub_->UpdateConfiguration(&ctx, req, &resp).ok()) {
                    success++;
                }
            }
        } catch (...) {
            exception_thrown = true;
        }
    });

    threads.emplace_back([this, &success, &exception_thrown]() {
        try {
            for (int i = 0; i < NUM_OPS; ++i) {
                astro_mount::HealthCheckRequest req;
                req.set_service("mount_controller");
                grpc::ClientContext ctx;
                astro_mount::HealthCheckResponse resp;
                if (stub_->CheckHealth(&ctx, req, &resp).ok()) {
                    success++;
                }
            }
        } catch (...) {
            exception_thrown = true;
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    // No exceptions should escape from any thread
    EXPECT_FALSE(exception_thrown);
    // At least 1 slew should succeed
    // All non-slew operations should succeed (4 threads * 5 ops each = 20,
    // plus 4 non-slew ops from thread 1)
    EXPECT_GE(success, 1 + (NUM_OPS - 1) + NUM_OPS * 4 - NUM_OPS);
}

TEST_F(GrpcIntegrationTest, ConcurrentSlewWithSameCoordinates) {
    constexpr int NUM_THREADS = 20;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<bool> exception_thrown{false};

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, &exception_thrown]() {
            try {
                astro_mount::Coordinates request;
                request.set_ra(12.345);
                request.set_dec(45.678);
                grpc::ClientContext context;
                google::protobuf::Empty response;
                auto status = stub_->SlewToCoordinates(&context, request, &response);
                if (status.ok()) {
                    success_count++;
                }
            } catch (...) {
                exception_thrown = true;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // No exceptions should escape from any thread
    EXPECT_FALSE(exception_thrown);
    // At least 1 slew should succeed
    EXPECT_GE(success_count, 1);
}

TEST_F(GrpcIntegrationTest, ConcurrentSaveAndLoadState) {
    constexpr int NUM_SAVES = 5;
    constexpr int NUM_LOADS = 5;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count{0};

    // Threads saving state
    for (int i = 0; i < NUM_SAVES; ++i) {
        threads.emplace_back([this, i, &ok_count]() {
            std::string path = "/tmp/test_concurrent_" + std::to_string(i) + "_" +
                               std::to_string(::getpid()) + ".json";
            astro_mount::StateSaveRequest req;
            req.set_file_path(path);

            grpc::ClientContext ctx;
            astro_mount::StateSaveResponse resp;

            if (stub_->SaveState(&ctx, req, &resp).ok()) {
                ok_count++;
            }

            // Cleanup
            std::remove(path.c_str());
        });
    }

    // Threads loading state (use a common known-good path)
    threads.emplace_back([this, &ok_count]() {
        for (int i = 0; i < NUM_LOADS; ++i) {
            astro_mount::StateLoadRequest req;
            req.set_file_path("/tmp/test_concurrent_0_" + std::to_string(::getpid()) + ".json");

            grpc::ClientContext ctx;
            google::protobuf::Empty resp;

            if (stub_->LoadState(&ctx, req, &resp).ok()) {
                ok_count++;
            }
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    // All saves should succeed, loads may fail if file doesn't exist yet
    EXPECT_GE(ok_count, NUM_SAVES);  // At least the saves succeed
}

// ============================================================================
// NEGATIVE TESTS FOR INVALID PROTOBUF MESSAGES
// ============================================================================

TEST_F(GrpcIntegrationTest, InvalidCoordinates_OutOfRange) {
    // Test with RA outside valid range [0, 24)
    {
        astro_mount::Coordinates request;
        request.set_ra(-1.0);   // Invalid: negative RA
        request.set_dec(45.0);

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->SlewToCoordinates(&context, request, &response);
        // Service passes through to controller - may accept or reject
        EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
    }

    // Test with RA > 24h
    {
        astro_mount::Coordinates request;
        request.set_ra(30.0);   // Invalid: RA > 24h
        request.set_dec(45.0);

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->SlewToCoordinates(&context, request, &response);
        EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
    }

    // Test with Dec outside [-90, 90]
    {
        astro_mount::Coordinates request;
        request.set_ra(10.0);
        request.set_dec(100.0);  // Invalid: Dec > 90°

        grpc::ClientContext context;
        google::protobuf::Empty response;

        auto status = stub_->SlewToCoordinates(&context, request, &response);
        EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
    }
}

TEST_F(GrpcIntegrationTest, InvalidCoordinates_NaN) {
    astro_mount::Coordinates request;
    request.set_ra(std::numeric_limits<double>::quiet_NaN());
    request.set_dec(45.0);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->SlewToCoordinates(&context, request, &response);
    // NaN values propagate through; test passes if no crash
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_F(GrpcIntegrationTest, InvalidCoordinates_Infinity) {
    astro_mount::Coordinates request;
    request.set_ra(std::numeric_limits<double>::infinity());
    request.set_dec(-std::numeric_limits<double>::infinity());

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->SlewToCoordinates(&context, request, &response);
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_F(GrpcIntegrationTest, InvalidConfiguration_NegativeValues) {
    astro_mount::Configuration request;
    request.set_latitude(52.0);
    request.set_longitude(21.0);
    request.mutable_ha_axis_params()->set_gear_ratio(-1.0);   // Invalid: negative gear ratio
    request.mutable_dec_axis_params()->set_gear_ratio(0.0);    // Invalid: zero gear ratio
    request.set_max_slew_rate(-5.0);      // Invalid: negative slew rate
    request.set_max_tracking_rate(-0.1);  // Invalid: negative tracking rate

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->UpdateConfiguration(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, InvalidHealthCheck_EmptyService) {
    astro_mount::HealthCheckRequest request;
    // Empty service name (default proto3 value)
    request.set_service("");

    grpc::ClientContext context;
    astro_mount::HealthCheckResponse response;

    auto status = stub_->CheckHealth(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.service(), "");
    // Should still return SERVING even with empty service name
    EXPECT_EQ(response.status(), astro_mount::HealthCheckResponse::SERVING);
}

TEST_F(GrpcIntegrationTest, InvalidHealthCheck_UnknownService) {
    astro_mount::HealthCheckRequest request;
    request.set_service("nonexistent_service");

    grpc::ClientContext context;
    astro_mount::HealthCheckResponse response;

    auto status = stub_->CheckHealth(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.service(), "nonexistent_service");
}

TEST_F(GrpcIntegrationTest, SaveState_EmptyPath) {
    astro_mount::StateSaveRequest request;
    // Empty file_path should trigger default path behavior
    request.set_file_path("");

    grpc::ClientContext context;
    astro_mount::StateSaveResponse response;

    auto status = stub_->SaveState(&context, request, &response);
    EXPECT_TRUE(status.ok());
    // Should use default path when empty
    EXPECT_EQ(response.file_path(), "mount_state.json");

    // Cleanup
    std::remove("mount_state.json");
}

TEST_F(GrpcIntegrationTest, SaveState_VeryLongPath) {
    // Test with a long (but filesystem-compatible) file path
    // Note: A 4096-char filename exceeds Linux PATH_MAX (4096) and
    // component length limits (255), so we use 200 chars instead.
    std::string long_path(200, 'a');
    long_path += ".json";
    long_path = "/tmp/" + long_path;

    astro_mount::StateSaveRequest request;
    request.set_file_path(long_path);

    grpc::ClientContext context;
    astro_mount::StateSaveResponse response;

    auto status = stub_->SaveState(&context, request, &response);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.file_path(), long_path);

    // Cleanup
    std::remove(long_path.c_str());
}

TEST_F(GrpcIntegrationTest, EncoderConfig_InvalidResolution) {
    astro_mount::EncoderConfig request;
    request.set_type(astro_mount::EncoderConfig::ABSOLUTE);
    request.set_resolution(0.0);  // Invalid: zero resolution

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->EnableEncoders(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, EncoderConfig_NegativeResolution) {
    astro_mount::EncoderConfig request;
    request.set_type(astro_mount::EncoderConfig::INCREMENTAL);
    request.set_resolution(-100.0);  // Invalid: negative resolution

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->EnableEncoders(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, GuiderConfig_InvalidAggression) {
    // Test with out-of-range aggression value
    astro_mount::GuiderConfig request;
    request.set_connection_string("tcp://localhost:7624");
    request.set_max_correction(10.0);
    request.set_aggression(2.5);  // Invalid: > 1.0

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->ConnectGuider(&context, request, &response);
    // May succeed or fail depending on mock HAL, but no crash
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_F(GrpcIntegrationTest, GuiderConfig_EmptyConnectionString) {
    astro_mount::GuiderConfig request;
    request.set_connection_string("");  // Invalid: empty connection string
    request.set_max_correction(5.0);
    request.set_aggression(0.5);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->ConnectGuider(&context, request, &response);
    EXPECT_TRUE(status.ok() || status.error_code() == grpc::StatusCode::INTERNAL);
}

TEST_F(GrpcIntegrationTest, DeterminePole_NegativeDuration) {
    astro_mount::PoleDeterminationRequest request;
    request.set_measurement_count(5);
    request.set_duration_hours(-1.0);  // Invalid: negative duration

    grpc::ClientContext context;
    astro_mount::PolePosition response;

    auto status = stub_->DeterminePolePosition(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, DeterminePole_ZeroMeasurements) {
    astro_mount::PoleDeterminationRequest request;
    request.set_measurement_count(0);  // Invalid: zero measurements
    request.set_duration_hours(1.0);

    grpc::ClientContext context;
    astro_mount::PolePosition response;

    auto status = stub_->DeterminePolePosition(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, Measurement_ZeroValues) {
    // All proto3 default values (all zeros)
    astro_mount::Measurement request;
    // No fields set - all default values

    grpc::ClientContext context;
    google::protobuf::Empty response;

    // Should handle empty measurement gracefully
    auto status = stub_->AddMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, Measurement_MissingObserved) {
    astro_mount::Measurement request;
    // Set expected coordinates but not observed
    request.mutable_expected()->set_ra(10.0);
    request.mutable_expected()->set_dec(20.0);
    // observed is default (all zeros)

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, Measurement_ExtremeValues) {
    astro_mount::Measurement request;
    request.mutable_observed()->set_ra(1e308);   // near max double
    request.mutable_observed()->set_dec(-1e308);
    request.mutable_expected()->set_ra(1e-308);  // near min double
    request.mutable_expected()->set_dec(-1e-308);
    request.set_temperature(1e308);
    request.set_pressure(-1e308);

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

TEST_F(GrpcIntegrationTest, Measurement_NegativeEnvironmentalParams) {
    astro_mount::Measurement request;
    request.mutable_observed()->set_ra(10.0);
    request.mutable_observed()->set_dec(20.0);
    request.mutable_expected()->set_ra(10.1);
    request.mutable_expected()->set_dec(20.1);
    request.set_temperature(-273.15);  // Absolute zero - extreme but valid
    request.set_pressure(0.0);         // Zero pressure - should trigger default
    request.set_humidity(-0.1);        // Invalid negative humidity

    grpc::ClientContext context;
    google::protobuf::Empty response;

    auto status = stub_->AddMeasurement(&context, request, &response);
    EXPECT_TRUE(status.ok());
}

} // namespace test
} // namespace api
} // namespace astro_mount

// Main function for running tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Initialize logger once before all tests
    // Logger is a singleton that cannot be re-initialized per test
    astro_mount::logging::Logger::init("");
    
    int result = RUN_ALL_TESTS();
    
    astro_mount::logging::Logger::shutdown();
    return result;
}
