#include "controllers/lx200_interface.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <sstream>

namespace astro_mount { namespace controllers {

Lx200SerialInterface::Lx200SerialInterface(const std::string& port, int baud)
    : port_(port), baud_(baud) {}

Lx200SerialInterface::~Lx200SerialInterface() { disconnect(); }

bool Lx200SerialInterface::connect(const std::string& port, int baud) {
    port_ = port; baud_ = baud;
    // open(port_.c_str(), O_RDWR | O_NOCTTY);
    connected_ = true;
    return true;
}

void Lx200SerialInterface::disconnect() {
    if (connected_) { /* close(fd_); */ fd_ = -1; connected_ = false; }
}

bool Lx200SerialInterface::isConnected() const { return connected_; }

Lx200CommandResult Lx200SerialInterface::sendCommand(const std::string& cmd) {
    Lx200CommandResult result;
    if (!connected_) { result.error = "Not connected"; return result; }
    if (!writeCommand(cmd)) { result.error = "Write failed"; return result; }
    auto resp = readResponse();
    result.success = !resp.empty() && resp != "#";
    result.response = resp;
    return result;
}

Lx200CommandResult Lx200SerialInterface::sendCommand(Lx200Command cmd) {
    switch (cmd) {
        case Lx200Command::GR: return sendCommand(":GR#");
        case Lx200Command::GD: return sendCommand(":GD#");
        case Lx200Command::MS: return sendCommand(":MS#");
        case Lx200Command::CM: return sendCommand(":CM#");
        case Lx200Command::Q:  return sendCommand(":Q#");
        default: return sendCommand("");
    }
}

bool Lx200SerialInterface::getRaDec(double& ra, double& dec) {
    auto ra_resp = sendCommand(Lx200Command::GR);
    if (!ra_resp.success) return false;
    auto dec_resp = sendCommand(Lx200Command::GD);
    if (!dec_resp.success) return false;
    return Lx200Protocol::parseRA(ra_resp.response, ra) &&
           Lx200Protocol::parseDec(dec_resp.response, dec);
}

bool Lx200SerialInterface::slewTo(double ra, double dec) {
    auto ra_cmd = ":Sr" + Lx200Protocol::formatRA(ra) + "#";
    auto dec_cmd = ":Sd" + Lx200Protocol::formatDec(dec) + "#";
    sendCommand(ra_cmd);
    sendCommand(dec_cmd);
    auto result = sendCommand(Lx200Command::MS);
    return result.success;
}

bool Lx200SerialInterface::syncTo(double ra, double dec) {
    auto ra_cmd = ":Sr" + Lx200Protocol::formatRA(ra) + "#";
    auto dec_cmd = ":Sd" + Lx200Protocol::formatDec(dec) + "#";
    sendCommand(ra_cmd);
    sendCommand(dec_cmd);
    auto result = sendCommand(Lx200Command::CM);
    return result.success;
}

bool Lx200SerialInterface::stop() {
    return sendCommand(Lx200Command::Q).success;
}

std::string Lx200SerialInterface::getVersion() {
    return sendCommand(":GV#").response;
}

bool Lx200SerialInterface::writeCommand(const std::string& cmd) {
    // write(fd_, cmd.c_str(), cmd.length());
    return true;
}

std::string Lx200SerialInterface::readResponse(int timeout_ms) {
    // char buf[256];
    // int n = read(fd_, buf, sizeof(buf) - 1);
    // if (n > 0) { buf[n] = 0; return buf; }
    return "00:00:00";  // Stub
}

}} // namespace
