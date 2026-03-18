#ifdef DISTRIBUTED_MONITORING

#include "distributed_config.hpp"
#include "../common/config_parser.hpp"

namespace btop::distributed::client {

DistributedConfig::OperatingMode DistributedConfig::getMode() const {
    if (config_.mode == "distributed") {
        return OperatingMode::DISTRIBUTED;
    }
    return OperatingMode::LOCAL;
}

DistributedConfig::RunMode DistributedConfig::getRunMode() const {
    if (config_.run_mode == "daemon") {
        return RunMode::DAEMON;
    }
    return RunMode::INTERACTIVE;
}

std::string DistributedConfig::getServerAddress() const {
    return config_.server_address;
}

uint16_t DistributedConfig::getServerPort() const {
    return config_.server_port;
}

std::string DistributedConfig::getAuthToken() const {
    return config_.auth_token;
}

std::chrono::milliseconds DistributedConfig::getCollectionInterval() const {
    return std::chrono::milliseconds(config_.collection_interval_ms);
}

bool DistributedConfig::isGpuEnabled() const {
    return config_.enable_gpu;
}

bool DistributedConfig::isDaemonMode() const {
    return getRunMode() == RunMode::DAEMON;
}

bool DistributedConfig::loadFromFile(const std::string& config_path) {
    return ConfigParser::parseClientConfig(config_path, config_);
}

bool DistributedConfig::validate() const {
    return ConfigParser::validateClientConfig(config_);
}

bool DistributedConfig::saveToFile(const std::string& config_path) const {
    return ConfigParser::writeClientConfig(config_path, config_);
}

void DistributedConfig::setMode(OperatingMode mode) {
    config_.mode = (mode == OperatingMode::DISTRIBUTED) ? "distributed" : "local";
}

void DistributedConfig::setRunMode(RunMode run_mode) {
    config_.run_mode = (run_mode == RunMode::DAEMON) ? "daemon" : "interactive";
}

void DistributedConfig::setServerAddress(const std::string& address) {
    config_.server_address = address;
}

void DistributedConfig::setServerPort(uint16_t port) {
    config_.server_port = port;
}

void DistributedConfig::setAuthToken(const std::string& token) {
    config_.auth_token = token;
}

void DistributedConfig::setCollectionInterval(std::chrono::milliseconds interval) {
    config_.collection_interval_ms = static_cast<uint32_t>(interval.count());
}

void DistributedConfig::setGpuEnabled(bool enabled) {
    config_.enable_gpu = enabled;
}

void DistributedConfig::setLogFile(const std::string& log_file) {
    config_.log_file = log_file;
}

void DistributedConfig::setPidFile(const std::string& pid_file) {
    config_.pid_file = pid_file;
}

std::string DistributedConfig::getLogFile() const {
    return config_.log_file;
}

std::string DistributedConfig::getPidFile() const {
    return config_.pid_file;
}

uint32_t DistributedConfig::getReconnectDelay() const {
    return config_.reconnect_delay_ms;
}

uint32_t DistributedConfig::getMaxReconnectAttempts() const {
    return config_.max_reconnect_attempts;
}

void DistributedConfig::setReconnectDelay(uint32_t delay_ms) {
    config_.reconnect_delay_ms = delay_ms;
}

void DistributedConfig::setMaxReconnectAttempts(uint32_t attempts) {
    config_.max_reconnect_attempts = attempts;
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING