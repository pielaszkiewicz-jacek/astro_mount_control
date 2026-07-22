#include "controllers/lx200_protocol.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace astro_mount { namespace controllers {

Lx200Command Lx200Protocol::parseCommand(const std::string& cmd) {
    if (cmd.length() < 2) return Lx200Command::NONE;

    if (cmd == ":GR#") return Lx200Command::GR;
    if (cmd == ":GD#") return Lx200Command::GD;
    if (cmd == ":MS#") return Lx200Command::MS;
    if (cmd == ":MN#") return Lx200Command::MN;
    if (cmd == ":CM#") return Lx200Command::CM;
    if (cmd == ":Q#")  return Lx200Command::Q;
    if (cmd == ":GV#") return Lx200Command::GV;
    if (cmd.rfind(":Sr", 0) == 0) return Lx200Command::Sr;
    if (cmd.rfind(":Sd", 0) == 0) return Lx200Command::Sd;
    return Lx200Command::NONE;
}

std::string Lx200Protocol::formatResponse(const Lx200CommandResult& result) {
    return result.success ? result.response : "#";
}

std::string Lx200Protocol::formatRA(double hours) {
    int h = static_cast<int>(std::floor(hours));
    int m = static_cast<int>(std::floor((hours - h) * 60));
    int s = static_cast<int>(std::round(((hours - h) * 60 - m) * 60));
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << h << ":"
       << std::setfill('0') << std::setw(2) << m << ":"
       << std::setfill('0') << std::setw(2) << s;
    return ss.str();
}

std::string Lx200Protocol::formatDec(double degrees) {
    char sign = degrees >= 0 ? '+' : '-';
    double abs_deg = std::abs(degrees);
    int d = static_cast<int>(std::floor(abs_deg));
    int m = static_cast<int>(std::floor((abs_deg - d) * 60));
    int s = static_cast<int>(std::round(((abs_deg - d) * 60 - m) * 60));
    std::ostringstream ss;
    ss << sign
       << std::setfill('0') << std::setw(2) << d << "*"
       << std::setfill('0') << std::setw(2) << m << ":"
       << std::setfill('0') << std::setw(2) << s;
    return ss.str();
}

bool Lx200Protocol::parseRA(const std::string& data, double& hours) {
    int h, m, s;
    if (sscanf(data.c_str(), "%d:%d:%d", &h, &m, &s) >= 2) {
        hours = h + m / 60.0 + s / 3600.0;
        return true;
    }
    return false;
}

bool Lx200Protocol::parseDec(const std::string& data, double& degrees) {
    char sign = '+';
    int d, m, s;
    if (sscanf(data.c_str(), "%c%d*%d:%d", &sign, &d, &m, &s) >= 4) {
        degrees = d + m / 60.0 + s / 3600.0;
        if (sign == '-') degrees = -degrees;
        return true;
    }
    return false;
}

double Lx200Coordinates::toDegrees() const {
    double val = hours + minutes / 60.0 + seconds / 3600.0;
    return negative ? -val : val;
}

double Lx200Coordinates::toHours() const {
    return toDegrees();
}

}} // namespace
