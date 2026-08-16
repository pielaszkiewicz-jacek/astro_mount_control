#ifndef POWER_SERVICE_IMPL_H
#define POWER_SERVICE_IMPL_H

#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <grpcpp/grpcpp.h>
#include "proto/power.grpc.pb.h"

namespace astro_mount {
namespace controllers {
class PowerManager;
} // namespace controllers
} // namespace astro_mount

namespace astro_power {

class PowerServiceImpl final : public astro_mount::PowerService::Service {
public:
    explicit PowerServiceImpl(const std::string& config_path);
    ~PowerServiceImpl() override;

    grpc::Status GetPowerStatus(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                astro_mount::PowerStatus* response) override;

    grpc::Status SetPowerOutput(grpc::ServerContext* context,
                                const astro_mount::PowerOutputRequest* request,
                                google::protobuf::Empty* response) override;

    grpc::Status GetPowerHistory(grpc::ServerContext* context,
                                 const astro_mount::PowerHistoryRequest* request,
                                 astro_mount::PowerHistoryResponse* response) override;

private:
    void populatePowerStatus(astro_mount::PowerStatus* status) const;
    bool configureFromJson(const std::string& config_path);
    void recordHistory();

    std::unique_ptr<astro_mount::controllers::PowerManager> manager_;
    std::string config_path_;
    bool initialized_{false};
    mutable std::mutex mutex_;

    // In-memory history ring buffer of power snapshots (guarded by mutex_).
    std::vector<astro_mount::PowerStatus> history_;
    static constexpr size_t kHistoryMax{600};
};

} // namespace astro_power

#endif // POWER_SERVICE_IMPL_H
