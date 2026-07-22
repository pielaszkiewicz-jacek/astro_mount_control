#include "sequencer/observation_sequencer.h"
#include <chrono>
#include <thread>
#include <cmath>

namespace astro_mount {
namespace sequencer {

ObservationSequencer::ObservationSequencer() = default;

ObservationSequencer::~ObservationSequencer() {
    stop();
}

bool ObservationSequencer::loadPlan(const ObservationPlan& plan) {
    if (state_ != PlanState::IDLE && state_ != PlanState::COMPLETED) {
        return false;
    }
    plan_ = plan;
    current_target_ = -1;
    current_exposure_ = 0;
    state_ = PlanState::IDLE;
    return true;
}

bool ObservationSequencer::start() {
    if (plan_.targets.empty()) return false;
    if (running_) return true;

    running_ = true;
    paused_ = false;
    current_target_ = 0;
    state_ = PlanState::LOADING;
    worker_thread_ = std::make_unique<std::thread>(&ObservationSequencer::sequencerLoop, this);
    return true;
}

void ObservationSequencer::stop() {
    running_ = false;
    paused_ = false;
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    worker_thread_.reset();
    state_ = PlanState::IDLE;
}

void ObservationSequencer::pause() {
    paused_ = true;
}

void ObservationSequencer::resume() {
    paused_ = false;
}

PlanState ObservationSequencer::getState() const {
    return state_;
}

int ObservationSequencer::getCurrentTargetIndex() const {
    return current_target_;
}

int ObservationSequencer::getCurrentExposureIndex() const {
    return current_exposure_;
}

double ObservationSequencer::getProgress() const {
    if (plan_.targets.empty()) return 0.0;
    int total = plan_.getTotalExposures();
    if (total == 0) return 0.0;
    return static_cast<double>(current_exposure_) / total * 100.0;
}

void ObservationSequencer::onWeatherAlert(bool auto_park) {
    if (auto_park && running_) {
        if (alert_cb_) alert_cb_("Weather alert received — parking mount");
        stop();
        state_ = PlanState::PARKING;
        if (park_cb_) park_cb_();
        state_ = PlanState::PARKED;
    }
}

void ObservationSequencer::sequencerLoop() {
    state_ = PlanState::LOADING;

    for (int t = 0; t < static_cast<int>(plan_.targets.size()) && running_; ++t) {
        current_target_ = t;
        const auto& target = plan_.targets[t];

        if (alert_cb_) {
            alert_cb_("Starting target: " + target.name);
        }

        if (!executeTarget(target)) {
            if (alert_cb_) alert_cb_("Failed target: " + target.name);
            continue;
        }

        while (paused_ && running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (running_) {
        state_ = PlanState::PARKING;
        if (park_cb_) park_cb_();
        state_ = PlanState::COMPLETED;
    }

    running_ = false;
}

bool ObservationSequencer::executeTarget(const Target& target) {
    // 1. Slew to target
    state_ = PlanState::SLEWING;
    if (slew_cb_ && !slew_cb_(target.ra, target.dec)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Start tracking
    state_ = PlanState::TRACKING;
    if (track_cb_ && !track_cb_(target.ra, target.dec)) {
        return false;
    }

    // 3. Auto-focus (if enabled and interval reached)
    if (plan_.auto_focus && current_target_ % plan_.focus_interval == 0) {
        state_ = PlanState::FOCUSING;
        if (focus_cb_) focus_cb_();
    }

    // 4. Execute exposures
    return executeExposures(target.exposure_plan);
}

bool ObservationSequencer::executeExposures(const models::ExposurePlan& plan) {
    for (const auto& step : plan.steps) {
        for (int i = 0; i < step.count && running_; ++i) {
            state_ = PlanState::EXPOSING;

            ExposureStep exp;
            exp.exposure_time_s = step.exposure_time_s;
            exp.gain = step.gain;
            exp.binning = step.binning;
            exp.filter_position = step.filter_position;
            exp.filter_name = step.filter_name;
            exp.image_type = step.image_type;

            if (expose_cb_ && !expose_cb_(exp)) {
                return false;
            }

            current_exposure_++;
        }
    }
    return true;
}

} // namespace sequencer
} // namespace astro_mount
