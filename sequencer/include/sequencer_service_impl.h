#ifndef SEQUENCER_SERVICE_IMPL_H
#define SEQUENCER_SERVICE_IMPL_H

#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include "proto/sequencer.grpc.pb.h"
#include "proto/mount_controller.grpc.pb.h"

namespace astro_mount {
namespace sequencer {
class ObservationSequencer;
} // namespace sequencer
} // namespace astro_mount

namespace astro_sequencer {

class SequencerServiceImpl final : public astro_mount::SequencerService::Service {
public:
    explicit SequencerServiceImpl(const std::string& config_path);
    ~SequencerServiceImpl() override;

    grpc::Status LoadPlan(grpc::ServerContext* context,
                          const astro_mount::ObservationPlanProto* request,
                          astro_mount::SequencerResult* response) override;

    grpc::Status StartSequencer(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                astro_mount::SequencerResult* response) override;

    grpc::Status StopSequencer(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               astro_mount::SequencerResult* response) override;

    grpc::Status PauseSequencer(grpc::ServerContext* context,
                                const google::protobuf::Empty* request,
                                astro_mount::SequencerResult* response) override;

    grpc::Status GetSequencerStatus(grpc::ServerContext* context,
                                    const google::protobuf::Empty* request,
                                    astro_mount::SequencerStatus* response) override;

private:
    bool configureFromJson(const std::string& config_path);
    void setupCallbacks();

    std::unique_ptr<astro_mount::sequencer::ObservationSequencer> sequencer_;
    std::string config_path_;
    bool initialized_{false};
    bool simulated_{true};
    mutable std::mutex mutex_;

    // gRPC stub for mount controller callbacks (slew, track, park)
    std::unique_ptr<astro_mount::MountControllerService::Stub> mount_stub_;
};

} // namespace astro_sequencer

#endif // SEQUENCER_SERVICE_IMPL_H
