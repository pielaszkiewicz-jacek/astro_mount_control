#ifndef OBSERVATION_SEQUENCER_H
#define OBSERVATION_SEQUENCER_H

#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include "sequencer/observation_plan.h"
#include "sequencer/exposure_sequence.h"

namespace astro_mount {
namespace sequencer {

class ObservationSequencer {
public:
    ObservationSequencer();
    ~ObservationSequencer();

    bool loadPlan(const ObservationPlan& plan);
    bool start();
    void stop();
    void pause();
    void resume();
    PlanState getState() const;
    int getCurrentTargetIndex() const;
    int getCurrentExposureIndex() const;
    double getProgress() const;

    // Callbacks to external systems
    using SlewCallback = std::function<bool(double ra, double dec)>;
    using TrackCallback = std::function<bool(double ra, double dec)>;
    using AutoFocusCallback = std::function<bool()>;
    using ExposeCallback = std::function<bool(const ExposureStep&)>;
    using ParkCallback = std::function<bool()>;
    using AlertCallback = std::function<void(const std::string&)>;

    void setSlewCallback(SlewCallback cb) { slew_cb_ = std::move(cb); }
    void setTrackCallback(TrackCallback cb) { track_cb_ = std::move(cb); }
    void setAutoFocusCallback(AutoFocusCallback cb) { focus_cb_ = std::move(cb); }
    void setExposeCallback(ExposeCallback cb) { expose_cb_ = std::move(cb); }
    void setParkCallback(ParkCallback cb) { park_cb_ = std::move(cb); }
    void setAlertCallback(AlertCallback cb) { alert_cb_ = std::move(cb); }

    // Weather alert integration
    void onWeatherAlert(bool auto_park);

private:
    void sequencerLoop();
    bool executeTarget(const Target& target);
    bool executeExposures(const models::ExposurePlan& plan);

    ObservationPlan plan_;
    PlanState state_{PlanState::IDLE};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    int current_target_{-1};
    int current_exposure_{0};

    std::unique_ptr<std::thread> worker_thread_;

    SlewCallback slew_cb_;
    TrackCallback track_cb_;
    AutoFocusCallback focus_cb_;
    ExposeCallback expose_cb_;
    ParkCallback park_cb_;
    AlertCallback alert_cb_;
};

} // namespace sequencer
} // namespace astro_mount

#endif
