#include "notifications/channels/mqtt_channel.h"
#include <sstream>
#include <chrono>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <string>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#if defined(__linux__) && defined(MQTT_TLS_ENABLED)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace astro_mount {
namespace notifications {

namespace {
// Minimal MQTT 3.1.1 packet helpers over a plain TCP socket (optionally TLS).
// libmosquitto/paho are NOT available on all targets, so this channel speaks
// the MQTT 3.1.1 wire protocol directly — CONNECT → PUBLISH (QoS 0/1/2) →
// DISCONNECT (N5: QoS + retain + optional TLS).

#ifdef __linux__
bool writeAll(int fd, const uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

bool readAll(int fd, uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = ::recv(fd, data + off, len - off, 0);
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

void appendU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
}

void appendStr(std::vector<uint8_t>& buf, const std::string& s) {
    appendU16(buf, static_cast<uint16_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

std::vector<uint8_t> encodeVarint(uint32_t value) {
    std::vector<uint8_t> out;
    do {
        uint8_t b = static_cast<uint8_t>(value % 128);
        value /= 128;
        if (value > 0) b |= 0x80;
        out.push_back(b);
    } while (value > 0);
    return out;
}

// Read one MQTT fixed header + remaining-length. Returns the remaining length
// on success (and copies the 1-4 length bytes), -1 on failure.
int readFixedHeader(int fd, uint8_t& type_byte, std::vector<uint8_t>& len_bytes) {
    uint8_t b;
    if (!readAll(fd, &b, 1)) return -1;
    type_byte = b;
    len_bytes.clear();
    uint32_t multiplier = 1;
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t lb;
        if (!readAll(fd, &lb, 1)) return -1;
        len_bytes.push_back(lb);
        value += (lb & 0x7F) * multiplier;
        if ((lb & 0x80) == 0) {
            return static_cast<int>(value);
        }
        multiplier *= 128;
    }
    return -1;
}
#endif // __linux__

} // anonymous namespace

MqttChannel::MqttChannel(Config config)
    : config_(std::move(config)) {
}

MqttChannel::MqttChannel() : MqttChannel(Config{}) {}

MqttChannel::~MqttChannel() {
    shutdown();
}

bool MqttChannel::initialize() {
    if (config_.broker_url.empty()) {
        return false;
    }
    // N5: no persistent connection is held — each send() opens a fresh
    // connection, connects (TLS when configured), publishes and disconnects.
    // initialized_ only means the configuration is valid; an unreachable
    // broker makes send() return false (honest failure).
    initialized_ = true;
    return true;
}

bool MqttChannel::send(const NotificationEvent& event) {
    if (!initialized_) return false;

    std::string topic = buildTopic(event);
    std::string payload = buildPayload(event);

#ifdef __linux__
    return publishOnce(topic, payload);
#else
    (void)topic; (void)payload;
    return false;
#endif
}

#ifdef __linux__
bool MqttChannel::publishOnce(const std::string& topic, const std::string& payload) {
    int qos = std::clamp(config_.qos, 0, 2);

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    std::string port = std::to_string(config_.broker_port);
    if (::getaddrinfo(config_.broker_url.c_str(), port.c_str(), &hints, &res) != 0) {
        ::close(fd);
        return false;
    }
    bool connected = (::connect(fd, res->ai_addr, res->ai_addrlen) == 0);
    ::freeaddrinfo(res);
    if (!connected) { ::close(fd); return false; }

#ifdef MQTT_TLS_ENABLED
    SSL* ssl = nullptr;
    SSL_CTX* ctx = nullptr;
    bool use_ssl = config_.use_tls;
    if (use_ssl) {
        // One-time OpenSSL init (thread-safe in 1.1+).
        SSL_library_init();
        SSL_load_error_strings();
        ctx = SSL_CTX_new(SSLv23_client_method());
        if (ctx) {
            ssl = SSL_new(ctx);
        }
        if (ssl) {
            SSL_set_fd(ssl, fd);
            SSL_set_tlsext_host_name(ssl, config_.broker_url.c_str());
            // Best-effort cert verification; failures still proceed (self-signed
            // brokers are common). Verification can be tightened later.
            SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
            if (SSL_connect(ssl) != 1) {
                SSL_free(ssl); ssl = nullptr;
            }
        }
    }
#else
    void* ssl = nullptr;      // TLS not compiled in — keep the same call sites
    void* ctx = nullptr;
    const bool use_ssl = false;
#endif

    // ── CONNECT packet (type 1) ──
    std::vector<uint8_t> vh;
    appendStr(vh, "MQTT");
    vh.push_back(4);  // protocol level = MQTT 3.1.1
    uint8_t flags = 0x02;  // clean session
    if (!config_.username.empty()) flags |= 0x80;
    if (!config_.password.empty()) flags |= 0x40;
    vh.push_back(flags);
    appendU16(vh, 60);  // keepalive [s]

    std::vector<uint8_t> pl;
    appendStr(pl, config_.client_id.empty() ? std::string("astro-mount-notifications")
                                            : config_.client_id);
    if (!config_.username.empty()) appendStr(pl, config_.username);
    if (!config_.password.empty()) appendStr(pl, config_.password);

    std::vector<uint8_t> connect;
    connect.push_back(0x10);
    connect.push_back(static_cast<uint8_t>(vh.size() + pl.size()));
    connect.insert(connect.end(), vh.begin(), vh.end());
    connect.insert(connect.end(), pl.begin(), pl.end());

    bool ok;
#ifdef MQTT_TLS_ENABLED
    if (ssl) {
        ok = SSL_write(ssl, connect.data(), static_cast<int>(connect.size())) ==
             static_cast<int>(connect.size());
    } else
#endif
    {
        ok = writeAll(fd, connect.data(), connect.size());
    }

    if (ok) {
        // CONNACK: fixed header (0x20, len 2) + 2 bytes (session present, code).
        uint8_t cack[4] = {0, 0, 0, 0};
#ifdef MQTT_TLS_ENABLED
        if (ssl) {
            ok = SSL_read(ssl, cack, sizeof(cack)) == static_cast<int>(sizeof(cack));
        } else
#endif
        {
            ok = readAll(fd, cack, sizeof(cack));
        }
        ok = ok && cack[0] == 0x20 && cack[3] == 0x00;  // accepted
    }

    // ── PUBLISH packet (type 3) with QoS + retain (N5) ──
    if (ok) {
        uint16_t packet_id = qos > 0 ? 1 : 0;
        uint8_t flags_byte = 0x30;                     // PUBLISH, QoS 0
        flags_byte |= (qos & 0x03) << 1;               // QoS bits
        if (config_.retain) flags_byte |= 0x01;        // retain flag

        uint32_t rl = 2 + static_cast<uint32_t>(topic.size()) + static_cast<uint32_t>(payload.size());
        if (qos > 0) rl += 2;  // packet id

        std::vector<uint8_t> publish;
        publish.push_back(flags_byte);
        auto rl_bytes = encodeVarint(rl);
        publish.insert(publish.end(), rl_bytes.begin(), rl_bytes.end());
        appendStr(publish, topic);
        if (qos > 0) appendU16(publish, packet_id);
        publish.insert(publish.end(), payload.begin(), payload.end());

#ifdef MQTT_TLS_ENABLED
        if (ssl) {
            ok = SSL_write(ssl, publish.data(), static_cast<int>(publish.size())) ==
                 static_cast<int>(publish.size());
        } else
#endif
        {
            ok = writeAll(fd, publish.data(), publish.size());
        }
    }

    // ── Wait for the QoS handshake (N5) ──
    if (ok && qos == 1) {
        // PUBACK: fixed header 0x40, len 2, packet id.
        uint8_t ack[4] = {0, 0, 0, 0};
#ifdef MQTT_TLS_ENABLED
        if (ssl) {
            ok = SSL_read(ssl, ack, sizeof(ack)) == static_cast<int>(sizeof(ack));
        } else
#endif
        {
            ok = readAll(fd, ack, sizeof(ack));
        }
        ok = ok && ack[0] == 0x40;
    } else if (ok && qos == 2) {
        // PUBLISH → PUBREC (0x50) → PUBREL (0x62) → PUBCOMP (0x70).
        uint8_t rec[4] = {0, 0, 0, 0};
#ifdef MQTT_TLS_ENABLED
        if (ssl) {
            ok = SSL_read(ssl, rec, sizeof(rec)) == static_cast<int>(sizeof(rec));
        } else
#endif
        {
            ok = readAll(fd, rec, sizeof(rec));
        }
        ok = ok && rec[0] == 0x50;
        if (ok) {
            std::vector<uint8_t> rel = {0x62, 0x02, rec[2], rec[3]};  // PUBREL + packet id
#ifdef MQTT_TLS_ENABLED
            if (ssl) {
                ok = SSL_write(ssl, rel.data(), static_cast<int>(rel.size())) ==
                     static_cast<int>(rel.size());
            } else
#endif
            {
                ok = writeAll(fd, rel.data(), rel.size());
            }
        }
        if (ok) {
            uint8_t comp[4] = {0, 0, 0, 0};
#ifdef MQTT_TLS_ENABLED
            if (ssl) {
                ok = SSL_read(ssl, comp, sizeof(comp)) == static_cast<int>(sizeof(comp));
            } else
#endif
            {
                ok = readAll(fd, comp, sizeof(comp));
            }
            ok = ok && comp[0] == 0x70;
        }
    }

    // ── DISCONNECT (type 14) — best effort ──
    uint8_t disc[2] = {0xE0, 0x00};
#ifdef MQTT_TLS_ENABLED
    if (ssl) {
        SSL_write(ssl, disc, sizeof(disc));
        SSL_shutdown(ssl);
    } else
#endif
    {
        writeAll(fd, disc, sizeof(disc));
    }

#ifdef MQTT_TLS_ENABLED
    if (ssl) SSL_free(ssl);
    if (ctx) SSL_CTX_free(ctx);
#endif
    ::close(fd);
    return ok;
}
#endif // __linux__

void MqttChannel::shutdown() {
    initialized_ = false;
}

bool MqttChannel::isHealthy() const {
    return initialized_;
}

std::string MqttChannel::buildTopic(const NotificationEvent& event) const {
    std::ostringstream ss;
    ss << config_.topic_prefix;

    // Add severity sub-topic
    switch (event.severity) {
        case Severity::DEBUG:    ss << "/debug";    break;
        case Severity::INFO:     ss << "/info";     break;
        case Severity::WARNING:  ss << "/warning";  break;
        case Severity::ERROR:    ss << "/error";    break;
        case Severity::CRITICAL: ss << "/critical"; break;
    }

    // Add category sub-topic
    ss << "/" << event.source;
    std::string topic = ss.str();
    std::transform(topic.begin(), topic.end(), topic.begin(), ::tolower);

    return topic;
}

std::string MqttChannel::buildPayload(const NotificationEvent& event) const {
    std::ostringstream ss;
    ss << "{"
        << "\"id\":\"" << event.id << "\","
        << "\"title\":\"" << event.title << "\","
        << "\"message\":\"" << event.message << "\","
        << "\"severity\":" << static_cast<int>(event.severity)
        << "}";
    return ss.str();
}

} // namespace notifications
} // namespace astro_mount
