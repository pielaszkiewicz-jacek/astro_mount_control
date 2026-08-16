#ifndef ST4_GUIDER_H
#define ST4_GUIDER_H
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include "hal/st4_control.h"
#include "controllers/st4_calibration.h"

namespace astro_mount { namespace controllers {

class St4Guider {
public:
    explicit St4Guider(std::unique_ptr<hal::St4Control> hal);
    ~St4Guider();

    // N3: open a persistent PHD2 TCP connection (default localhost:4400),
    // send the JSON-RPC CONNECT handshake and keep the socket open with a
    // reader thread that detects disconnects. Returns true only when a real
    // connection was established.
    bool startPHD2(const std::string& host, int port);
    void stopPHD2();

    /// @brief Whether a live PHD2 connection is currently held (N3).
    bool isPHD2Connected() const;

    /// @brief Send a PHD2 JSON-RPC method call on the live connection
    /// (e.g. "start_guiding" / "stop_guiding" / "loop"). Fire-and-forget.
    bool sendPhd2Method(const std::string& method, const std::string& params_json = "");

    bool pulse(hal::St4Direction dir, int duration_ms);

    /**
     * @brief Run ST4 calibration (arcsec/ms per axis).
     *
     * Optionally accepts a displacement probe callback. Without one, a
     * theoretical sidereal-rate calibration is used. Returns true on success
     * and stores the coefficients internally.
     */
    bool calibrate(St4DisplacementProbe probe = nullptr,
                   int pulse_duration_ms = 500,
                   int steps = 4,
                   int step_delay_ms = 500);

    /// Retrieve the last calibration coefficients.
    St4CalibrationParams getCalibration() const;

    /// Convert an arcsec correction to an ST4 pulse duration (ms) for an axis.
    /// Uses the measured (or theoretical) calibration coefficients.
    int correctionToPulseMs(double correction_arcsec, bool is_ra) const;

    /// @brief Apply guide-loop parameters from a St4GuiderConfig.
    /// @param aggression       0..1 multiplier on the computed pulse duration
    /// @param invert_ra        Flip the RA (E/W) correction direction
    /// @param invert_dec       Flip the Dec (N/S) correction direction
    /// @param min_pulse_ms     Minimum pulse duration (dead zone, ms)
    /// @param max_pulse_ms     Maximum pulse duration (ms)
    void setGuideParams(double aggression, bool invert_ra, bool invert_dec,
                        int min_pulse_ms, int max_pulse_ms);

    /// @brief Whether the guiding loop is active (PHD2 StartGuiding/GuideStopped
    /// events + explicit start/stop). Thread-safe.
    bool isGuiding() const { return guiding_.load(); }
    void setGuiding(bool v) { guiding_ = v; }

    /// @brief Process one PHD2 JSON-RPC event message (the body after the
    /// Content-Length header). GuideStep events drive the guiding loop; state
    /// events (StartGuiding/GuideStopped/AppState) and CalibrationComplete are
    /// also handled. Public so the loop can be driven by tests or other feeds.
    void handlePhd2Event(const std::string& body);

    struct Stats {
        int pulses_sent{0}, pulses_failed{0};
        double ra_correction{0}, dec_correction{0};
        double rms_ra{0}, rms_dec{0};
        bool calibrated{false};
    };
    Stats getStats() const;

private:
    // Reader thread body — blocks on recv(), parses PHD2 JSON-RPC event
    // messages (Content-Length framed) and feeds GuideStep corrections into the
    // guiding loop. Clears phd2_connected_ when the peer closes the socket (N3).
    // Exits when stopPHD2() closes the fd.
    void phd2ReaderLoop();

    // Apply a GuideStep correction (arcsec) as ST4 pulses through the HAL.
    void applyGuideCorrection(double ra_arcsec, double dec_arcsec);

    // Push a correction into the rolling RMS estimate for one axis.
    void updateRms(double error_arcsec, double& rms_out, bool& init_flag);

    std::unique_ptr<hal::St4Control> hal_;
    std::atomic<bool> phd2_connected_{false};
    std::atomic<bool> guiding_{false};
    int phd2_fd_{-1};                       // guarded by phd2_mutex_
    std::unique_ptr<std::thread> phd2_thread_;
    mutable std::mutex phd2_mutex_;         // guards phd2_fd_ / thread lifecycle

    // Guide-loop parameters (guarded by mutex_).
    double aggression_{1.0};
    bool invert_ra_{false};
    bool invert_dec_{false};
    int min_pulse_ms_{10};
    int max_pulse_ms_{3000};

    // Rolling RMS state (guarded by mutex_).
    double rms_ra_state_{0.0};
    double rms_dec_state_{0.0};
    bool rms_ra_init_{false};
    bool rms_dec_init_{false};

    // Monotonic JSON-RPC request id for sendPhd2Method().
    int next_request_id_{2};  // 1 is used by the CONNECT handshake

    Stats stats_;
    St4CalibrationParams calibration_;
    mutable std::mutex mutex_;   // guards stats_, calibration_, guide params
};

}} // namespace astro_mount::controllers
#endif
