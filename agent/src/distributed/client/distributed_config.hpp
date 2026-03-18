#pragma once

#ifdef DISTRIBUTED_MONITORING

#include "../common/config.hpp"
#include <chrono>

namespace btop::distributed::client {

/**
 * @brief Configuration manager for distributed btop_client
 * 
 * Handles configuration parsing and management for operating modes,
 * daemon mode configuration, and server connection parameters.
 */
class DistributedConfig {
public:
    enum class OperatingMode { LOCAL, DISTRIBUTED };
    enum class RunMode { INTERACTIVE, DAEMON };
    
    // Getters
    OperatingMode getMode() const;
    RunMode getRunMode() const;
    std::string getServerAddress() const;
    uint16_t getServerPort() const;
    std::string getAuthToken() const;
    std::chrono::milliseconds getCollectionInterval() const;
    bool isGpuEnabled() const;
    bool isDaemonMode() const;
    std::string getLogFile() const;
    std::string getPidFile() const;
    uint32_t getReconnectDelay() const;
    uint32_t getMaxReconnectAttempts() const;
    
    // Setters
    void setMode(OperatingMode mode);
    void setRunMode(RunMode run_mode);
    void setServerAddress(const std::string& address);
    void setServerPort(uint16_t port);
    void setAuthToken(const std::string& token);
    void setCollectionInterval(std::chrono::milliseconds interval);
    void setGpuEnabled(bool enabled);
    void setLogFile(const std::string& log_file);
    void setPidFile(const std::string& pid_file);
    void setReconnectDelay(uint32_t delay_ms);
    void setMaxReconnectAttempts(uint32_t attempts);
    
    // Configuration loading, saving and validation
    bool loadFromFile(const std::string& config_path);
    bool saveToFile(const std::string& config_path) const;
    bool validate() const;
    
private:
    ClientConfig config_;
};

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING