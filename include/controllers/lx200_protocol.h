#ifndef LX200_PROTOCOL_H
#define LX200_PROTOCOL_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace astro_mount { namespace controllers {

struct Lx200Coordinates {
    int hours{0}, minutes{0}, seconds{0};
    bool negative{false};

    double toDegrees() const;
    double toHours() const;
    std::string toString(bool is_ra) const;

    static Lx200Coordinates fromDouble(double value, bool is_ra);
};

enum class Lx200Command {
    NONE,
    MS,  // :MS#  Slew to target
    MN,  // :MN#  Slew to target (sync)
    Sr,  // :Sr#  Set RA
    Sd,  // :Sd#  Set Dec
    GR,  // :GR#  Get RA
    GD,  // :GD#  Get Dec
    CM,  // :CM#  Sync
    Q,   // :Q#   Stop
    AP,  // :AP#  Slew to Alt/Az
    AL,  // :AL#  Set Alt
    AZ,  // :AZ#  Set Az
    GA,  // :GA#  Get Alt
    GZ,  // :GZ#  Get Az
    Gc,  // :Gc#  Get time (LST)
    GC,  // :GC#  Get date
    St,  // :St#  Set date
    SL,  // :SL#  Set site longitude
    Sg,  // :Sg#  Set site latitude
    Gt,  // :Gt#  Get site longitude
    Gg,  // :Gg#  Get site latitude
    hP,  // :hP#  Set pier side
    GU,  // :GU#  Get tracking rate
    RM,  // :RM#  Set tracking rate (sidereal)
    RL,  // :RL#  Set tracking rate (lunar)
    RS,  // :RS#  Set tracking rate (solar)
    RQ,  // :RQ#  Set tracking rate (0 = off)
    U2,  // :U2#  Set tracking freq
    GV,  // :GV#  Get version
    GVD, // :GVD# Get version date
    GVF, // :GVF# Get version firmware
};

struct Lx200CommandResult {
    bool success{false};
    std::string response;
    std::string error;
};

class Lx200Protocol {
public:
    static Lx200Command parseCommand(const std::string& cmd);
    static std::string formatResponse(const Lx200CommandResult& result);

    // Coordinate formatting
    static std::string formatRA(double hours);
    static std::string formatDec(double degrees);
    static bool parseRA(const std::string& data, double& hours);
    static bool parseDec(const std::string& data, double& degrees);

    // Standard LX200 command strings
    static constexpr const char* CMD_GET_RA = ":GR#";
    static constexpr const char* CMD_GET_DEC = ":GD#";
    static constexpr const char* CMD_SLEW = ":MS#";
    static constexpr const char* CMD_SYNC = ":CM#";
    static constexpr const char* CMD_STOP = ":Q#";
    static constexpr const char* CMD_GET_VERSION = ":GV#";
};

}} // namespace astro_mount::controllers
#endif
