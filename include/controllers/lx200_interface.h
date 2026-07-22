#ifndef LX200_INTERFACE_H
#define LX200_INTERFACE_H

#include <memory>
#include <string>
#include <functional>
#include "controllers/lx200_protocol.h"

namespace astro_mount { namespace controllers {

class Lx200Interface {
public:
    virtual ~Lx200Interface() = default;
    virtual bool connect(const std::string& port, int baud) = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual Lx200CommandResult sendCommand(const std::string& cmd) = 0;
    virtual Lx200CommandResult sendCommand(Lx200Command cmd) = 0;
    virtual bool getRaDec(double& ra, double& dec) = 0;
    virtual bool slewTo(double ra, double dec) = 0;
    virtual bool syncTo(double ra, double dec) = 0;
    virtual bool stop() = 0;
    virtual std::string getVersion() = 0;
};

class Lx200SerialInterface : public Lx200Interface {
public:
    explicit Lx200SerialInterface(const std::string& port = "/dev/ttyS0", int baud = 9600);
    ~Lx200SerialInterface() override;

    bool connect(const std::string& port, int baud) override;
    void disconnect() override;
    bool isConnected() const override;
    Lx200CommandResult sendCommand(const std::string& cmd) override;
    Lx200CommandResult sendCommand(Lx200Command cmd) override;
    bool getRaDec(double& ra, double& dec) override;
    bool slewTo(double ra, double dec) override;
    bool syncTo(double ra, double dec) override;
    bool stop() override;
    std::string getVersion() override;

private:
    std::string readResponse(int timeout_ms = 1000);
    bool writeCommand(const std::string& cmd);

    std::string port_;
    int baud_;
    int fd_{-1};
    bool connected_{false};
};

}} // namespace astro_mount::controllers
#endif
