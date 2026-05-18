#include "config/config_monitor.h"
#include "logging/logger.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <atomic>

namespace astro_mount {
namespace config {

namespace fs = std::filesystem;

// Implementation of ConfigMonitor
class ConfigMonitor::Impl {
public:
    Impl(const std::string& config_file, int check_interval_ms)
        : config_file_(config_file)
        , check_interval_ms_(check_interval_ms)
        , last_modified_time_(0)
        , config_(std::make_unique<Configuration>()) {
        
        // Try to load initial configuration
        if (!config_->loadFromFile(config_file)) {
            API_LOG_WARN("Failed to load initial configuration from {}", config_file);
            *config_ = Configuration::getDefault();
        }
        
        // Get initial modification time
        updateLastModifiedTime();
    }
    
    ~Impl() {
        stopMonitoring();
    }
    
    bool startMonitoring() {
        if (monitoring_thread_.joinable()) {
            return false;
        }
        
        monitoring_ = true;
        monitoring_thread_ = std::thread(&Impl::monitorLoop, this);
        
        API_LOG_INFO("Started monitoring configuration file: {}", config_file_);
        return true;
    }
    
    void stopMonitoring() {
        monitoring_ = false;
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }
    
    const Configuration& getConfiguration() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return *config_;
    }
    
    bool reloadConfiguration() {
        std::unique_lock<std::mutex> lock(config_mutex_);
        
        auto old_config = std::make_unique<Configuration>(*config_);
        auto new_config = std::make_unique<Configuration>();
        
        if (!new_config->loadFromFile(config_file_)) {
            API_LOG_ERROR("Failed to reload configuration from {}", config_file_);
            return false;
        }
        
        // Validate new configuration
        auto errors = new_config->validate();
        if (!errors.empty()) {
            API_LOG_ERROR("Reloaded configuration has {} validation errors:", errors.size());
            for (const auto& error : errors) {
                API_LOG_ERROR("  - {}", error);
            }
            return false;
        }
        
        // Swap configurations
        config_.swap(new_config);
        
        // Update modification time
        updateLastModifiedTime();
        
        API_LOG_INFO("Configuration reloaded successfully from {}", config_file_);
        
        // Notify callbacks
        lock.unlock();
        if (change_callback_) {
            change_callback_(*config_);
        }
        
        return true;
    }
    
    void setConfigChangeCallback(ConfigChangeCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        change_callback_ = std::move(callback);
    }
    
private:
    void monitorLoop() {
        API_LOG_DEBUG("Configuration monitor thread started");
        
        while (monitoring_) {
            try {
                checkForChanges();
                std::this_thread::sleep_for(std::chrono::milliseconds(check_interval_ms_));
            } catch (const std::exception& e) {
                API_LOG_ERROR("Error in configuration monitor: {}", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // Wait 5s on error
            }
        }
        
        API_LOG_DEBUG("Configuration monitor thread stopped");
    }
    
    void checkForChanges() {
        if (!fs::exists(config_file_)) {
            API_LOG_WARN("Configuration file does not exist: {}", config_file_);
            return;
        }
        
        auto current_time = getFileModificationTime();
        
        if (current_time > last_modified_time_) {
            API_LOG_INFO("Configuration file changed, reloading...");
            if (reloadConfiguration()) {
                API_LOG_INFO("Configuration reloaded successfully");
            } else {
                API_LOG_ERROR("Failed to reload configuration");
            }
        }
    }
    
    std::time_t getFileModificationTime() const {
        try {
            auto ftime = fs::last_write_time(config_file_);
            // Convert file_time_type to system_clock::time_point
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            return std::chrono::system_clock::to_time_t(sctp);
        } catch (const std::exception& e) {
            API_LOG_ERROR("Failed to get file modification time: {}", e.what());
            return 0;
        }
    }
    
    void updateLastModifiedTime() {
        last_modified_time_ = getFileModificationTime();
    }
    
    std::string config_file_;
    int check_interval_ms_;
    std::atomic<bool> monitoring_{false};
    std::time_t last_modified_time_;
    
    std::unique_ptr<Configuration> config_;
    mutable std::mutex config_mutex_;
    
    ConfigChangeCallback change_callback_;
    std::mutex callback_mutex_;
    
    std::thread monitoring_thread_;
};

// ConfigMonitor public interface implementation
ConfigMonitor::ConfigMonitor(const std::string& config_file, int check_interval_ms)
    : config_file_(config_file)
    , check_interval_ms_(check_interval_ms)
    , impl_(std::make_unique<Impl>(config_file, check_interval_ms)) {}

ConfigMonitor::~ConfigMonitor() = default;

bool ConfigMonitor::start() {
    bool result = impl_->startMonitoring();
    if (result) {
        monitoring_ = true;
    }
    return result;
}

void ConfigMonitor::stop() {
    impl_->stopMonitoring();
    monitoring_ = false;
}

const Configuration& ConfigMonitor::getConfiguration() const {
    return impl_->getConfiguration();
}

bool ConfigMonitor::reload() {
    return impl_->reloadConfiguration();
}

void ConfigMonitor::setConfigChangeCallback(ConfigChangeCallback callback) {
    impl_->setConfigChangeCallback(std::move(callback));
}

// Implementation of ConfigNotifier
class ConfigNotifier::Impl {
public:
    Impl() : next_token_(1) {}
    
    ~Impl() = default;
    
    int subscribe(ConfigSection section, SectionChangeCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        int token = next_token_++;
        subscriptions_[token] = {section, std::move(callback)};
        return token;
    }
    
    void unsubscribe(int token) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.erase(token);
    }
    
    void notify(const ConfigChangeEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (const auto& [token, subscription] : subscriptions_) {
            if (subscription.section == ConfigSection::ALL || 
                subscription.section == event.section) {
                try {
                    subscription.callback(event);
                } catch (const std::exception& e) {
                    API_LOG_ERROR("Error in config change callback: {}", e.what());
                }
            }
        }
    }
    
private:
    struct Subscription {
        ConfigSection section;
        SectionChangeCallback callback;
    };
    
    std::unordered_map<int, Subscription> subscriptions_;
    std::mutex mutex_;
    int next_token_;
};

// ConfigNotifier public interface implementation
ConfigNotifier::ConfigNotifier() : impl_(std::make_unique<Impl>()) {}
ConfigNotifier::~ConfigNotifier() = default;

int ConfigNotifier::subscribe(ConfigSection section, SectionChangeCallback callback) {
    return impl_->subscribe(section, std::move(callback));
}

void ConfigNotifier::unsubscribe(int token) {
    impl_->unsubscribe(token);
}

void ConfigNotifier::notify(const ConfigChangeEvent& event) {
    impl_->notify(event);
}

// Implementation of ConfigManager
class ConfigManager::Impl {
public:
    Impl(const std::string& config_file, int check_interval_ms)
        : config_file_(config_file)
        , check_interval_ms_(check_interval_ms)
        , monitor_(config_file, check_interval_ms)
        , notifier_(std::make_unique<ConfigNotifier>()) {
        
        // Set up monitor callback to forward events
        monitor_.setConfigChangeCallback([this](const Configuration& new_config) {
            onConfigurationChanged(new_config);
        });
    }
    
    ~Impl() {
        shutdown();
    }
    
    bool initialize() {
        if (active_) {
            return true;
        }
        
        if (!monitor_.start()) {
            API_LOG_ERROR("Failed to start configuration monitor");
            return false;
        }
        
        active_ = true;
        API_LOG_INFO("Configuration manager initialized with file: {}", config_file_);
        return true;
    }
    
    void shutdown() {
        if (!active_) {
            return;
        }
        
        monitor_.stop();
        active_ = false;
        API_LOG_INFO("Configuration manager shutdown");
    }
    
    const Configuration& getConfiguration() const {
        return monitor_.getConfiguration();
    }
    
    bool reloadConfiguration() {
        if (!active_) {
            return monitor_.reload();
        }
        
        // Manual reload
        return monitor_.reload();
    }
    
    bool updateConfiguration(const Configuration& config) {
        std::lock_guard<std::mutex> lock(update_mutex_);
        
        // Validate before saving
        auto errors = validateConfiguration(config);
        if (!errors.empty()) {
            API_LOG_ERROR("Configuration validation failed:");
            for (const auto& error : errors) {
                API_LOG_ERROR("  - {}", error);
            }
            return false;
        }
        
        // Save to file
        if (!config.saveToFile(config_file_)) {
            API_LOG_ERROR("Failed to save configuration to {}", config_file_);
            return false;
        }
        
        API_LOG_INFO("Configuration updated and saved to {}", config_file_);
        return true;
    }
    
    int registerConfigChangeCallback(ConfigNotifier::ConfigSection section,
                                    ConfigNotifier::SectionChangeCallback callback) {
        return notifier_->subscribe(section, std::move(callback));
    }
    
    void unregisterConfigChangeCallback(int token) {
        notifier_->unsubscribe(token);
    }
    
    void onConfigurationChanged(const Configuration& new_config) {
        // Compare with old configuration to determine what changed
        static Configuration old_config;
        
        ConfigNotifier::ConfigChangeEvent event;
        event.new_config = new_config;
        event.old_config = old_config;
        event.timestamp = std::chrono::system_clock::now();
        
        // Determine which section changed (simplified - in real implementation
        // we would compare field by field)
        event.section = ConfigNotifier::ConfigSection::ALL;
        event.field_changed = "full_reload";
        
        // Notify subscribers
        notifier_->notify(event);
        
        // Update old config
        old_config = new_config;
    }
    
    bool isActive() const { return active_; }
    
private:
    std::string config_file_;
    int check_interval_ms_;
    std::atomic<bool> active_{false};
    
    ConfigMonitor monitor_;
    std::unique_ptr<ConfigNotifier> notifier_;
    std::mutex update_mutex_;
};

// ConfigManager public interface implementation
ConfigManager::ConfigManager(const std::string& config_file, int check_interval_ms)
    : config_file_(config_file)
    , check_interval_ms_(check_interval_ms)
    , impl_(std::make_unique<Impl>(config_file, check_interval_ms)) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::initialize() {
    return impl_->initialize();
}

void ConfigManager::shutdown() {
    impl_->shutdown();
}

const Configuration& ConfigManager::getConfiguration() const {
    return impl_->getConfiguration();
}

bool ConfigManager::reloadConfiguration() {
    return impl_->reloadConfiguration();
}

bool ConfigManager::updateConfiguration(const Configuration& config) {
    return impl_->updateConfiguration(config);
}

int ConfigManager::registerConfigChangeCallback(
    ConfigNotifier::ConfigSection section,
    ConfigNotifier::SectionChangeCallback callback) {
    return impl_->registerConfigChangeCallback(section, std::move(callback));
}

void ConfigManager::unregisterConfigChangeCallback(int token) {
    impl_->unregisterConfigChangeCallback(token);
}

bool ConfigManager::isActive() const {
    return impl_->isActive();
}

std::string ConfigManager::getConfigFilePath() const {
    return config_file_;
}

std::vector<std::string> ConfigManager::validateConfiguration(const Configuration& config) {
    return config.validate();
}

} // namespace config
} // namespace astro_mount