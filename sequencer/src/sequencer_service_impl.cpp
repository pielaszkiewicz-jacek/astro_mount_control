#include "sequencer/include/sequencer_service_impl.h"
#include "sequencer/observation_sequencer.h"
#include "sequencer/observation_plan.h"
#include "sequencer/target.h"
#include "models/exposure_planner.h"
#include "sequencer/exposure_sequence.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

namespace astro_sequencer {

using astro_mount::sequencer::ObservationSequencer;
using astro_mount::sequencer::ObservationPlan;
using astro_mount::sequencer::PlanState;
using astro_mount::sequencer::Target;
using astro_mount::models::ExposurePlan;
using ModelsExposureStep = astro_mount::models::ExposureStep;
using SeqExposureStep = astro_mount::sequencer::ExposureStep;

#define RETURN_ERROR(resp, msg) \
    do { \
        (resp)->set_success(false); \
        (resp)->set_message(msg); \
        return grpc::Status(grpc::StatusCode::UNAVAILABLE, msg); \
    } while(0)

SequencerServiceImpl::SequencerServiceImpl(const std::string& config_path)
    : config_path_(config_path) {
    try {
        configureFromJson(config_path);
        sequencer_ = std::make_unique<ObservationSequencer>();
        setupCallbacks();
        initialized_ = true;
        std::cout << "[SequencerServiceImpl] Initialised (simulated="
                  << (simulated_ ? "yes" : "no") << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "[SequencerServiceImpl] Init error: " << e.what() << "\n";
    }
}

SequencerServiceImpl::~SequencerServiceImpl() {
    if (sequencer_) sequencer_->stop();
}

bool SequencerServiceImpl::configureFromJson(const std::string& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[SequencerServiceImpl] No config file, using simulated mode\n";
        return false;
    }

    nlohmann::json config;
    file >> config;

    simulated_ = config.value("simulated", true);

    if (!simulated_ && config.contains("mount_address")) {
        std::string mount_address = config["mount_address"].get<std::string>();
        auto channel = grpc::CreateChannel(mount_address, grpc::InsecureChannelCredentials());
        mount_stub_ = astro_mount::MountControllerService::NewStub(channel);
        std::cout << "[SequencerServiceImpl] Connected to mount at " << mount_address << "\n";
    }

    return true;
}

void SequencerServiceImpl::setupCallbacks() {
    if (!sequencer_) return;

    if (simulated_ || !mount_stub_) {
        sequencer_->setSlewCallback([](double ra, double dec) -> bool {
            std::cout << "[Sequencer:SIM] Slew to RA=" << ra << " Dec=" << dec << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return true;
        });
        sequencer_->setTrackCallback([](double ra, double dec) -> bool {
            std::cout << "[Sequencer:SIM] Track RA=" << ra << " Dec=" << dec << "\n";
            return true;
        });
        sequencer_->setAutoFocusCallback([]() -> bool {
            std::cout << "[Sequencer:SIM] Auto-focus\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return true;
        });
        sequencer_->setExposeCallback([](const SeqExposureStep& step) -> bool {
            std::cout << "[Sequencer:SIM] Expose " << step.exposure_time_s << "s "
                      << "gain=" << step.gain << " binning=" << step.binning
                      << " filter=" << step.filter_name << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(step.exposure_time_s * 100)));
            return true;
        });
        sequencer_->setParkCallback([]() -> bool {
            std::cout << "[Sequencer:SIM] Park mount\n";
            return true;
        });
        sequencer_->setAlertCallback([](const std::string& msg) {
            std::cout << "[Sequencer] " << msg << "\n";
        });
    } else {
        sequencer_->setSlewCallback([this](double ra, double dec) -> bool {
            grpc::ClientContext ctx;
            astro_mount::Coordinates coords;
            coords.set_ra(ra); coords.set_dec(dec);
            google::protobuf::Empty resp;
            return mount_stub_->SlewToCoordinates(&ctx, coords, &resp).ok();
        });
        sequencer_->setTrackCallback([this](double ra, double dec) -> bool {
            grpc::ClientContext ctx;
            astro_mount::Coordinates coords;
            coords.set_ra(ra); coords.set_dec(dec);
            google::protobuf::Empty resp;
            return mount_stub_->TrackObject(&ctx, coords, &resp).ok();
        });
        sequencer_->setParkCallback([this]() -> bool {
            grpc::ClientContext ctx;
            google::protobuf::Empty req, resp;
            return mount_stub_->Park(&ctx, req, &resp).ok();
        });
        sequencer_->setAlertCallback([](const std::string& msg) {
            std::cerr << "[Sequencer] " << msg << "\n";
        });
    }
}

grpc::Status SequencerServiceImpl::LoadPlan(
    grpc::ServerContext* context,
    const astro_mount::ObservationPlanProto* request,
    astro_mount::SequencerResult* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !sequencer_)
        RETURN_ERROR(response, "Sequencer not initialised");

    ObservationPlan plan;
    plan.name = request->name();
    plan.observer = request->observer();
    plan.notes = request->notes();
    plan.auto_focus = request->auto_focus();
    plan.auto_guide = request->auto_guide();
    plan.dither = request->dither();
    plan.focus_interval = request->focus_interval();

    for (const auto& tp : request->targets()) {
        Target t;
        t.name = tp.name();
        t.catalog_name = tp.catalog_name();
        t.ra = tp.ra();
        t.dec = tp.dec();
        t.magnitude = tp.magnitude();
        t.type = tp.type();

        ModelsExposureStep step;
        step.exposure_time_s = tp.exposure_time_s();
        step.gain = tp.gain();
        step.binning = tp.binning();
        step.filter_position = tp.filter_position();
        step.filter_name = tp.filter_name();
        step.count = tp.exposure_count() > 0 ? tp.exposure_count() : 1;
        t.exposure_plan.steps.push_back(step);

        plan.targets.push_back(t);
    }

    if (!sequencer_->loadPlan(plan)) {
        RETURN_ERROR(response, "Cannot load plan — sequencer not in IDLE/COMPLETED state");
    }

    response->set_success(true);
    response->set_message("Plan '" + plan.name + "' loaded with " +
                          std::to_string(plan.targets.size()) + " targets");
    return grpc::Status::OK;
}

grpc::Status SequencerServiceImpl::StartSequencer(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::SequencerResult* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !sequencer_)
        RETURN_ERROR(response, "Sequencer not initialised");
    if (!sequencer_->start())
        RETURN_ERROR(response, "Failed to start — no plan loaded or already running");
    response->set_success(true);
    response->set_message("Sequencer started");
    return grpc::Status::OK;
}

grpc::Status SequencerServiceImpl::StopSequencer(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::SequencerResult* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !sequencer_)
        RETURN_ERROR(response, "Sequencer not initialised");
    sequencer_->stop();
    response->set_success(true);
    response->set_message("Sequencer stopped");
    return grpc::Status::OK;
}

grpc::Status SequencerServiceImpl::PauseSequencer(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::SequencerResult* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    sequencer_->pause();
    response->set_success(true);
    response->set_message("Sequencer paused");
    return grpc::Status::OK;
}

grpc::Status SequencerServiceImpl::GetSequencerStatus(
    grpc::ServerContext* context,
    const google::protobuf::Empty* request,
    astro_mount::SequencerStatus* response) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !sequencer_) {
        response->set_state("IDLE");
        return grpc::Status::OK;
    }

    switch (sequencer_->getState()) {
        case PlanState::IDLE:       response->set_state("IDLE"); break;
        case PlanState::LOADING:    response->set_state("LOADING"); break;
        case PlanState::SLEWING:    response->set_state("SLEWING"); break;
        case PlanState::TRACKING:   response->set_state("TRACKING"); break;
        case PlanState::FOCUSING:   response->set_state("FOCUSING"); break;
        case PlanState::EXPOSING:   response->set_state("EXPOSING"); break;
        case PlanState::PARKING:    response->set_state("PARKING"); break;
        case PlanState::PARKED:     response->set_state("PARKED"); break;
        case PlanState::COMPLETED:  response->set_state("COMPLETED"); break;
        default:                    response->set_state("ERROR"); break;
    }

    response->set_current_target_index(sequencer_->getCurrentTargetIndex());
    response->set_current_exposure(sequencer_->getCurrentExposureIndex());
    response->set_progress_percent(sequencer_->getProgress());

    return grpc::Status::OK;
}

} // namespace astro_sequencer
