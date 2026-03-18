#ifdef DISTRIBUTED_MONITORING

#include "config_parser.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace btop::distributed {

bool ConfigParser::parseClientConfig(const std::string& config_path, ClientConfig& client_config) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    if (content.empty()) {
        return true;
    }

    if (isJsonFormat(content)) {
        return parseJsonClientConfig(content, client_config);
    }
    return parseKeyValueClientConfig(content, client_config);
}

bool ConfigParser::parseServerConfig(const std::string& config_path, ServerConfig& server_config) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return true;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    if (content.empty()) {
        return true;
    }

    if (isJsonFormat(content)) {
        return parseJsonServerConfig(content, server_config);
    }
    return parseKeyValueServerConfig(content, server_config);
}

bool ConfigParser::writeClientConfig(const std::string& config_path, const ClientConfig& client_config) {
    std::ofstream file(config_path);
    if (!file.is_open()) {
        return false;
    }

    file << generateJsonClientConfig(client_config);
    return file.good();
}

bool ConfigParser::writeServerConfig(const std::string& config_path, const ServerConfig& server_config) {
    std::ofstream file(config_path);
    if (!file.is_open()) {
        return false;
    }

    file << generateJsonServerConfig(server_config);
    return file.good();
}

bool ConfigParser::validateClientConfig(const ClientConfig& client_config) {
    if (client_config.mode != "local" && client_config.mode != "distributed") {
        return false;
    }

    if (client_config.run_mode != "interactive" && client_config.run_mode != "daemon") {
        return false;
    }

    if (client_config.mode == "distributed" && client_config.server_address.empty()) {
        return false;
    }

    if (client_config.mode == "distributed" && client_config.auth_token.empty()) {
        return false;
    }

    if (client_config.server_port == 0) {
        return false;
    }

    if (client_config.collection_interval_ms == 0) {
        return false;
    }

    if (client_config.reconnect_delay_ms == 0 || client_config.max_reconnect_attempts == 0) {
        return false;
    }

    if (client_config.run_mode == "daemon") {
        if (client_config.log_file.empty() || client_config.pid_file.empty()) {
            return false;
        }
    }

    return true;
}

bool ConfigParser::validateServerConfig(const ServerConfig& server_config) {
    if (server_config.listen_port == 0) {
        return false;
    }

    if (server_config.bind_address.empty()) {
        return false;
    }

    if (server_config.auth_method != "token" && server_config.auth_method != "certificate") {
        return false;
    }

    if (server_config.enable_tls && (server_config.cert_file.empty() || server_config.key_file.empty())) {
        return false;
    }

    if (server_config.data_retention_hours == 0 || server_config.max_clients == 0) {
        return false;
    }

    if (server_config.client_stale_after_seconds == 0) {
        return false;
    }

    if (server_config.database_path.empty()) {
        return false;
    }

    if (server_config.daemon_mode && (server_config.log_file.empty() || server_config.pid_file.empty())) {
        return false;
    }

    return true;
}

bool ConfigParser::parseJsonClientConfig(const std::string& json_content, ClientConfig& config) {
    std::regex mode_regex("\\\"mode\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex run_mode_regex("\\\"run_mode\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex server_address_regex("\\\"server_address\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex server_port_regex("\\\"server_port\\\"\\s*:\\s*(\\d+)");
    std::regex auth_token_regex("\\\"auth_token\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex collection_interval_regex("\\\"collection_interval_ms\\\"\\s*:\\s*(\\d+)");
    std::regex enable_gpu_regex("\\\"enable_gpu\\\"\\s*:\\s*(true|false)");
    std::regex reconnect_delay_regex("\\\"reconnect_delay_ms\\\"\\s*:\\s*(\\d+)");
    std::regex max_reconnect_attempts_regex("\\\"max_reconnect_attempts\\\"\\s*:\\s*(\\d+)");
    std::regex log_file_regex("\\\"log_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex pid_file_regex("\\\"pid_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");

    std::smatch match;

    if (std::regex_search(json_content, match, mode_regex)) config.mode = match[1].str();
    if (std::regex_search(json_content, match, run_mode_regex)) config.run_mode = match[1].str();
    if (std::regex_search(json_content, match, server_address_regex)) config.server_address = match[1].str();
    if (std::regex_search(json_content, match, server_port_regex)) config.server_port = static_cast<uint16_t>(std::stoul(match[1].str()));
    if (std::regex_search(json_content, match, auth_token_regex)) config.auth_token = match[1].str();
    if (std::regex_search(json_content, match, collection_interval_regex)) config.collection_interval_ms = std::stoul(match[1].str());
    if (std::regex_search(json_content, match, enable_gpu_regex)) config.enable_gpu = (match[1].str() == "true");
    if (std::regex_search(json_content, match, reconnect_delay_regex)) config.reconnect_delay_ms = std::stoul(match[1].str());
    if (std::regex_search(json_content, match, max_reconnect_attempts_regex)) config.max_reconnect_attempts = std::stoul(match[1].str());
    if (std::regex_search(json_content, match, log_file_regex)) config.log_file = match[1].str();
    if (std::regex_search(json_content, match, pid_file_regex)) config.pid_file = match[1].str();

    return true;
}

bool ConfigParser::parseJsonServerConfig(const std::string& json_content, ServerConfig& config) {
    std::regex listen_port_regex("\\\"listen_port\\\"\\s*:\\s*(\\d+)");
    std::regex bind_address_regex("\\\"bind_address\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex auth_method_regex("\\\"auth_method\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex auth_token_regex("\\\"auth_token\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex cert_file_regex("\\\"cert_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex key_file_regex("\\\"key_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex data_retention_regex("\\\"data_retention_hours\\\"\\s*:\\s*(\\d+)");
    std::regex max_clients_regex("\\\"max_clients\\\"\\s*:\\s*(\\d+)");
    std::regex enable_tls_regex("\\\"enable_tls\\\"\\s*:\\s*(true|false)");
    std::regex daemon_mode_regex("\\\"daemon_mode\\\"\\s*:\\s*(true|false)");
    std::regex log_file_regex("\\\"log_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex pid_file_regex("\\\"pid_file\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex database_path_regex("\\\"database_path\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex web_root_regex("\\\"web_root\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::regex stale_after_regex("\\\"client_stale_after_seconds\\\"\\s*:\\s*(\\d+)");

    std::smatch match;

    if (std::regex_search(json_content, match, listen_port_regex)) config.listen_port = static_cast<uint16_t>(std::stoul(match[1].str()));
    if (std::regex_search(json_content, match, bind_address_regex)) config.bind_address = match[1].str();
    if (std::regex_search(json_content, match, auth_method_regex)) config.auth_method = match[1].str();
    if (std::regex_search(json_content, match, auth_token_regex)) config.auth_token = match[1].str();
    if (std::regex_search(json_content, match, cert_file_regex)) config.cert_file = match[1].str();
    if (std::regex_search(json_content, match, key_file_regex)) config.key_file = match[1].str();
    if (std::regex_search(json_content, match, data_retention_regex)) config.data_retention_hours = std::stoul(match[1].str());
    if (std::regex_search(json_content, match, max_clients_regex)) config.max_clients = std::stoul(match[1].str());
    if (std::regex_search(json_content, match, enable_tls_regex)) config.enable_tls = (match[1].str() == "true");
    if (std::regex_search(json_content, match, daemon_mode_regex)) config.daemon_mode = (match[1].str() == "true");
    if (std::regex_search(json_content, match, log_file_regex)) config.log_file = match[1].str();
    if (std::regex_search(json_content, match, pid_file_regex)) config.pid_file = match[1].str();
    if (std::regex_search(json_content, match, database_path_regex)) config.database_path = match[1].str();
    if (std::regex_search(json_content, match, web_root_regex)) config.web_root = match[1].str();
    if (std::regex_search(json_content, match, stale_after_regex)) config.client_stale_after_seconds = std::stoul(match[1].str());

    return true;
}

bool ConfigParser::parseKeyValueClientConfig(const std::string& content, ClientConfig& config) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "mode") {
            config.mode = value;
        } else if (key == "run_mode") {
            config.run_mode = value;
        } else if (key == "server_address") {
            config.server_address = value;
        } else if (key == "server_port") {
            try {
                config.server_port = static_cast<uint16_t>(std::stoul(value));
            } catch (const std::exception&) {
            }
        } else if (key == "auth_token") {
            config.auth_token = value;
        } else if (key == "collection_interval_ms") {
            try {
                config.collection_interval_ms = std::stoul(value);
            } catch (const std::exception&) {
            }
        } else if (key == "enable_gpu") {
            config.enable_gpu = (value == "true" || value == "1" || value == "yes");
        } else if (key == "reconnect_delay_ms") {
            try {
                config.reconnect_delay_ms = std::stoul(value);
            } catch (const std::exception&) {
            }
        } else if (key == "max_reconnect_attempts") {
            try {
                config.max_reconnect_attempts = std::stoul(value);
            } catch (const std::exception&) {
            }
        } else if (key == "log_file") {
            config.log_file = value;
        } else if (key == "pid_file") {
            config.pid_file = value;
        }
    }

    return true;
}

bool ConfigParser::parseKeyValueServerConfig(const std::string& content, ServerConfig& config) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string value = trim(line.substr(eq_pos + 1));

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "listen_port") {
            try {
                config.listen_port = static_cast<uint16_t>(std::stoul(value));
            } catch (const std::exception&) {
            }
        } else if (key == "bind_address") {
            config.bind_address = value;
        } else if (key == "auth_method") {
            config.auth_method = value;
        } else if (key == "auth_token") {
            config.auth_token = value;
        } else if (key == "cert_file") {
            config.cert_file = value;
        } else if (key == "key_file") {
            config.key_file = value;
        } else if (key == "data_retention_hours") {
            try {
                config.data_retention_hours = std::stoul(value);
            } catch (const std::exception&) {
            }
        } else if (key == "max_clients") {
            try {
                config.max_clients = std::stoul(value);
            } catch (const std::exception&) {
            }
        } else if (key == "enable_tls") {
            config.enable_tls = (value == "true" || value == "1" || value == "yes");
        } else if (key == "daemon_mode") {
            config.daemon_mode = (value == "true" || value == "1" || value == "yes");
        } else if (key == "log_file") {
            config.log_file = value;
        } else if (key == "pid_file") {
            config.pid_file = value;
        } else if (key == "database_path") {
            config.database_path = value;
        } else if (key == "web_root") {
            config.web_root = value;
        } else if (key == "client_stale_after_seconds") {
            try {
                config.client_stale_after_seconds = std::stoul(value);
            } catch (const std::exception&) {
            }
        }
    }

    return true;
}

std::string ConfigParser::generateJsonClientConfig(const ClientConfig& config) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"mode\": \"" << config.mode << "\",\n";
    json << "  \"run_mode\": \"" << config.run_mode << "\",\n";
    json << "  \"server_address\": \"" << config.server_address << "\",\n";
    json << "  \"server_port\": " << config.server_port << ",\n";
    json << "  \"auth_token\": \"" << config.auth_token << "\",\n";
    json << "  \"collection_interval_ms\": " << config.collection_interval_ms << ",\n";
    json << "  \"enable_gpu\": " << (config.enable_gpu ? "true" : "false") << ",\n";
    json << "  \"reconnect_delay_ms\": " << config.reconnect_delay_ms << ",\n";
    json << "  \"max_reconnect_attempts\": " << config.max_reconnect_attempts << ",\n";
    json << "  \"log_file\": \"" << config.log_file << "\",\n";
    json << "  \"pid_file\": \"" << config.pid_file << "\"\n";
    json << "}\n";
    return json.str();
}

std::string ConfigParser::generateJsonServerConfig(const ServerConfig& config) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"listen_port\": " << config.listen_port << ",\n";
    json << "  \"bind_address\": \"" << config.bind_address << "\",\n";
    json << "  \"auth_method\": \"" << config.auth_method << "\",\n";
    json << "  \"auth_token\": \"" << config.auth_token << "\",\n";
    json << "  \"cert_file\": \"" << config.cert_file << "\",\n";
    json << "  \"key_file\": \"" << config.key_file << "\",\n";
    json << "  \"data_retention_hours\": " << config.data_retention_hours << ",\n";
    json << "  \"max_clients\": " << config.max_clients << ",\n";
    json << "  \"enable_tls\": " << (config.enable_tls ? "true" : "false") << ",\n";
    json << "  \"daemon_mode\": " << (config.daemon_mode ? "true" : "false") << ",\n";
    json << "  \"log_file\": \"" << config.log_file << "\",\n";
    json << "  \"pid_file\": \"" << config.pid_file << "\",\n";
    json << "  \"database_path\": \"" << config.database_path << "\",\n";
    json << "  \"web_root\": \"" << config.web_root << "\",\n";
    json << "  \"client_stale_after_seconds\": " << config.client_stale_after_seconds << "\n";
    json << "}\n";
    return json.str();
}

std::string ConfigParser::trim(const std::string& str) {
    const size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool ConfigParser::isJsonFormat(const std::string& content) {
    std::string trimmed = trim(content);
    return !trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}';
}

} // namespace btop::distributed

#endif // DISTRIBUTED_MONITORING
