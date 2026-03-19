#ifdef DISTRIBUTED_MONITORING

#include "daemon_manager.hpp"
#include "network_client.hpp"
#include "../common/auth_interface.hpp"
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <queue>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <optional>
#include <regex>

namespace btop::distributed::client {

// Helper struct for CURL response data
struct CurlResponse {
    std::string data;
    long response_code = 0;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, CurlResponse* response) {
        size_t total_size = size * nmemb;
        response->data.append(static_cast<char*>(contents), total_size);
        return total_size;
	}
};

int shutdownAwareProgressCallback(void* /*clientp*/, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
	return isShutdownRequested() ? 1 : 0;
}

class NetworkClient::Impl {
public:
    Impl() : curl_(nullptr), connected_(false), authenticated_(false), tls_enabled_(false),
             last_auth_result_(AuthResult::SUCCESS),
             connection_timeout_(std::chrono::seconds(30)),
             reconnect_delay_(std::chrono::seconds(5)),
	             max_buffer_size_(1000), retry_attempts_(0), max_retry_attempts_(5),
             base_retry_delay_(std::chrono::seconds(1)) {
        // Initialize libcurl
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_ = curl_easy_init();
        
	        if (curl_) {
	            // Set common options
	            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, CurlResponse::WriteCallback);
	            curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
	            curl_easy_setopt(curl_, CURLOPT_TIMEOUT, connection_timeout_.count());
	            curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
	            curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
	            curl_easy_setopt(curl_, CURLOPT_USERAGENT, "btop-client/1.0");
	            curl_easy_setopt(curl_, CURLOPT_PROXY, "");
	            curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, 0L);
	            curl_easy_setopt(curl_, CURLOPT_XFERINFOFUNCTION, shutdownAwareProgressCallback);
	        }
	    }
    
    ~Impl() {
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
        curl_global_cleanup();
    }
    
	    bool connect(const std::string& address, uint16_t port) {
	        if (!curl_) {
	            return false;
	        }
	        if (isShutdownRequested()) {
	            return false;
	        }
        
        server_address_ = address;
        server_port_ = port;
        
        // Build base URL
        std::ostringstream url_stream;
        if (tls_enabled_) {
            url_stream << "https://";
        } else {
            url_stream << "http://";
        }
        url_stream << address << ":" << port;
        base_url_ = url_stream.str();
        
        // Test connection with a simple ping
        std::string ping_url = base_url_ + "/api/ping";
        
        CurlResponse response;
        curl_easy_setopt(curl_, CURLOPT_URL, ping_url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        
        CURLcode res = curl_easy_perform(curl_);
        
	        if (res == CURLE_OK) {
	            curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.response_code);
            // Accept 200 (OK) or 404 (endpoint not found, but server is responding)
            connected_ = (response.response_code == 200 || response.response_code == 404);
            if (!connected_) {
                std::cerr << "ping failed with HTTP " << response.response_code << std::endl;
            }
	        } else if (res == CURLE_ABORTED_BY_CALLBACK && isShutdownRequested()) {
	            connected_ = false;
	        } else {
	            std::cerr << "ping CURL error: " << curl_easy_strerror(res) << std::endl;
	            connected_ = false;
	        }
        
        return connected_;
    }
    
    bool authenticate(const std::string& token) {
        // Create token-based credentials
        Credentials creds;
        creds.method = AuthMethod::TOKEN;
        creds.token = token;
        creds.expires_at = std::chrono::system_clock::now() + std::chrono::hours(24); // Default 24h expiry
        
        return authenticateWithCredentials(creds);
    }
    
	    bool authenticateWithCredentials(const Credentials& credentials) {
	        if (!connected_ || !curl_) {
            last_auth_result_ = AuthResult::NETWORK_ERROR;
            return false;
        }
        
        // Store credentials for future use
        current_credentials_ = std::make_unique<Credentials>(credentials);
        
        // Validate credentials with auth manager if available
        if (auth_manager_) {
            last_auth_result_ = auth_manager_->authenticate(credentials);
            if (last_auth_result_ != AuthResult::SUCCESS) {
                authenticated_ = false;
                return false;
            }
        }
        
        // Test authentication with server
        bool server_auth_success = performServerAuthentication(credentials);
        
        if (server_auth_success) {
            authenticated_ = true;
            last_auth_result_ = AuthResult::SUCCESS;
        } else {
            authenticated_ = false;
            if (last_auth_result_ == AuthResult::SUCCESS) {
                last_auth_result_ = AuthResult::INVALID_CREDENTIALS;
            }
        }
        
        return authenticated_;
    }
    
private:
    auto buildAuthHeaders(const Credentials& credentials) -> struct curl_slist* {
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        if (credentials.method == AuthMethod::TOKEN && !credentials.token.empty()) {
            std::string auth_header = "Authorization: Bearer " + credentials.token;
            headers = curl_slist_append(headers, auth_header.c_str());
        } else if (credentials.method == AuthMethod::CERTIFICATE && !credentials.certificate.empty()) {
            std::string cert_header = "X-Client-Certificate: " + credentials.certificate;
            headers = curl_slist_append(headers, cert_header.c_str());
        }

        return headers;
    }

	    bool performServerAuthentication(const Credentials& credentials) {
        // Test authentication with a protected endpoint
        std::string auth_url = base_url_ + "/api/auth/status";
        
        // Set authorization header based on auth method
        struct curl_slist* headers = buildAuthHeaders(credentials);
        
        CurlResponse response;
        curl_easy_setopt(curl_, CURLOPT_URL, auth_url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        
        CURLcode res = curl_easy_perform(curl_);
        
        curl_slist_free_all(headers);
        
	        if (res == CURLE_OK) {
	            curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.response_code);
            if (response.response_code != 200) {
                std::cerr << "authentication failed with HTTP " << response.response_code;
                if (!response.data.empty()) {
                    std::cerr << ": " << response.data;
                }
                std::cerr << std::endl;
            }
            return (response.response_code == 200);
	        }
	        if (res == CURLE_ABORTED_BY_CALLBACK && isShutdownRequested()) {
	            return false;
	        }
	        
	        std::cerr << "authentication CURL error: " << curl_easy_strerror(res) << std::endl;
	        return false;
    }

public:
	    std::optional<RemoteAgentConfig> fetchAgentConfig(const std::string& hostname) {
	        if (!connected_ || !curl_ || !current_credentials_ || hostname.empty()) {
	            return std::nullopt;
	        }
	        if (isShutdownRequested()) {
	            return std::nullopt;
	        }

        char* escaped_hostname = curl_easy_escape(curl_, hostname.c_str(), static_cast<int>(hostname.size()));
        if (escaped_hostname == nullptr) {
            return std::nullopt;
        }

        std::string config_url = base_url_ + "/api/agent/config?hostname=" + escaped_hostname;
        curl_free(escaped_hostname);

        struct curl_slist* headers = buildAuthHeaders(*current_credentials_);
        CurlResponse response;
        curl_easy_setopt(curl_, CURLOPT_URL, config_url.c_str());
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

        CURLcode res = curl_easy_perform(curl_);
        curl_slist_free_all(headers);

	        if (res != CURLE_OK) {
	            if (res == CURLE_ABORTED_BY_CALLBACK && isShutdownRequested()) {
	                connected_ = false;
	                return std::nullopt;
	            }
	            std::cerr << "config fetch CURL error: " << curl_easy_strerror(res) << std::endl;
	            connected_ = false;
	            return std::nullopt;
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.response_code);
        if (response.response_code != 200) {
            std::cerr << "config fetch failed with HTTP " << response.response_code;
            if (!response.data.empty()) {
                std::cerr << ": " << response.data;
            }
            std::cerr << std::endl;
            return std::nullopt;
        }

        RemoteAgentConfig config {};
        config.hostname = hostname;
        config.display_name = hostname;

        std::smatch match;
        const std::regex interval_regex("\\\"collection_interval_ms\\\"\\s*:\\s*(\\d+)");
        const std::regex gpu_regex("\\\"enable_gpu\\\"\\s*:\\s*(true|false)");
        const std::regex display_name_regex("\\\"display_name\\\"\\s*:\\s*(null|\\\"([^\\\"]*)\\\")");

        if (std::regex_search(response.data, match, interval_regex)) {
            config.collection_interval_ms = static_cast<std::uint32_t>(std::stoul(match[1].str()));
        }
        if (std::regex_search(response.data, match, gpu_regex)) {
            config.enable_gpu = (match[1].str() == "true");
        }
        if (std::regex_search(response.data, match, display_name_regex) && match.size() > 2 && match[2].matched) {
            config.display_name = match[2].str();
        }

        return config;
    }

	    bool sendMetrics(const MetricsData& data) {
	        if (isShutdownRequested()) {
	            return false;
	        }
	        // Always try to send buffered data first
	        processPendingMetrics();
        
        if (!connected_ || !curl_) {
            // Buffer the data if not connected
            bufferMetrics(data);
            return false;
        }
        
        bool success = sendMetricsInternal(data);
        
        if (!success) {
            // Buffer the data and attempt reconnection
            bufferMetrics(data);
            attemptReconnectWithBackoff();
        } else {
            // Reset retry attempts on successful send
            retry_attempts_ = 0;
        }
        
        return success;
    }
    
private:
    bool sendMetricsInternal(const MetricsData& data) {
        try {
            // Serialize metrics to JSON
            std::string json_data = data.toJson();
            
            // Send to metrics endpoint
            std::string metrics_url = base_url_ + "/api/metrics";
            
            // Set headers
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            
            // Add authentication headers based on current credentials
            if (current_credentials_) {
                if (current_credentials_->method == AuthMethod::TOKEN && !current_credentials_->token.empty()) {
                    std::string auth_header = "Authorization: Bearer " + current_credentials_->token;
                    headers = curl_slist_append(headers, auth_header.c_str());
                } else if (current_credentials_->method == AuthMethod::CERTIFICATE && !current_credentials_->certificate.empty()) {
                    std::string cert_header = "X-Client-Certificate: " + current_credentials_->certificate;
                    headers = curl_slist_append(headers, cert_header.c_str());
                }
            }
            
            CurlResponse response;
            curl_easy_setopt(curl_, CURLOPT_URL, metrics_url.c_str());
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl_, CURLOPT_HTTPGET, 0L);
            curl_easy_setopt(curl_, CURLOPT_POST, 1L);
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, json_data.c_str());
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, json_data.length());
            
            CURLcode res = curl_easy_perform(curl_);
            
            curl_slist_free_all(headers);
            
	            if (res == CURLE_OK) {
	                curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.response_code);
                bool success = (response.response_code >= 200 && response.response_code < 300);
                
                if (!success) {
                    std::cerr << "metrics upload failed with HTTP " << response.response_code;
                    if (!response.data.empty()) {
                        std::cerr << ": " << response.data;
                    }
                    std::cerr << std::endl;
                    // Server error, mark as disconnected
                    connected_ = false;
                }
                
                return success;
	            } else if (res == CURLE_ABORTED_BY_CALLBACK && isShutdownRequested()) {
	                connected_ = false;
	                return false;
	            } else {
	                std::cerr << "metrics upload CURL error: " << curl_easy_strerror(res) << std::endl;
                // Network error, mark as disconnected
                connected_ = false;
                return false;
            }
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
	    void bufferMetrics(const MetricsData& data) {
	        std::lock_guard<std::mutex> lock(buffer_mutex_);
	        
	        metrics_buffer_.push(data);
        
        // Enforce buffer size limit (remove oldest if necessary)
        while (metrics_buffer_.size() > max_buffer_size_) {
            metrics_buffer_.pop();
        }
    }
    
	    void processPendingMetrics() {
	        if (!connected_ || !curl_ || isShutdownRequested()) {
	            return;
	        }
        
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        
        // Send all buffered metrics
	        while (!metrics_buffer_.empty() && connected_) {
	            if (isShutdownRequested()) {
	                break;
	            }
	            const MetricsData& buffered_data = metrics_buffer_.front();
            
            if (sendMetricsInternal(buffered_data)) {
                metrics_buffer_.pop();
            } else {
                // Failed to send, stop processing buffer
                break;
            }
        }
    }
    
	    void attemptReconnectWithBackoff() {
	        if (retry_attempts_ >= max_retry_attempts_ || isShutdownRequested()) {
	            return; // Max attempts reached
	        }
	        
	        // Calculate exponential backoff delay
	        auto delay = calculateBackoffDelay();
	        constexpr auto poll_interval = std::chrono::milliseconds(250);
	        auto deadline = std::chrono::steady_clock::now() + delay;
	        while (std::chrono::steady_clock::now() < deadline) {
	            if (isShutdownRequested()) {
	                return;
	            }
	            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
	            std::this_thread::sleep_for(std::min(poll_interval, remaining));
	        }
	        
	        retry_attempts_++;
	        
	        // Try to reconnect
	        if (!isShutdownRequested() && attemptReconnect()) {
            retry_attempts_ = 0; // Reset on successful reconnection
            
            // Notify callback if set
            if (reconnect_callback_) {
                reconnect_callback_();
            }
        }
    }
    
    std::chrono::milliseconds calculateBackoffDelay() {
        // Exponential backoff: base_delay * 2^retry_attempts with jitter
        double multiplier = std::pow(2.0, retry_attempts_);
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            base_retry_delay_ * multiplier
        );
        
        // Add jitter (±25%)
        double jitter_factor = 0.75 + (static_cast<double>(rand()) / RAND_MAX) * 0.5;
        delay = std::chrono::duration_cast<std::chrono::milliseconds>(delay * jitter_factor);
        
        // Cap at maximum delay
        auto max_delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(5));
        return std::min(delay, max_delay);
    }

public:
    
    bool isConnected() const {
        return connected_;
    }
    
    bool isAuthenticated() const {
        return authenticated_;
    }
    
    void disconnect() {
        connected_ = false;
        authenticated_ = false;
        current_credentials_.reset();
        retry_attempts_ = 0;
        last_auth_result_ = AuthResult::SUCCESS;
        
        // Clear buffer on explicit disconnect
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        while (!metrics_buffer_.empty()) {
            metrics_buffer_.pop();
        }
    }
    
    void setCredentials(const Credentials& credentials) {
        current_credentials_ = std::make_unique<Credentials>(credentials);
    }
    
    void setAuthManager(std::shared_ptr<AuthManager> auth_manager) {
        auth_manager_ = auth_manager;
    }
    
    bool refreshAuthentication() {
        if (!current_credentials_ || !auth_manager_) {
            return false;
        }
        
        // Check if credentials need refresh
        auto refreshed = auth_manager_->validateAndRefresh(*current_credentials_);
        if (refreshed) {
            current_credentials_ = std::move(refreshed);
            // Re-authenticate with server using refreshed credentials
            return authenticateWithCredentials(*current_credentials_);
        }
        
        return false;
    }
    
    AuthResult getLastAuthResult() const {
        return last_auth_result_;
    }
    
    void setReconnectCallback(std::function<void()> callback) {
        reconnect_callback_ = callback;
    }
    
    void enableTLS(bool enable) {
        tls_enabled_ = enable;
        
        if (curl_) {
            if (enable) {
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
            } else {
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
            }
        }
    }
    
    void setConnectionTimeout(std::chrono::milliseconds timeout) {
        connection_timeout_ = timeout;
        
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeout.count() / 1000);
        }
    }
    
    void setReconnectDelay(std::chrono::milliseconds delay) {
        reconnect_delay_ = delay;
        base_retry_delay_ = delay;
    }
    
    void setMaxBufferSize(size_t size) {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        max_buffer_size_ = size;
        
        // Enforce new limit
        while (metrics_buffer_.size() > max_buffer_size_) {
            metrics_buffer_.pop();
        }
    }
    
    void setMaxRetryAttempts(size_t attempts) {
        max_retry_attempts_ = attempts;
    }
    
    size_t getBufferedMetricsCount() const {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        return metrics_buffer_.size();
    }
    
    // Attempt to reconnect with exponential backoff
    bool attemptReconnect() {
        if (connected_) {
            return true;
        }
        
        // Try to reconnect
        if (connect(server_address_, server_port_)) {
            if (current_credentials_) {
                return authenticateWithCredentials(*current_credentials_);
            }
            return true;
        }
        
        return false;
    }

private:
    CURL* curl_;
    bool connected_;
    bool authenticated_;
    bool tls_enabled_;
    std::string server_address_;
    uint16_t server_port_ = 0;
    std::string base_url_;
    std::unique_ptr<Credentials> current_credentials_;
    std::shared_ptr<AuthManager> auth_manager_;
    AuthResult last_auth_result_;
    std::chrono::milliseconds connection_timeout_;
    std::chrono::milliseconds reconnect_delay_;
    std::function<void()> reconnect_callback_;
    
    // Buffering and retry logic
    mutable std::mutex buffer_mutex_;
    std::queue<MetricsData> metrics_buffer_;
    size_t max_buffer_size_;
    std::atomic<size_t> retry_attempts_;
    size_t max_retry_attempts_;
    std::chrono::milliseconds base_retry_delay_;
};

// NetworkClient implementation
NetworkClient::NetworkClient() : pImpl_(std::make_unique<Impl>()) {}

NetworkClient::~NetworkClient() = default;

bool NetworkClient::connect(const std::string& address, uint16_t port) {
    return pImpl_->connect(address, port);
}

bool NetworkClient::authenticate(const std::string& token) {
    return pImpl_->authenticate(token);
}

bool NetworkClient::authenticateWithCredentials(const Credentials& credentials) {
    return pImpl_->authenticateWithCredentials(credentials);
}

bool NetworkClient::sendMetrics(const MetricsData& data) {
    return pImpl_->sendMetrics(data);
}

std::optional<RemoteAgentConfig> NetworkClient::fetchAgentConfig(const std::string& hostname) {
    return pImpl_->fetchAgentConfig(hostname);
}

bool NetworkClient::isConnected() const {
    return pImpl_->isConnected();
}

bool NetworkClient::isAuthenticated() const {
    return pImpl_->isAuthenticated();
}

void NetworkClient::disconnect() {
    pImpl_->disconnect();
}

void NetworkClient::setCredentials(const Credentials& credentials) {
    pImpl_->setCredentials(credentials);
}

void NetworkClient::setAuthManager(std::shared_ptr<AuthManager> auth_manager) {
    pImpl_->setAuthManager(auth_manager);
}

bool NetworkClient::refreshAuthentication() {
    return pImpl_->refreshAuthentication();
}

AuthResult NetworkClient::getLastAuthResult() const {
    return pImpl_->getLastAuthResult();
}

void NetworkClient::setReconnectCallback(std::function<void()> callback) {
    pImpl_->setReconnectCallback(callback);
}

void NetworkClient::enableTLS(bool enable) {
    pImpl_->enableTLS(enable);
}

void NetworkClient::setConnectionTimeout(std::chrono::milliseconds timeout) {
    pImpl_->setConnectionTimeout(timeout);
}

void NetworkClient::setReconnectDelay(std::chrono::milliseconds delay) {
    pImpl_->setReconnectDelay(delay);
}

void NetworkClient::setMaxBufferSize(size_t size) {
    pImpl_->setMaxBufferSize(size);
}

void NetworkClient::setMaxRetryAttempts(size_t attempts) {
    pImpl_->setMaxRetryAttempts(attempts);
}

size_t NetworkClient::getBufferedMetricsCount() const {
    return pImpl_->getBufferedMetricsCount();
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
