#pragma once

#ifdef DISTRIBUTED_MONITORING

#include "config.hpp"
#include <string>
#include <optional>

namespace btop::distributed {

/**
 * @brief Configuration file parser supporting JSON and simple key-value formats
 * 
 * Handles parsing of configuration files in both JSON format and simple
 * key=value format for backward compatibility.
 */
class ConfigParser {
public:
    /**
     * @brief Parse configuration from file
     * @param config_path Path to configuration file
     * @param client_config Output client configuration
     * @param server_config Output server configuration (optional)
     * @return true if parsing succeeded, false otherwise
     */
    static bool parseClientConfig(const std::string& config_path, ClientConfig& client_config);
    static bool parseServerConfig(const std::string& config_path, ServerConfig& server_config);
    
    /**
     * @brief Write configuration to file
     * @param config_path Path to configuration file
     * @param client_config Client configuration to write
     * @param server_config Server configuration to write (optional)
     * @return true if writing succeeded, false otherwise
     */
    static bool writeClientConfig(const std::string& config_path, const ClientConfig& client_config);
    static bool writeServerConfig(const std::string& config_path, const ServerConfig& server_config);
    
    /**
     * @brief Validate configuration parameters
     * @param client_config Client configuration to validate
     * @param server_config Server configuration to validate (optional)
     * @return true if configuration is valid, false otherwise
     */
    static bool validateClientConfig(const ClientConfig& client_config);
    static bool validateServerConfig(const ServerConfig& server_config);
    
private:
    // JSON parsing helpers
    static bool parseJsonClientConfig(const std::string& json_content, ClientConfig& config);
    static bool parseJsonServerConfig(const std::string& json_content, ServerConfig& config);
    
    // Key-value parsing helpers (for backward compatibility)
    static bool parseKeyValueClientConfig(const std::string& content, ClientConfig& config);
    static bool parseKeyValueServerConfig(const std::string& content, ServerConfig& config);
    
    // JSON generation helpers
    static std::string generateJsonClientConfig(const ClientConfig& config);
    static std::string generateJsonServerConfig(const ServerConfig& config);
    
    // Utility functions
    static std::string trim(const std::string& str);
    static bool isJsonFormat(const std::string& content);
};

} // namespace btop::distributed

#endif // DISTRIBUTED_MONITORING