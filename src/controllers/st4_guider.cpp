#include "controllers/st4_guider.h"
#include "controllers/st4_calibration.h"
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <string>
#include <nlohmann/json.hpp>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace astro_mount { namespace controllers {

namespace {

#ifdef __linux__
// Write all bytes to a socket (MSG_NOSIGNAL so a closed peer does not SIGPIPE).
bool writeAll(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}
#endif

} // anonymous namespace

St4Guider::St4Guider(std::unique_ptr<hal::St4Control> hal) : hal_(std::move(hal)) {}
St4Guider::~St4Guider() { stopPHD2(); }

bool St4Guider::startPHD2(const std::string& host, int port) {
    // N3: real, persistent PHD2 integration — open a TCP connection (default
    // localhost:4400), send the PHD2 JSON-RPC CONNECT handshake and KEEP the
    // socket open with a reader thread that parses the event stream. The
    // reader now feeds GuideStep corrections into the guiding loop, so
    // `phd2_connected_` reflects a live, monitored connection.
    stopPHD2();
#ifdef __linux__
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) {
        ::close(fd);
        return false;
    }
    bool ok = (::connect(fd, res->ai_addr, res->ai_addrlen) == 0);
    ::freeaddrinfo(res);

    if (ok) {
        // PHD2 JSON-RPC: header "Content-Length: N" + JSON body.
        const std::string cmd =
            "{\"jsonrpc\":\"2.0\",\"method\":\"CONNECT\","
            "\"params\":{\"app_name\":\"astro-mount\",\"app_version\":\"1.0\"},\"id\":1}";
        const std::string msg =
            "Content-Length: " + std::to_string(cmd.size()) + "\r\n\r\n" + cmd;
        ok = writeAll(fd, msg.data(), msg.size());
    }

    if (ok) {
        // Publish the fd and start the monitor thread before returning.
        {
            std::lock_guard<std::mutex> lock(phd2_mutex_);
            phd2_fd_ = fd;
        }
        phd2_connected_ = true;
        phd2_thread_ = std::make_unique<std::thread>(&St4Guider::phd2ReaderLoop, this);
    } else {
        ::close(fd);
    }

    return ok;
#else
    (void)host; (void)port;
    phd2_connected_ = false;
    return false;
#endif
}

bool St4Guider::sendPhd2Method(const std::string& method,
                               const std::string& params_json) {
#ifdef __linux__
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(phd2_mutex_);
        fd = phd2_fd_;
    }
    if (fd < 0) return false;

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["method"] = method;
    req["params"] = params_json.empty()
        ? nlohmann::json::object()
        : nlohmann::json::parse(params_json, nullptr, false);
    req["id"] = next_request_id_++;

    std::string body = req.dump();
    std::string frame = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return writeAll(fd, frame.data(), frame.size());
#else
    (void)method; (void)params_json;
    return false;
#endif
}

void St4Guider::phd2ReaderLoop() {
    // Blocks on recv(); when the peer closes the socket (recv returns 0/error)
    // clear the connected flag. stopPHD2() shuts down + closes the fd to wake
    // this loop and joins the thread. Incoming bytes are assembled into
    // Content-Length framed JSON-RPC messages and dispatched to
    // processPhd2Event() — this is the live PHD2 event stream that drives the
    // guiding loop (GuideStep → corrections → ST4 pulses).
    std::string buffer;
    while (phd2_connected_) {
        char chunk[512];
        int fd = -1;
        {
            std::lock_guard<std::mutex> lock(phd2_mutex_);
            fd = phd2_fd_;
        }
        if (fd < 0) break;

        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            phd2_connected_ = false;
            break;
        }
        buffer.append(chunk, static_cast<size_t>(n));

        // Extract all complete Content-Length framed messages.
        while (true) {
            auto hdr_end = buffer.find("\r\n\r\n");
            if (hdr_end == std::string::npos) break;  // incomplete header

            std::string header = buffer.substr(0, hdr_end);
            size_t cl_pos = header.find("Content-Length:");
            if (cl_pos == std::string::npos) {
                // Unknown framing — drop this header block so we do not stall.
                buffer.erase(0, hdr_end + 4);
                continue;
            }
            long content_length = std::atol(header.c_str() + cl_pos + 15);
            if (content_length < 0 || content_length > (1 << 20)) {
                buffer.erase(0, hdr_end + 4);
                continue;
            }
            size_t body_start = hdr_end + 4;
            if (buffer.size() < body_start + static_cast<size_t>(content_length)) {
                break;  // incomplete body — wait for more data
            }
            std::string body = buffer.substr(body_start, static_cast<size_t>(content_length));
            buffer.erase(0, body_start + static_cast<size_t>(content_length));

            handlePhd2Event(body);
        }

        // Safety: drop stale partial data if a malformed frame grows unbounded.
        if (buffer.size() > (1 << 20)) buffer.clear();
    }
}

void St4Guider::handlePhd2Event(const std::string& body) {
    nlohmann::json msg;
    try {
        msg = nlohmann::json::parse(body);
    } catch (...) {
        return;
    }
    if (!msg.is_object()) return;

    const std::string event = msg.value("event", "");

    // ── GuideStep: the heart of the guiding loop ──
    if (event == "GuideStep") {
        double ra = msg.value("RADistance", 0.0);     // arcsec
        double dec = msg.value("DecDistance", 0.0);   // arcsec
        applyGuideCorrection(ra, dec);
        return;
    }

    // ── Guiding state transitions ──
    if (event == "StartGuiding") {
        guiding_ = true;
        return;
    }
    if (event == "GuideStopped" || event == "StarLost") {
        guiding_ = false;
        return;
    }

    // ── AppState carries a text "State" field in newer PHD2 versions ──
    if (event == "AppState") {
        const std::string state = msg.value("State", "");
        guiding_ = (state == "Guiding");
        return;
    }

    // ── CalibrationComplete: ingest real per-axis coefficients from PHD2 ──
    if (event == "CalibrationComplete") {
        if (msg.contains("ra_arcsec_per_ms") || msg.contains("dec_arcsec_per_ms")) {
            St4CalibrationParams cal;
            cal.ra_arcsec_per_ms = msg.value("ra_arcsec_per_ms", 0.0);
            cal.dec_arcsec_per_ms = msg.value("dec_arcsec_per_ms", 0.0);
            cal.calibrated = (cal.ra_arcsec_per_ms > 0.0) || (cal.dec_arcsec_per_ms > 0.0);
            cal.method = "phd2";
            if (cal.calibrated) {
                std::lock_guard<std::mutex> lock(mutex_);
                calibration_ = cal;
                stats_.calibrated = true;
            }
        }
        return;
    }

    // Other events (SettleDone, StarSelected, LockPositionSet, ...) are
    // intentionally ignored — the guiding loop only needs GuideStep + state.
}

void St4Guider::applyGuideCorrection(double ra_arcsec, double dec_arcsec) {
    if (!guiding_.load()) return;
    if (!hal_ || !hal_->isConnected()) return;

    if (!std::isfinite(ra_arcsec)) ra_arcsec = 0.0;
    if (!std::isfinite(dec_arcsec)) dec_arcsec = 0.0;

    bool invert_ra = false, invert_dec = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        invert_ra = invert_ra_;
        invert_dec = invert_dec_;
    }

    // ── RA axis ──
    if (std::abs(ra_arcsec) > 0.0) {
        int ra_ms = correctionToPulseMs(ra_arcsec, true);
        if (ra_ms > 0) {
            // Positive RA error = star East of lock → move West to re-center.
            bool west = (ra_arcsec > 0.0);
            if (invert_ra) west = !west;
            hal::St4Direction dir = west ? hal::St4Direction::WEST : hal::St4Direction::EAST;
            pulse(dir, ra_ms);
        }
    }

    // ── Dec axis ──
    if (std::abs(dec_arcsec) > 0.0) {
        int dec_ms = correctionToPulseMs(dec_arcsec, false);
        if (dec_ms > 0) {
            // Positive Dec error = star North of lock → move South to re-center.
            bool south = (dec_arcsec > 0.0);
            if (invert_dec) south = !south;
            hal::St4Direction dir = south ? hal::St4Direction::SOUTH : hal::St4Direction::NORTH;
            pulse(dir, dec_ms);
        }
    }

    // ── Track corrections + rolling RMS ──
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.ra_correction = ra_arcsec;
        stats_.dec_correction = dec_arcsec;
        updateRms(ra_arcsec, rms_ra_state_, rms_ra_init_);
        updateRms(dec_arcsec, rms_dec_state_, rms_dec_init_);
        stats_.rms_ra = rms_ra_state_;
        stats_.rms_dec = rms_dec_state_;
    }
}

void St4Guider::updateRms(double error_arcsec, double& rms_out, bool& init_flag) {
    // Exponentially-weighted RMS (alpha=0.1) — smooth, no fixed window needed.
    const double alpha = 0.1;
    if (!init_flag) {
        rms_out = std::abs(error_arcsec);
        init_flag = true;
    } else {
        rms_out = std::sqrt((1.0 - alpha) * rms_out * rms_out +
                            alpha * error_arcsec * error_arcsec);
    }
}

void St4Guider::setGuideParams(double aggression, bool invert_ra, bool invert_dec,
                               int min_pulse_ms, int max_pulse_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    aggression_ = (std::isfinite(aggression) && aggression > 0.0) ? aggression : 1.0;
    invert_ra_ = invert_ra;
    invert_dec_ = invert_dec;
    min_pulse_ms_ = std::max(1, min_pulse_ms);
    max_pulse_ms_ = std::max(min_pulse_ms_, max_pulse_ms);
}

void St4Guider::stopPHD2() {
    phd2_connected_ = false;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(phd2_mutex_);
        fd = phd2_fd_;
        phd2_fd_ = -1;
    }
    if (fd >= 0) {
#ifdef __linux__
        ::shutdown(fd, SHUT_RDWR);  // wake the blocked recv()
        ::close(fd);
#endif
    }
    if (phd2_thread_ && phd2_thread_->joinable()) {
        phd2_thread_->join();
    }
    phd2_thread_.reset();
}

bool St4Guider::isPHD2Connected() const {
    return phd2_connected_.load();
}

bool St4Guider::pulse(hal::St4Direction dir, int duration_ms) {
    if (hal_->pulse(dir, duration_ms)) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_.pulses_sent++;
        return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.pulses_failed++;
    return false;
}

bool St4Guider::calibrate(St4DisplacementProbe probe,
                          int pulse_duration_ms,
                          int steps,
                          int step_delay_ms) {
    if (!hal_ || !hal_->isConnected()) {
        return false;
    }

    St4Calibration calibration;
    St4CalibrationParams params =
        calibration.calibrate(*hal_, pulse_duration_ms, steps, step_delay_ms, probe);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        calibration_ = params;
        stats_.calibrated = params.calibrated;
    }
    return params.calibrated;
}

St4CalibrationParams St4Guider::getCalibration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calibration_;
}

int St4Guider::correctionToPulseMs(double correction_arcsec, bool is_ra) const {
    std::lock_guard<std::mutex> lock(mutex_);
    double rate = is_ra ? calibration_.ra_arcsec_per_ms
                        : calibration_.dec_arcsec_per_ms;
    if (!calibration_.calibrated || rate <= 0.0 ||
        !std::isfinite(correction_arcsec)) {
        return 0;
    }
    double ms = std::abs(correction_arcsec) / rate * aggression_;
    if (!std::isfinite(ms)) return 0;
    int rounded = static_cast<int>(std::lround(ms));
    // Dead zone: corrections below min_pulse_ms are skipped (avoids
    // over-correcting on sub-threshold errors).
    if (rounded < min_pulse_ms_) return 0;
    if (rounded > max_pulse_ms_) rounded = max_pulse_ms_;
    return rounded;
}

St4Guider::Stats St4Guider::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}} // namespace
