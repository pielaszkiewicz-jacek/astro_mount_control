#include "controllers/canopen_interface.h"
#include "logging/logger.h"
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <nlohmann/json.hpp>

// In a real implementation, this would use a CANopen library like CANopenSocket, libedssharp, or CANFestival
// For this example, we simulate CANopen communication

namespace astro_mount {
namespace controllers {

using json = nlohmann::json;

class CanOpenInterface::Impl {
    friend class CanOpenInterface;
public:
    Impl() : connected_(false), running_(false), sync_thread_running_(false),
             pdo_thread_running_(false) {
        // Initialize axis data
        for (int i = 0; i < 2; ++i) {
            axis_status_[i] = DriveStatus{};
            axis_position_[i] = PositionData{};
            axis_encoder_[i] = EncoderData{};
            axis_enabled_[i] = false;
            axis_target_position_[i] = 0.0;
            axis_target_velocity_[i] = 0.0;
        }
    }
    
    ~Impl() {
        shutdown();
    }
    
    bool initialize(const CanOpenConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        config_ = config;
        connected_ = false;
        
        // Initialize simulated CAN bus
        logging::Logger::get("canopen")->info("CANopen: Initializing interface {} at {} bps",
                  config.interface_name, config.bitrate);
        
        return true;
    }
    
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            sync_thread_running_ = false;
            pdo_thread_running_ = false;
        }
        
        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }
        
        if (pdo_thread_.joinable()) {
            pdo_thread_.join();
        }
        
        disconnect();
    }
    
    bool connect() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (connected_) {
            return true;
        }
        
        // Simulate CAN connection
        logging::Logger::get("canopen")->info("CANopen: Connecting to {} (node {})",
                  config_.interface_name, config_.node_id);
        
        // Start SYNC thread if configured
        if (config_.use_sync && config_.sync_period_ms > 0) {
            sync_thread_running_ = true;
            sync_thread_ = std::thread([this]() {
                syncThreadFunction();
            });
        }
        
        // Start PDO receive thread – symuluje odbiór ramek PDO z magistrali CAN
        pdo_thread_running_ = true;
        pdo_thread_ = std::thread([this]() {
            pdoReceiveThread();
        });
        
        connected_ = true;
        running_ = true;
        
        return true;
    }
    
    void disconnect() {
        // Najpierw zatrzymaj wątki poza mutexem, żeby uniknąć deadlocka
        // (pdoReceiveThread i syncThreadFunction trzymają mutex)
        pdo_thread_running_ = false;
        sync_thread_running_ = false;
        running_ = false;
        
        if (pdo_thread_.joinable()) {
            pdo_thread_.join();
        }
        if (sync_thread_.joinable()) {
            sync_thread_.join();
        }
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (!connected_) {
                return;
            }
            
            // Disable all axes (bezpośrednio, nie przez disableDrive – unikamy lock)
            for (int i = 0; i < 2; ++i) {
                axis_enabled_[i] = false;
                axis_target_velocity_[i] = 0.0;
                axis_status_[i].enabled = false;
                axis_status_[i].operational = false;
                axis_status_[i].timestamp = std::chrono::system_clock::now();
            }
            
            connected_ = false;
            
            logging::Logger::get("canopen")->info("CANopen: Disconnected");
        }
    }
    
    bool isConnected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connected_;
    }
    
    bool configureDrive(int axis_id, const std::string& config_string) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            json config = json::parse(config_string);
            
            // Parse configuration (simplified)
            // In real implementation, this would configure object dictionary entries
            
            logging::Logger::get("canopen")->info("CANopen: Configuring axis {}", axis_id);
            return true;
        } catch (const std::exception& e) {
            logging::Logger::get("canopen")->error("CANopen: Configuration error: {}", e.what());
            return false;
        }
    }
    
    bool enableDrive(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            return false;
        }
        
        // Simulate drive enable sequence
        axis_enabled_[axis_id] = true;
        axis_status_[axis_id].enabled = true;
        axis_status_[axis_id].operational = true;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        logging::Logger::get("canopen")->info("CANopen: Axis {} enabled", axis_id);
        
        // Notify status callback
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
        
        return true;
    }
    
    void disableDrive(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        axis_enabled_[axis_id] = false;
        axis_status_[axis_id].enabled = false;
        axis_status_[axis_id].operational = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Notify status callback
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
    }
    
    bool setPositionTarget(int axis_id, double position, double velocity, double acceleration) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_ || !axis_enabled_[axis_id]) {
            return false;
        }
        
        axis_target_position_[axis_id] = position;
        axis_target_velocity_[axis_id] = velocity;
        
        // Simulate position update
        axis_position_[axis_id].target_position = position;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
        
        axis_status_[axis_id].moving = true;
        axis_status_[axis_id].target_reached = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Start simulated movement
        std::thread([this, axis_id, position]() {
            simulateMovement(axis_id, position);
        }).detach();
        
        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
        
        return true;
    }
    
    bool setVelocityTarget(int axis_id, double velocity, double acceleration) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_ || !axis_enabled_[axis_id]) {
            return false;
        }
        
        axis_target_velocity_[axis_id] = velocity;
        
        // Simulate velocity control
        axis_position_[axis_id].actual_velocity = velocity;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
        
        axis_status_[axis_id].moving = (velocity != 0.0);
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
        
        return true;
    }
    
    void stopAxis(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        axis_target_velocity_[axis_id] = 0.0;
        axis_position_[axis_id].actual_velocity = 0.0;
        axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
        
        axis_status_[axis_id].moving = false;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Notify callbacks
        if (position_callback_) {
            position_callback_(axis_id, axis_position_[axis_id]);
        }
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
    }
    
    void emergencyStop(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        disableDrive(axis_id);
        axis_target_velocity_[axis_id] = 0.0;
        axis_position_[axis_id].actual_velocity = 0.0;
        
        axis_status_[axis_id].error = true;
        axis_status_[axis_id].error_code = 0x8000; // Emergency stop
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Notify error callback
        if (error_callback_) {
            error_callback_(axis_id, "Emergency stop");
        }
    }
    
    bool clearErrors(int axis_id) {
        if (axis_id < 0 || axis_id >= 2) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        axis_status_[axis_id].error = false;
        axis_status_[axis_id].warning = false;
        axis_status_[axis_id].error_code = 0;
        axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
        
        // Notify status callback
        if (status_callback_) {
            status_callback_(axis_id, axis_status_[axis_id]);
        }
        
        return true;
    }
    
    DriveStatus getDriveStatus(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (axis_id < 0 || axis_id >= 2) {
            return DriveStatus{};
        }
        
        return axis_status_[axis_id];
    }
    
    PositionData getPositionData(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (axis_id < 0 || axis_id >= 2) {
            return PositionData{};
        }
        
        return axis_position_[axis_id];
    }
    
    EncoderData getEncoderData(int axis_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (axis_id < 0 || axis_id >= 2) {
            return EncoderData{};
        }
        
        return axis_encoder_[axis_id];
    }
    
    void setStatusCallback(StatusCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_callback_ = callback;
    }
    
    void setPositionCallback(PositionCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        position_callback_ = callback;
    }
    
    void setEncoderCallback(EncoderCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        encoder_callback_ = callback;
    }
    
    void setErrorCallback(ErrorCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_callback_ = callback;
    }
    
    bool sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                 const void* data, size_t data_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            return false;
        }
        
        // Simulate SDO write
        logging::Logger::get("canopen")->info("CANopen: SDO write to axis {} index=0x{:04X} subindex=0x{:02X} size={}",
                  axis_id, index, subindex, data_size);
        
        return true;
    }
    
    int receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                   void* data, size_t data_size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            return -1;
        }
        
        // Simulate SDO read
        logging::Logger::get("canopen")->info("CANopen: SDO read from axis {} index=0x{:04X} subindex=0x{:02X}",
                  axis_id, index, subindex);
        
        // Return simulated data
        if (data_size >= 4) {
            uint32_t value = 0x12345678;
            memcpy(data, &value, 4);
            return 4;
        }
        
        return 0;
    }
    
    bool configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_ || pdo_number < 1 || pdo_number > 4) {
            return false;
        }
        
        // Simulate PDO configuration
        logging::Logger::get("canopen")->info("CANopen: Configuring PDO {} for axis {}", pdo_number, axis_id);
        
        return true;
    }
    
    void enablePDO(int axis_id, int pdo_number, bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_ || pdo_number < 1 || pdo_number > 4) {
            return;
        }
        
        logging::Logger::get("canopen")->info("CANopen: {} PDO {} for axis {}", (enable ? "Enabling" : "Disabling"), pdo_number, axis_id);
    }
    
    bool sendNMT(uint8_t node_id, uint8_t command) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            return false;
        }
        
        // Simulate NMT command transmission
        // In real CANopen: send CAN frame COB-ID 0x000 with data[0]=command, data[1]=node_id
        logging::Logger::get("canopen")->info("CANopen: Sending NMT command 0x{:02X} to node {}", command, node_id);
        
        // Handle NMT commands - update internal state accordingly
        if (command == 0x81 || command == 0x82) {
            // Reset Node or Reset Communication - simulate re-initialization
            for (int i = 0; i < 2; ++i) {
                axis_enabled_[i] = false;
                axis_target_velocity_[i] = 0.0;
                axis_target_position_[i] = 0.0;
                axis_position_[i].actual_position = 0.0;
                axis_position_[i].actual_velocity = 0.0;
                axis_encoder_[i].raw_position = 0;
                axis_encoder_[i].raw_velocity = 0;
                axis_status_[i].enabled = false;
                axis_status_[i].operational = false;
                axis_status_[i].moving = false;
                axis_status_[i].error = false;
                
                if (status_callback_) {
                    status_callback_(i, axis_status_[i]);
                }
            }
        } else if (command == 0x02) {
            // Stop Remote Node
            if (node_id == 0) {
                for (int i = 0; i < 2; ++i) {
                    axis_enabled_[i] = false;
                    axis_target_velocity_[i] = 0.0;
                    axis_status_[i].enabled = false;
                    axis_status_[i].operational = false;
                    axis_status_[i].moving = false;
                }
            } else {
                int idx = node_id - 1;
                if (idx >= 0 && idx < 2) {
                    axis_enabled_[idx] = false;
                    axis_target_velocity_[idx] = 0.0;
                    axis_status_[idx].enabled = false;
                    axis_status_[idx].operational = false;
                    axis_status_[idx].moving = false;
                }
            }
        } else if (command == 0x01) {
            // Start Remote Node
            if (node_id == 0) {
                for (int i = 0; i < 2; ++i) {
                    axis_enabled_[i] = true;
                    axis_status_[i].enabled = true;
                    axis_status_[i].operational = true;
                }
            } else {
                int idx = node_id - 1;
                if (idx >= 0 && idx < 2) {
                    axis_enabled_[idx] = true;
                    axis_status_[idx].enabled = true;
                    axis_status_[idx].operational = true;
                }
            }
        }
        
        return true;
    }
    
    void sendSync() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            return;
        }
        
        // Simulate SYNC message
        // In real implementation, this would send CAN frame with ID 0x80
        
        // Update all axes on SYNC
        for (int i = 0; i < 2; ++i) {
            if (axis_enabled_[i]) {
                // Simulate encoder updates
                axis_encoder_[i].raw_position += static_cast<uint32_t>(axis_position_[i].actual_velocity * 100);
                axis_encoder_[i].timestamp = std::chrono::system_clock::now();
                
                // Notify encoder callback
                if (encoder_callback_) {
                    encoder_callback_(i, axis_encoder_[i]);
                }
            }
        }
    }
    
    std::string getStatistics() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        json stats;
        stats["connected"] = connected_;
        stats["running"] = running_;
        stats["interface"] = config_.interface_name;
        stats["bitrate"] = config_.bitrate;
        stats["node_id"] = config_.node_id;
        
        for (int i = 0; i < 2; ++i) {
            stats["axis" + std::to_string(i)]["enabled"] = axis_enabled_[i];
            stats["axis" + std::to_string(i)]["position"] = axis_position_[i].actual_position;
            stats["axis" + std::to_string(i)]["velocity"] = axis_position_[i].actual_velocity;
        }
        
        return stats.dump(4);
    }
    
    bool saveConfiguration(const std::string& filename) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            json config;
            config["interface_name"] = config_.interface_name;
            config["bitrate"] = config_.bitrate;
            config["node_id"] = config_.node_id;
            config["use_sync"] = config_.use_sync;
            config["sync_period_ms"] = config_.sync_period_ms;
            config["sdo_timeout_ms"] = config_.sdo_timeout_ms;
            
            std::ofstream file(filename);
            if (!file.is_open()) {
                return false;
            }
            
            file << config.dump(4);
            return true;
        } catch (const std::exception& e) {
            logging::Logger::get("canopen")->error("CANopen: Save configuration error: {}", e.what());
            return false;
        }
    }
    
    bool loadConfiguration(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }
            
            json config = json::parse(file);
            
            config_.interface_name = config.value("interface_name", "can0");
            config_.bitrate = config.value("bitrate", 125000);
            config_.node_id = config.value("node_id", 1);
            config_.use_sync = config.value("use_sync", true);
            config_.sync_period_ms = config.value("sync_period_ms", 100);
            config_.sdo_timeout_ms = config.value("sdo_timeout_ms", 1000);
            
            return true;
        } catch (const std::exception& e) {
            logging::Logger::get("canopen")->error("CANopen: Load configuration error: {}", e.what());
            return false;
        }
    }
    
    void syncThreadFunction() {
        while (sync_thread_running_) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (connected_ && config_.use_sync) {
                    sendSync();
                }
            }
            
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.sync_period_ms));
        }
    }
    
    /**
     * @brief PDO receive thread – symuluje odbiór rzeczywistych ramek PDO z magistrali CAN.
     * 
     * W rzeczywistym systemie CANopen serwonapędy wysyłają cyklicznie ramki PDO
     * zawierające aktualną pozycję, prędkość i status. Ten wątek symuluje to
     * dla trybu prędkościowego (velocity mode), całkując prędkość do pozycji.
     */
    void pdoReceiveThread() {
        const double update_rate = 100.0; // Hz – typowa szybkość PDO
        const auto period = std::chrono::milliseconds(1000 / static_cast<int>(update_rate));
        
        while (pdo_thread_running_) {
            std::this_thread::sleep_for(period);
            
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (!connected_ || !running_) continue;
            
            const double dt = 1.0 / update_rate; // seconds per tick
            
            for (int i = 0; i < 2; ++i) {
                if (!axis_enabled_[i]) continue;
                
                // Całkuj prędkość do pozycji (symulacja odczytu PDO z enkodera)
                double vel = axis_target_velocity_[i];
                axis_position_[i].actual_position += vel * dt;
                axis_position_[i].actual_velocity = vel;
                axis_position_[i].timestamp = std::chrono::system_clock::now();
                
                // Aktualizuj enkoder
                axis_encoder_[i].raw_position =
                    static_cast<uint32_t>(axis_position_[i].actual_position * 1000.0);
                axis_encoder_[i].raw_velocity =
                    static_cast<uint32_t>(std::abs(vel) * 1000.0);
                axis_encoder_[i].timestamp = std::chrono::system_clock::now();
                
                // Aktualizuj status
                axis_status_[i].moving = (std::abs(vel) > 0.0001);
                axis_status_[i].timestamp = std::chrono::system_clock::now();
                
                // Wywołaj callbacki (symulacja odebrania PDO)
                if (position_callback_) {
                    position_callback_(i, axis_position_[i]);
                }
                if (encoder_callback_) {
                    encoder_callback_(i, axis_encoder_[i]);
                }
                if (status_callback_) {
                    status_callback_(i, axis_status_[i]);
                }
            }
        }
    }
    
    void simulateMovement(int axis_id, double target_position) {
        const double max_velocity = 10.0; // deg/s
        const double acceleration = 2.0; // deg/s²
        const double update_rate = 100.0; // Hz
        
        double current_position = axis_position_[axis_id].actual_position;
        double current_velocity = 0.0;
        double distance = target_position - current_position;
        double direction = (distance > 0) ? 1.0 : -1.0;
        
        // Acceleration phase
        while (std::abs(current_velocity) < max_velocity && 
               std::abs(target_position - current_position) > 0.1) {
            current_velocity += direction * acceleration / update_rate;
            current_position += current_velocity / update_rate;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                axis_position_[axis_id].actual_position = current_position;
                axis_position_[axis_id].actual_velocity = current_velocity;
                axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Update encoder
                axis_encoder_[axis_id].raw_position = 
                    static_cast<uint32_t>(current_position * 1000.0);
                axis_encoder_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Notify callbacks
                if (position_callback_) {
                    position_callback_(axis_id, axis_position_[axis_id]);
                }
                if (encoder_callback_) {
                    encoder_callback_(axis_id, axis_encoder_[axis_id]);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000/static_cast<int>(update_rate)));
        }
        
        // Constant velocity phase
        while (std::abs(target_position - current_position) > std::abs(current_velocity * current_velocity / (2.0 * acceleration))) {
            current_position += current_velocity / update_rate;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                axis_position_[axis_id].actual_position = current_position;
                axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Update encoder
                axis_encoder_[axis_id].raw_position = 
                    static_cast<uint32_t>(current_position * 1000.0);
                axis_encoder_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Notify callbacks
                if (position_callback_) {
                    position_callback_(axis_id, axis_position_[axis_id]);
                }
                if (encoder_callback_) {
                    encoder_callback_(axis_id, axis_encoder_[axis_id]);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000/static_cast<int>(update_rate)));
        }
        
        // Deceleration phase
        while (std::abs(target_position - current_position) > 0.01) {
            if (std::abs(current_velocity) > 0.01) {
                current_velocity -= direction * acceleration / update_rate;
            }
            current_position += current_velocity / update_rate;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                axis_position_[axis_id].actual_position = current_position;
                axis_position_[axis_id].actual_velocity = current_velocity;
                axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Update encoder
                axis_encoder_[axis_id].raw_position = 
                    static_cast<uint32_t>(current_position * 1000.0);
                axis_encoder_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Notify callbacks
                if (position_callback_) {
                    position_callback_(axis_id, axis_position_[axis_id]);
                }
                if (encoder_callback_) {
                    encoder_callback_(axis_id, axis_encoder_[axis_id]);
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1000/static_cast<int>(update_rate)));
        }
        
        // Final position
        {
            std::lock_guard<std::mutex> lock(mutex_);
            axis_position_[axis_id].actual_position = target_position;
            axis_position_[axis_id].actual_velocity = 0.0;
            axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
            
            axis_status_[axis_id].moving = false;
            axis_status_[axis_id].target_reached = true;
            axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
            
            // Update encoder
            axis_encoder_[axis_id].raw_position = 
                static_cast<uint32_t>(target_position * 1000.0);
            axis_encoder_[axis_id].timestamp = std::chrono::system_clock::now();
            
            // Notify callbacks
            if (position_callback_) {
                position_callback_(axis_id, axis_position_[axis_id]);
            }
            if (encoder_callback_) {
                encoder_callback_(axis_id, axis_encoder_[axis_id]);
            }
            if (status_callback_) {
                status_callback_(axis_id, axis_status_[axis_id]);
            }
        }
    }
    
private:
    mutable std::mutex mutex_;
    CanOpenConfig config_;
    bool connected_;
    bool running_;
    bool sync_thread_running_;
    std::thread sync_thread_;
    bool pdo_thread_running_;
    std::thread pdo_thread_;
    
    DriveStatus axis_status_[2];
    PositionData axis_position_[2];
    EncoderData axis_encoder_[2];
    bool axis_enabled_[2];
    double axis_target_position_[2];
    double axis_target_velocity_[2];
    
    double trajectory_last_time_{0.0};
    
    StatusCallback status_callback_;
    PositionCallback position_callback_;
    EncoderCallback encoder_callback_;
    ErrorCallback error_callback_;
};

// Public interface implementation
CanOpenInterface::CanOpenInterface() 
    : pimpl(std::make_unique<Impl>()) {}

CanOpenInterface::~CanOpenInterface() = default;

bool CanOpenInterface::initialize(const CanOpenConfig& config) {
    return pimpl->initialize(config);
}

void CanOpenInterface::shutdown() {
    pimpl->shutdown();
}

bool CanOpenInterface::connect() {
    return pimpl->connect();
}

void CanOpenInterface::disconnect() {
    pimpl->disconnect();
}

bool CanOpenInterface::isConnected() const {
    return pimpl->isConnected();
}

bool CanOpenInterface::configureDrive(int axis_id, const std::string& config_string) {
    return pimpl->configureDrive(axis_id, config_string);
}

bool CanOpenInterface::enableDrive(int axis_id) {
    return pimpl->enableDrive(axis_id);
}

void CanOpenInterface::disableDrive(int axis_id) {
    pimpl->disableDrive(axis_id);
}

bool CanOpenInterface::setPositionTarget(int axis_id, double position, double velocity, double acceleration) {
    return pimpl->setPositionTarget(axis_id, position, velocity, acceleration);
}

bool CanOpenInterface::setVelocityTarget(int axis_id, double velocity, double acceleration) {
    return pimpl->setVelocityTarget(axis_id, velocity, acceleration);
}

void CanOpenInterface::stopAxis(int axis_id) {
    pimpl->stopAxis(axis_id);
}

void CanOpenInterface::emergencyStop(int axis_id) {
    pimpl->emergencyStop(axis_id);
}

bool CanOpenInterface::clearErrors(int axis_id) {
    return pimpl->clearErrors(axis_id);
}

CanOpenInterface::DriveStatus CanOpenInterface::getDriveStatus(int axis_id) const {
    return pimpl->getDriveStatus(axis_id);
}

CanOpenInterface::PositionData CanOpenInterface::getPositionData(int axis_id) const {
    return pimpl->getPositionData(axis_id);
}

CanOpenInterface::EncoderData CanOpenInterface::getEncoderData(int axis_id) const {
    return pimpl->getEncoderData(axis_id);
}

void CanOpenInterface::setStatusCallback(StatusCallback callback) {
    pimpl->setStatusCallback(callback);
}

void CanOpenInterface::setPositionCallback(PositionCallback callback) {
    pimpl->setPositionCallback(callback);
}

void CanOpenInterface::setEncoderCallback(EncoderCallback callback) {
    pimpl->setEncoderCallback(callback);
}

void CanOpenInterface::setErrorCallback(ErrorCallback callback) {
    pimpl->setErrorCallback(callback);
}

bool CanOpenInterface::sendSDO(int axis_id, uint16_t index, uint8_t subindex, 
                               const void* data, size_t data_size) {
    return pimpl->sendSDO(axis_id, index, subindex, data, data_size);
}

int CanOpenInterface::receiveSDO(int axis_id, uint16_t index, uint8_t subindex,
                                 void* data, size_t data_size) {
    return pimpl->receiveSDO(axis_id, index, subindex, data, data_size);
}

bool CanOpenInterface::configurePDO(int axis_id, int pdo_number, const std::vector<uint32_t>& mapping) {
    return pimpl->configurePDO(axis_id, pdo_number, mapping);
}

void CanOpenInterface::enablePDO(int axis_id, int pdo_number, bool enable) {
    pimpl->enablePDO(axis_id, pdo_number, enable);
}

bool CanOpenInterface::sendNMT(uint8_t node_id, uint8_t command) {
    return pimpl->sendNMT(node_id, command);
}

void CanOpenInterface::sendSync() {
    pimpl->sendSync();
}

std::string CanOpenInterface::getStatistics() const {
    return pimpl->getStatistics();
}

bool CanOpenInterface::saveConfiguration(const std::string& filename) const {
    return pimpl->saveConfiguration(filename);
}

bool CanOpenInterface::loadConfiguration(const std::string& filename) {
    return pimpl->loadConfiguration(filename);
}

std::vector<CanOpenInterface::TrajectoryPoint> 
CanOpenInterface::generateTrajectory(const TrajectoryParams& params) {
    std::vector<TrajectoryPoint> trajectory;
    
    double distance = params.target_position - params.start_position;
    double direction = (distance > 0) ? 1.0 : -1.0;
    double abs_distance = std::abs(distance);
    
    if (params.type == TRAPEZOIDAL) {
        // Trapezoidal velocity profile
        double t_acc = params.max_velocity / params.max_acceleration;
        double s_acc = 0.5 * params.max_acceleration * t_acc * t_acc;
        
        if (2.0 * s_acc <= abs_distance) {
            // Trapezoidal profile: acceleration - constant velocity - deceleration
            double t_const = (abs_distance - 2.0 * s_acc) / params.max_velocity;
            double total_time = 2.0 * t_acc + t_const;
            
            double dt = 1.0 / params.update_rate;
            for (double t = 0; t <= total_time; t += dt) {
                TrajectoryPoint point;
                point.time = t;
                
                if (t < t_acc) {
                    // Acceleration phase
                    point.acceleration = params.max_acceleration * direction;
                    point.velocity = point.acceleration * t;
                    point.position = params.start_position + 0.5 * point.acceleration * t * t;
                    point.jerk = 0.0;
                } else if (t < t_acc + t_const) {
                    // Constant velocity phase
                    point.velocity = params.max_velocity * direction;
                    point.acceleration = 0.0;
                    point.position = params.start_position + s_acc * direction + 
                                    point.velocity * (t - t_acc);
                    point.jerk = 0.0;
                } else {
                    // Deceleration phase
                    double t_dec = t - (t_acc + t_const);
                    point.acceleration = -params.max_acceleration * direction;
                    point.velocity = params.max_velocity * direction + point.acceleration * t_dec;
                    point.position = params.start_position + direction * (abs_distance - 
                                    0.5 * params.max_acceleration * t_dec * t_dec);
                    point.jerk = 0.0;
                }
                
                trajectory.push_back(point);
            }
        } else {
            // Triangular profile: acceleration - deceleration (no constant velocity)
            double t_acc_tri = std::sqrt(abs_distance / params.max_acceleration);
            double total_time = 2.0 * t_acc_tri;
            
            double dt = 1.0 / params.update_rate;
            for (double t = 0; t <= total_time; t += dt) {
                TrajectoryPoint point;
                point.time = t;
                
                if (t < t_acc_tri) {
                    // Acceleration phase
                    point.acceleration = params.max_acceleration * direction;
                    point.velocity = point.acceleration * t;
                    point.position = params.start_position + 0.5 * point.acceleration * t * t;
                    point.jerk = 0.0;
                } else {
                    // Deceleration phase
                    double t_dec = t - t_acc_tri;
                    point.acceleration = -params.max_acceleration * direction;
                    point.velocity = params.max_acceleration * t_acc_tri * direction + 
                                    point.acceleration * t_dec;
                    point.position = params.start_position + direction * (abs_distance - 
                                    0.5 * params.max_acceleration * t_dec * t_dec);
                    point.jerk = 0.0;
                }
                
                trajectory.push_back(point);
            }
        }
        
    } else if (params.type == S_SHAPE) {
        // S-curve (jerk-limited) profile
        double t_jerk = params.max_acceleration / params.max_jerk;
        double t_acc = params.max_velocity / params.max_acceleration - t_jerk;
        
        if (t_acc > 0) {
            // 7-phase S-curve: jerk+ -> acc+ -> jerk- -> const vel -> jerk- -> acc- -> jerk+
            double s_jerk = params.max_jerk * t_jerk * t_jerk * t_jerk / 6.0;
            double s_acc = 0.5 * params.max_acceleration * t_acc * t_acc + 
                          params.max_acceleration * t_acc * t_jerk;
            double s_const = params.max_velocity * t_acc;
            
            double total_distance_phase = 2.0 * (s_jerk + s_acc) + s_const;
            
            if (total_distance_phase <= abs_distance) {
                // Full S-curve with constant velocity phase
                double t_const = (abs_distance - total_distance_phase) / params.max_velocity;
                double total_time = 4.0 * t_jerk + 2.0 * t_acc + t_const;
                
                double dt = 1.0 / params.update_rate;
                for (double t = 0; t <= total_time; t += dt) {
                    TrajectoryPoint point;
                    point.time = t;
                    
                    if (t < t_jerk) {
                        // Phase 1: Jerk positive
                        point.jerk = params.max_jerk * direction;
                        point.acceleration = point.jerk * t;
                        point.velocity = 0.5 * point.jerk * t * t;
                        point.position = params.start_position + point.jerk * t * t * t / 6.0;
                    } else if (t < t_jerk + t_acc) {
                        // Phase 2: Constant acceleration
                        double t1 = t - t_jerk;
                        point.jerk = 0.0;
                        point.acceleration = params.max_acceleration * direction;
                        point.velocity = 0.5 * params.max_jerk * t_jerk * t_jerk + 
                                        point.acceleration * t1;
                        point.position = params.start_position + direction * 
                                        (params.max_jerk * t_jerk * t_jerk * t_jerk / 6.0 +
                                         0.5 * params.max_acceleration * t1 * t1 +
                                         params.max_acceleration * t_jerk * t1);
                    } else if (t < 2.0 * t_jerk + t_acc) {
                        // Phase 3: Jerk negative
                        double t2 = t - (t_jerk + t_acc);
                        point.jerk = -params.max_jerk * direction;
                        point.acceleration = params.max_acceleration * direction + point.jerk * t2;
                        point.velocity = params.max_velocity * direction + 
                                        0.5 * point.jerk * t2 * t2;
                        point.position = params.start_position + direction * 
                                        (s_jerk + s_acc + params.max_velocity * t2 +
                                         point.jerk * t2 * t2 * t2 / 6.0);
                    } else if (t < 2.0 * t_jerk + t_acc + t_const) {
                        // Phase 4: Constant velocity
                        double t3 = t - (2.0 * t_jerk + t_acc);
                        point.jerk = 0.0;
                        point.acceleration = 0.0;
                        point.velocity = params.max_velocity * direction;
                        point.position = params.start_position + direction * 
                                        (2.0 * (s_jerk + s_acc) + point.velocity * t3);
                    } else if (t < 3.0 * t_jerk + t_acc + t_const) {
                        // Phase 5: Jerk negative (deceleration start)
                        double t4 = t - (2.0 * t_jerk + t_acc + t_const);
                        point.jerk = -params.max_jerk * direction;
                        point.acceleration = point.jerk * t4;
                        point.velocity = params.max_velocity * direction + 
                                        0.5 * point.jerk * t4 * t4;
                        point.position = params.start_position + direction * 
                                        (abs_distance - s_jerk - s_acc - 
                                         params.max_velocity * t4 -
                                         point.jerk * t4 * t4 * t4 / 6.0);
                    } else if (t < 3.0 * t_jerk + 2.0 * t_acc + t_const) {
                        // Phase 6: Constant deceleration
                        double t5 = t - (3.0 * t_jerk + t_acc + t_const);
                        point.jerk = 0.0;
                        point.acceleration = -params.max_acceleration * direction;
                        point.velocity = params.max_velocity * direction - 
                                        params.max_acceleration * t5 * direction;
                        point.position = params.start_position + direction * 
                                        (abs_distance - s_jerk - 0.5 * params.max_acceleration * t5 * t5 -
                                         params.max_acceleration * t_jerk * t5);
                    } else {
                        // Phase 7: Jerk positive (final phase)
                        double t6 = t - (3.0 * t_jerk + 2.0 * t_acc + t_const);
                        point.jerk = params.max_jerk * direction;
                        point.acceleration = -params.max_acceleration * direction + point.jerk * t6;
                        point.velocity = point.jerk * (t6 - t_jerk) * (t6 - t_jerk) / 2.0;
                        point.position = params.target_position - 
                                        point.jerk * (t_jerk - t6) * (t_jerk - t6) * (t_jerk - t6) / 6.0;
                    }
                    
                    trajectory.push_back(point);
                }
            } else {
                // S-curve without constant velocity phase (triangular S-curve)
                // Simplified implementation for short moves
                double t_acc_s = std::sqrt(abs_distance / params.max_acceleration);
                double total_time = 2.0 * t_acc_s + 2.0 * t_jerk;
                
                double dt = 1.0 / params.update_rate;
                for (double t = 0; t <= total_time; t += dt) {
                    TrajectoryPoint point;
                    point.time = t;
                    
                    if (t < t_jerk) {
                        // Phase 1: Jerk positive
                        point.jerk = params.max_jerk * direction;
                        point.acceleration = point.jerk * t;
                        point.velocity = 0.5 * point.jerk * t * t;
                        point.position = params.start_position + point.jerk * t * t * t / 6.0;
                    } else if (t < t_jerk + t_acc_s) {
                        // Phase 2: Constant acceleration
                        double t1 = t - t_jerk;
                        point.jerk = 0.0;
                        point.acceleration = params.max_acceleration * direction;
                        point.velocity = 0.5 * params.max_jerk * t_jerk * t_jerk + 
                                        point.acceleration * t1;
                        point.position = params.start_position + direction * 
                                        (params.max_jerk * t_jerk * t_jerk * t_jerk / 6.0 +
                                         0.5 * params.max_acceleration * t1 * t1 +
                                         params.max_acceleration * t_jerk * t1);
                    } else if (t < 2.0 * t_jerk + t_acc_s) {
                        // Phase 3: Jerk negative (to peak velocity)
                        double t2 = t - (t_jerk + t_acc_s);
                        point.jerk = -params.max_jerk * direction;
                        point.acceleration = params.max_acceleration * direction + point.jerk * t2;
                        point.velocity = params.max_acceleration * (t_acc_s + t_jerk) * direction + 
                                        0.5 * point.jerk * t2 * t2;
                        point.position = params.start_position + direction * 
                                        (abs_distance / 2.0 + point.velocity * t2 +
                                         point.jerk * t2 * t2 * t2 / 6.0);
                    } else if (t < 2.0 * t_jerk + 2.0 * t_acc_s) {
                        // Phase 4: Constant deceleration
                        double t3 = t - (2.0 * t_jerk + t_acc_s);
                        point.jerk = 0.0;
                        point.acceleration = -params.max_acceleration * direction;
                        point.velocity = params.max_acceleration * (t_acc_s + t_jerk) * direction - 
                                        params.max_acceleration * t3 * direction;
                        point.position = params.start_position + direction * 
                                        (abs_distance - abs_distance / 4.0 -
                                         0.5 * params.max_acceleration * t3 * t3 -
                                         params.max_acceleration * t_jerk * t3);
                    } else {
                        // Phase 5: Jerk positive (final phase)
                        double t4 = t - (2.0 * t_jerk + 2.0 * t_acc_s);
                        point.jerk = params.max_jerk * direction;
                        point.acceleration = -params.max_acceleration * direction + point.jerk * t4;
                        point.velocity = point.jerk * (t4 - t_jerk) * (t4 - t_jerk) / 2.0;
                        point.position = params.target_position - 
                                        point.jerk * (t_jerk - t4) * (t_jerk - t4) * (t_jerk - t4) / 6.0;
                    }
                    
                    trajectory.push_back(point);
                }
            }
        }
        
    } else if (params.type == SINE) {
        // Sine-based smooth profile
        double total_time = abs_distance / params.max_velocity * 2.0; // Approximation
        double omega = M_PI / total_time;
        
        double dt = 1.0 / params.update_rate;
        for (double t = 0; t <= total_time; t += dt) {
            TrajectoryPoint point;
            point.time = t;
            
            // Sine-based velocity profile
            point.velocity = params.max_velocity * direction * std::sin(omega * t);
            point.acceleration = params.max_velocity * direction * omega * std::cos(omega * t);
            point.jerk = -params.max_velocity * direction * omega * omega * std::sin(omega * t);
            point.position = params.start_position + direction * 
                            (params.max_velocity / omega * (1.0 - std::cos(omega * t)));
            
            trajectory.push_back(point);
        }
        
    } else if (params.type == POLYNOMIAL) {
        // 5th order polynomial profile
        double total_time = abs_distance / params.max_velocity * 1.5; // Approximation
        
        // Polynomial coefficients for smooth motion
        // Position: p(t) = a0 + a1*t + a2*t² + a3*t³ + a4*t⁴ + a5*t⁵
        // Boundary conditions: p(0)=start, p(T)=target, v(0)=v(T)=a(0)=a(T)=0
        double T = total_time;
        double a0 = params.start_position;
        double a1 = 0.0;
        double a2 = 0.0;
        double a3 = 10.0 * distance / (T * T * T);
        double a4 = -15.0 * distance / (T * T * T * T);
        double a5 = 6.0 * distance / (T * T * T * T * T);
        
        double dt = 1.0 / params.update_rate;
        for (double t = 0; t <= total_time; t += dt) {
            TrajectoryPoint point;
            point.time = t;
            
            double t2 = t * t;
            double t3 = t2 * t;
            double t4 = t3 * t;
            double t5 = t4 * t;
            
            point.position = a0 + a1*t + a2*t2 + a3*t3 + a4*t4 + a5*t5;
            point.velocity = a1 + 2.0*a2*t + 3.0*a3*t2 + 4.0*a4*t3 + 5.0*a5*t4;
            point.acceleration = 2.0*a2 + 6.0*a3*t + 12.0*a4*t2 + 20.0*a5*t3;
            point.jerk = 6.0*a3 + 24.0*a4*t + 60.0*a5*t2;
            
            trajectory.push_back(point);
        }
    }
    
    // Ensure final point is exactly at target
    if (!trajectory.empty()) {
        trajectory.back().position = params.target_position;
        trajectory.back().velocity = 0.0;
        trajectory.back().acceleration = 0.0;
        trajectory.back().jerk = 0.0;
    }
    
    return trajectory;
}

bool CanOpenInterface::executeTrajectory(int axis_id, 
                                        const std::vector<TrajectoryPoint>& trajectory,
                                        std::function<void(const TrajectoryPoint&)> callback) {
    if (axis_id < 0 || axis_id >= 2 || trajectory.empty()) {
        return false;
    }
    
    // Start trajectory execution in background thread
    std::thread([this, axis_id, trajectory, callback]() {
        for (const auto& point : trajectory) {
            {
                std::lock_guard<std::mutex> lock(pimpl->mutex_);
                
                // Update axis position
                pimpl->axis_position_[axis_id].actual_position = point.position;
                pimpl->axis_position_[axis_id].actual_velocity = point.velocity;
                pimpl->axis_position_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Update encoder
                pimpl->axis_encoder_[axis_id].raw_position = 
                    static_cast<uint32_t>(point.position * 1000.0);
                pimpl->axis_encoder_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Update status
                pimpl->axis_status_[axis_id].moving = true;
                pimpl->axis_status_[axis_id].target_reached = false;
                pimpl->axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
                
                // Notify callbacks
                if (pimpl->position_callback_) {
                    pimpl->position_callback_(axis_id, pimpl->axis_position_[axis_id]);
                }
                if (pimpl->encoder_callback_) {
                    pimpl->encoder_callback_(axis_id, pimpl->axis_encoder_[axis_id]);
                }
                if (pimpl->status_callback_) {
                    pimpl->status_callback_(axis_id, pimpl->axis_status_[axis_id]);
                }
            }
            
            // Call trajectory callback if provided
            if (callback) {
                callback(point);
            }
            
            // Sleep for trajectory update rate
            // Calculate dt from trajectory points
            double dt = (pimpl->trajectory_last_time_ > 0) ? (point.time - pimpl->trajectory_last_time_) : 0.01; // Default 100 Hz
            pimpl->trajectory_last_time_ = point.time;
            
            if (dt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(dt * 1000)));
            }
        }
        
        // Final status update
        {
            std::lock_guard<std::mutex> lock(pimpl->mutex_);
            pimpl->axis_status_[axis_id].moving = false;
            pimpl->axis_status_[axis_id].target_reached = true;
            pimpl->axis_status_[axis_id].timestamp = std::chrono::system_clock::now();
            
            if (pimpl->status_callback_) {
                pimpl->status_callback_(axis_id, pimpl->axis_status_[axis_id]);
            }
        }
    }).detach();
    
    return true;
}

} // namespace controllers
} // namespace astro_mount
