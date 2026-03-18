#pragma once

#ifdef DISTRIBUTED_MONITORING

#include <chrono>
#include <cstdint>
#include <string>

namespace btop::distributed {

/**
 * @brief Configuration structure for btop_client distributed mode
 */
struct ClientConfig {
    std::string mode = "local";  // "local" or "distributed"
    std::string run_mode = "interactive";  // "interactive" or "daemon"
    std::string server_address = "localhost";
    uint16_t server_port = 8080;
    std::string auth_token = "";
    uint32_t collection_interval_ms = 1000;
    bool enable_gpu = true;
    uint32_t reconnect_delay_ms = 5000;
    uint32_t max_reconnect_attempts = 10;
    std::string log_file = "";  // for daemon mode logging
    std::string pid_file = "";  // for daemon process management
};

/**
 * @brief Configuration structure for monitoring_server
 */
struct ServerConfig {
    uint16_t listen_port = 8080;
    std::string bind_address = "0.0.0.0";
    std::string auth_method = "token";  // "token" or "certificate"
    std::string auth_token = "";
    std::string cert_file = "";
    std::string key_file = "";
    uint32_t data_retention_hours = 24;
    uint32_t max_clients = 100;
    bool enable_tls = false;
    bool daemon_mode = false;
    std::string log_file = "";
    std::string pid_file = "";
    std::string database_path = "btop-monitoring.sqlite";
    std::string web_root = "";
    uint32_t client_stale_after_seconds = 15;
};

} // namespace btop::distributed

#endif // DISTRIBUTED_MONITORING
