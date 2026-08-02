#ifndef FOCUSER_SERVICE_IMPL_H
#define FOCUSER_SERVICE_IMPL_H

#include <memory>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>
#include "proto/focuser.grpc.pb.h"

namespace astro_mount { namespace hal { class FocuserControl; } }

namespace astro_focuser {

class FocuserServiceImpl final : public astro_mount::FocuserService::Service {
public:
    explicit FocuserServiceImpl(const std::string& config_path);
    ~FocuserServiceImpl() override;

    grpc::Status MoveFocuser(grpc::ServerContext* context,
                             const astro_mount::FocuserMoveRequest* request,
                             google::protobuf::Empty* response) override;
    grpc::Status HaltFocuser(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;
    grpc::Status GetFocuserPosition(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    astro_mount::FocuserStatus* response) override;
    grpc::Status RunAutoFocus(grpc::ServerContext* context,
                              const astro_mount::AutoFocusRequest* request,
                              grpc::ServerWriter<astro_mount::AutoFocusProgress>* writer) override;
    grpc::Status SetTemperatureCompensation(grpc::ServerContext* context,
                                            const astro_mount::TempCompConfig* request,
                                            google::protobuf::Empty* response) override;
    grpc::Status GetTempCompensationStatus(grpc::ServerContext* context,
                                           const google::protobuf::Empty* request,
                                           astro_mount::TempCompStatus* response) override;

private:
    bool configureFromJson(const std::string& config_path);
    void populateStatus(astro_mount::FocuserStatus* status) const;

    std::unique_ptr<astro_mount::hal::FocuserControl> focuser_;
    bool initialized_{false};
    bool simulated_{true};
    mutable std::mutex mutex_;

    // Temperature compensation state
    struct TempCompState {
        bool enabled{false};
        double coefficient{0.0};
        int32_t max_adjustment{500};
        double reference_temp{20.0};
        int32_t total_adjustment{0};
    };
    TempCompState temp_comp_;
};

} // namespace astro_focuser

#endif // FOCUSER_SERVICE_IMPL_H
