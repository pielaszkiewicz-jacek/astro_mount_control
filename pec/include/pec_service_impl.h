#ifndef PEC_SERVICE_IMPL_H
#define PEC_SERVICE_IMPL_H

#include <memory>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>
#include "proto/pec.grpc.pb.h"

namespace astro_mount { namespace models { class PECModel; } }

/**
 * @brief gRPC implementation of the PEC service (P2).
 *
 * Wraps the PECModel (training → DFT → harmonic correction) and exposes it
 * over gRPC. Hosted in-process inside the mount controller on the unified
 * gRPC port (50051), like the dome/derotator/focuser subsystems.
 *
 * Training is driven by synthetic periodic-error samples when no external
 * encoder source is wired up, so the full pipeline (sample collection → FFT →
 * harmonic reconstruction) is demonstrable end-to-end.
 */
namespace astro_pec {

class PecServiceImpl final : public astro_mount::PECService::Service {
public:
    explicit PecServiceImpl(const std::string& config_path);
    ~PecServiceImpl() override;

    grpc::Status StartTraining(grpc::ServerContext* context,
                               const astro_mount::PECTrainingConfig* request,
                               grpc::ServerWriter<astro_mount::PECTrainingProgress>* writer) override;

    grpc::Status StopTraining(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              google::protobuf::Empty* response) override;

    grpc::Status GetPECStatus(grpc::ServerContext* context,
                              const google::protobuf::Empty* request,
                              astro_mount::PECStatus* response) override;

    grpc::Status SetPECEnabled(grpc::ServerContext* context,
                               const astro_mount::PECEnableRequest* request,
                               google::protobuf::Empty* response) override;

    grpc::Status SavePECData(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

    grpc::Status LoadPECData(grpc::ServerContext* context,
                             const google::protobuf::Empty* request,
                             google::protobuf::Empty* response) override;

private:
    void populateStatus(astro_mount::PECStatus* status) const;
    bool saveToJson() const;
    bool loadFromJson();

    std::unique_ptr<astro_mount::models::PECModel> pec_;
    std::string config_path_;
    std::string data_path_;
    bool enabled_{false};
    mutable std::mutex mutex_;
};

} // namespace astro_pec

#endif // PEC_SERVICE_IMPL_H
