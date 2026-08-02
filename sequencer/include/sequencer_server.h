#ifndef SEQUENCER_SERVER_H
#define SEQUENCER_SERVER_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

namespace astro_sequencer {

class SequencerServiceImpl;

class SequencerServer {
public:
    SequencerServer(const std::string& server_address,
                    const std::string& sequencer_config_path,
                    bool enable_ssl = false,
                    const std::string& ssl_cert_path = "",
                    const std::string& ssl_key_path = "");
    ~SequencerServer();

    bool Start();
    void Stop();
    void Wait();

    SequencerServer(const SequencerServer&) = delete;
    SequencerServer& operator=(const SequencerServer&) = delete;
    SequencerServer(SequencerServer&&) = delete;
    SequencerServer& operator=(SequencerServer&&) = delete;

private:
    std::string server_address_;
    std::string sequencer_config_path_;
    bool enable_ssl_;
    std::string ssl_cert_path_;
    std::string ssl_key_path_;
    std::unique_ptr<SequencerServiceImpl> service_;
    std::unique_ptr<grpc::Server> server_;
};

} // namespace astro_sequencer

#endif // SEQUENCER_SERVER_H
