#pragma once

#ifdef DISTRIBUTED_MONITORING

#include "../common/metrics_data.hpp"
#include "../common/auth_interface.hpp"
#include <string>
#include <functional>
#include <cstdint>
#include <memory>
#include <chrono>
#include <optional>

namespace btop::distributed::client {

using btop::distributed::Credentials;
using btop::distributed::AuthManager;
using btop::distributed::AuthResult;

struct RemoteAgentConfig {
    std::string hostname;
    std::string display_name;
    std::uint32_t collection_interval_ms = 1000;
    bool enable_gpu = true;
};

/**
 * @brief Network client for connecting to monitoring_server
 * 
 * Manages TCP connections, TLS support, authentication, and metrics transmission.
 */
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    
    bool connect(const std::string& address, uint16_t port);
    bool authenticate(const std::string& token);
    bool authenticateWithCredentials(const Credentials& credentials);
    std::optional<RemoteAgentConfig> fetchAgentConfig(const std::string& hostname);
    bool sendMetrics(const MetricsData& data);
    bool isConnected() const;
    bool isAuthenticated() const;
    void disconnect();
    void setReconnectCallback(std::function<void()> callback);
    
    // Authentication management
    void setCredentials(const Credentials& credentials);
    void setAuthManager(std::shared_ptr<AuthManager> auth_manager);
    bool refreshAuthentication();
    AuthResult getLastAuthResult() const;
    
    // Connection management
    void enableTLS(bool enable);
    void setConnectionTimeout(std::chrono::milliseconds timeout);
    void setReconnectDelay(std::chrono::milliseconds delay);
    
    // Buffering and retry configuration
    void setMaxBufferSize(size_t size);
    void setMaxRetryAttempts(size_t attempts);
    size_t getBufferedMetricsCount() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
