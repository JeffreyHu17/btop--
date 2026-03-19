#ifdef DISTRIBUTED_MONITORING

#include "metrics_data.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace btop::distributed {

// JSON serialization for MetricsData::CpuMetrics
void to_json(nlohmann::json& j, const MetricsData::CpuMetrics& cpu) {
    j = nlohmann::json{
        {"usage_percent", cpu.usage_percent},
        {"core_usage", cpu.core_usage},
        {"temperature_celsius", cpu.temperature_celsius},
        {"frequency_mhz", cpu.frequency_mhz}
    };
}

void from_json(const nlohmann::json& j, MetricsData::CpuMetrics& cpu) {
    j.at("usage_percent").get_to(cpu.usage_percent);
    j.at("core_usage").get_to(cpu.core_usage);
    j.at("temperature_celsius").get_to(cpu.temperature_celsius);
    j.at("frequency_mhz").get_to(cpu.frequency_mhz);
}

// JSON serialization for MetricsData::MemoryMetrics
void to_json(nlohmann::json& j, const MetricsData::MemoryMetrics& memory) {
    j = nlohmann::json{
        {"total_bytes", memory.total_bytes},
        {"used_bytes", memory.used_bytes},
        {"available_bytes", memory.available_bytes},
        {"cached_bytes", memory.cached_bytes},
        {"swap_total_bytes", memory.swap_total_bytes},
        {"swap_used_bytes", memory.swap_used_bytes}
    };
}

void from_json(const nlohmann::json& j, MetricsData::MemoryMetrics& memory) {
    j.at("total_bytes").get_to(memory.total_bytes);
    j.at("used_bytes").get_to(memory.used_bytes);
    j.at("available_bytes").get_to(memory.available_bytes);
    j.at("cached_bytes").get_to(memory.cached_bytes);
    j.at("swap_total_bytes").get_to(memory.swap_total_bytes);
    j.at("swap_used_bytes").get_to(memory.swap_used_bytes);
}

// JSON serialization for MetricsData::NetworkMetrics
void to_json(nlohmann::json& j, const MetricsData::NetworkMetrics& network) {
    j = nlohmann::json{
        {"bytes_sent", network.bytes_sent},
        {"bytes_received", network.bytes_received},
        {"packets_sent", network.packets_sent},
        {"packets_received", network.packets_received},
        {"interface_name", network.interface_name}
    };
}

void from_json(const nlohmann::json& j, MetricsData::NetworkMetrics& network) {
    j.at("bytes_sent").get_to(network.bytes_sent);
    j.at("bytes_received").get_to(network.bytes_received);
    j.at("packets_sent").get_to(network.packets_sent);
    j.at("packets_received").get_to(network.packets_received);
    j.at("interface_name").get_to(network.interface_name);
}

// JSON serialization for MetricsData::GpuMetrics
void to_json(nlohmann::json& j, const MetricsData::GpuMetrics& gpu) {
    j = nlohmann::json{
        {"index", gpu.index},
        {"utilization_percent", gpu.utilization_percent},
        {"memory_used_bytes", gpu.memory_used_bytes},
        {"memory_total_bytes", gpu.memory_total_bytes},
        {"memory_utilization_percent", gpu.memory_utilization_percent},
        {"temperature_celsius", gpu.temperature_celsius},
        {"power_usage_watts", gpu.power_usage_watts},
        {"clock_speed_mhz", gpu.clock_speed_mhz},
        {"pcie_tx_kbps", gpu.pcie_tx_kbps},
        {"pcie_rx_kbps", gpu.pcie_rx_kbps},
        {"encoder_utilization_percent", gpu.encoder_utilization_percent},
        {"decoder_utilization_percent", gpu.decoder_utilization_percent}
    };
}

void from_json(const nlohmann::json& j, MetricsData::GpuMetrics& gpu) {
    if (j.contains("index")) j.at("index").get_to(gpu.index);
    j.at("utilization_percent").get_to(gpu.utilization_percent);
    j.at("memory_used_bytes").get_to(gpu.memory_used_bytes);
    j.at("memory_total_bytes").get_to(gpu.memory_total_bytes);
    if (j.contains("memory_utilization_percent")) j.at("memory_utilization_percent").get_to(gpu.memory_utilization_percent);
    j.at("temperature_celsius").get_to(gpu.temperature_celsius);
    j.at("power_usage_watts").get_to(gpu.power_usage_watts);
    j.at("clock_speed_mhz").get_to(gpu.clock_speed_mhz);
    if (j.contains("pcie_tx_kbps")) j.at("pcie_tx_kbps").get_to(gpu.pcie_tx_kbps);
    if (j.contains("pcie_rx_kbps")) j.at("pcie_rx_kbps").get_to(gpu.pcie_rx_kbps);
    if (j.contains("encoder_utilization_percent")) j.at("encoder_utilization_percent").get_to(gpu.encoder_utilization_percent);
    if (j.contains("decoder_utilization_percent")) j.at("decoder_utilization_percent").get_to(gpu.decoder_utilization_percent);
}

// JSON serialization for MetricsData::TopProcess
void to_json(nlohmann::json& j, const MetricsData::TopProcess& process) {
    j = nlohmann::json{
        {"pid", process.pid},
        {"name", process.name},
        {"user", process.user},
        {"memory_bytes", process.memory_bytes},
        {"cpu_percent", process.cpu_percent},
        {"threads", process.threads},
        {"state", std::string(1, process.state)}
    };
}

void from_json(const nlohmann::json& j, MetricsData::TopProcess& process) {
    j.at("pid").get_to(process.pid);
    j.at("name").get_to(process.name);
    j.at("user").get_to(process.user);
    j.at("memory_bytes").get_to(process.memory_bytes);
    j.at("cpu_percent").get_to(process.cpu_percent);
    j.at("threads").get_to(process.threads);
    std::string state = "0";
    j.at("state").get_to(state);
    process.state = state.empty() ? '0' : state.front();
}

// JSON serialization for MetricsData::ProcessMetrics
void to_json(nlohmann::json& j, const MetricsData::ProcessMetrics& processes) {
    j = nlohmann::json{
        {"total_processes", processes.total_processes},
        {"running_processes", processes.running_processes},
        {"sleeping_processes", processes.sleeping_processes},
        {"load_average_1min", processes.load_average_1min},
        {"load_average_5min", processes.load_average_5min},
        {"load_average_15min", processes.load_average_15min},
        {"top_processes", processes.top_processes}
    };
    if (!processes.all_processes.empty()) {
        j["all_processes"] = processes.all_processes;
    }
}

void from_json(const nlohmann::json& j, MetricsData::ProcessMetrics& processes) {
    j.at("total_processes").get_to(processes.total_processes);
    j.at("running_processes").get_to(processes.running_processes);
    j.at("sleeping_processes").get_to(processes.sleeping_processes);
    j.at("load_average_1min").get_to(processes.load_average_1min);
    j.at("load_average_5min").get_to(processes.load_average_5min);
    j.at("load_average_15min").get_to(processes.load_average_15min);
    if (j.contains("top_processes")) {
        j.at("top_processes").get_to(processes.top_processes);
    }
    if (j.contains("all_processes")) {
        j.at("all_processes").get_to(processes.all_processes);
    }
}

// Helper function to convert time_point to ISO 8601 string
std::string timePointToString(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

// Helper function to convert ISO 8601 string to time_point
std::chrono::system_clock::time_point stringToTimePoint(const std::string& str) {
    std::tm tm = {};
    std::istringstream ss(str);
    
    // Parse the main timestamp part
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        throw std::invalid_argument("Invalid timestamp format: " + str);
    }
    
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    // Parse milliseconds if present
    if (ss.peek() == '.') {
        ss.ignore(); // skip the '.'
        int ms;
        ss >> ms;
        if (!ss.fail()) {
            tp += std::chrono::milliseconds(ms);
        }
    }
    
    return tp;
}

// JSON serialization for MetricsData
void to_json(nlohmann::json& j, const MetricsData& data) {
    j = nlohmann::json{
        {"hostname", data.hostname},
        {"timestamp", timePointToString(data.timestamp)},
        {"cpu", data.cpu},
        {"memory", data.memory},
        {"network", data.network},
        {"gpus", data.gpus},
        {"processes", data.processes}
    };
}

void from_json(const nlohmann::json& j, MetricsData& data) {
    j.at("hostname").get_to(data.hostname);
    data.timestamp = stringToTimePoint(j.at("timestamp").get<std::string>());
    j.at("cpu").get_to(data.cpu);
    j.at("memory").get_to(data.memory);
    j.at("network").get_to(data.network);
    j.at("gpus").get_to(data.gpus);
    j.at("processes").get_to(data.processes);
}

// Serialization functions
std::string MetricsData::toJson() const {
    nlohmann::json j = *this;
    return j.dump();
}

MetricsData MetricsData::fromJson(const std::string& json_str) {
    try {
        nlohmann::json j = nlohmann::json::parse(json_str);
        return j.get<MetricsData>();
    } catch (const nlohmann::json::exception& e) {
        throw std::invalid_argument("JSON parsing error: " + std::string(e.what()));
    }
}

// Validation functions
bool MetricsData::validate() const {
    // Validate hostname
    if (hostname.empty()) {
        return false;
    }
    
    // Validate CPU metrics
    if (cpu.usage_percent < 0.0 || cpu.usage_percent > 100.0) {
        return false;
    }
    
    for (double core_usage : cpu.core_usage) {
        if (core_usage < 0.0 || core_usage > 100.0) {
            return false;
        }
    }
    
    if (cpu.temperature_celsius < -273.15) { // Absolute zero check
        return false;
    }
    
    if (cpu.frequency_mhz < 0.0) {
        return false;
    }
    
    // Validate memory metrics
    if (memory.used_bytes > memory.total_bytes) {
        return false;
    }
    
    if (memory.swap_used_bytes > memory.swap_total_bytes) {
        return false;
    }
    
    // Validate GPU metrics
    for (const auto& gpu : gpus) {
        if (gpu.utilization_percent > 100) {
            return false;
        }

        if (gpu.memory_utilization_percent > 100) {
            return false;
        }
        
        if (gpu.memory_used_bytes > gpu.memory_total_bytes) {
            return false;
        }
        
        if (gpu.temperature_celsius < -273.15) { // Absolute zero check
            return false;
        }

        if (gpu.encoder_utilization_percent > 100 || gpu.decoder_utilization_percent > 100) {
            return false;
        }
    }

    // Validate process metrics
    if (processes.running_processes > processes.total_processes ||
        processes.sleeping_processes > processes.total_processes) {
        return false;
    }
    
    if (processes.load_average_1min < 0.0 ||
        processes.load_average_5min < 0.0 ||
        processes.load_average_15min < 0.0) {
        return false;
    }

    for (const auto& process : processes.top_processes) {
        if (process.cpu_percent < 0.0) {
            return false;
        }
    }
    for (const auto& process : processes.all_processes) {
        if (process.cpu_percent < 0.0) {
            return false;
        }
    }
    
    return true;
}

std::string MetricsData::getValidationErrors() const {
    std::vector<std::string> errors;
    
    // Check hostname
    if (hostname.empty()) {
        errors.push_back("Hostname cannot be empty");
    }
    
    // Check CPU metrics
    if (cpu.usage_percent < 0.0 || cpu.usage_percent > 100.0) {
        errors.push_back("CPU usage percent must be between 0 and 100");
    }
    
    for (size_t i = 0; i < cpu.core_usage.size(); ++i) {
        if (cpu.core_usage[i] < 0.0 || cpu.core_usage[i] > 100.0) {
            errors.push_back("CPU core " + std::to_string(i) + " usage must be between 0 and 100");
        }
    }
    
    if (cpu.temperature_celsius < -273.15) {
        errors.push_back("CPU temperature cannot be below absolute zero");
    }
    
    if (cpu.frequency_mhz < 0.0) {
        errors.push_back("CPU frequency cannot be negative");
    }
    
    // Check memory metrics
    if (memory.used_bytes > memory.total_bytes) {
        errors.push_back("Memory used cannot exceed total memory");
    }
    
    if (memory.swap_used_bytes > memory.swap_total_bytes) {
        errors.push_back("Swap used cannot exceed total swap");
    }
    
    // Check GPU metrics
    for (size_t i = 0; i < gpus.size(); ++i) {
        const auto& gpu = gpus[i];
        
        if (gpu.utilization_percent > 100) {
            errors.push_back("GPU " + std::to_string(i) + " utilization cannot exceed 100%");
        }

        if (gpu.memory_utilization_percent > 100) {
            errors.push_back("GPU " + std::to_string(i) + " memory utilization cannot exceed 100%");
        }
        
        if (gpu.memory_used_bytes > gpu.memory_total_bytes) {
            errors.push_back("GPU " + std::to_string(i) + " memory used cannot exceed total");
        }
        
        if (gpu.temperature_celsius < -273.15) {
            errors.push_back("GPU " + std::to_string(i) + " temperature cannot be below absolute zero");
        }

        if (gpu.encoder_utilization_percent > 100 || gpu.decoder_utilization_percent > 100) {
            errors.push_back("GPU " + std::to_string(i) + " encoder/decoder utilization cannot exceed 100%");
        }
    }

    // Check process metrics
    if (processes.running_processes > processes.total_processes) {
        errors.push_back("Running processes cannot exceed total processes");
    }
    
    if (processes.sleeping_processes > processes.total_processes) {
        errors.push_back("Sleeping processes cannot exceed total processes");
    }
    
    if (processes.load_average_1min < 0.0 ||
        processes.load_average_5min < 0.0 ||
        processes.load_average_15min < 0.0) {
        errors.push_back("Load averages cannot be negative");
    }

    for (const auto& process : processes.top_processes) {
        if (process.cpu_percent < 0.0) {
            errors.push_back("Top process CPU percent cannot be negative");
        }
    }
    for (const auto& process : processes.all_processes) {
        if (process.cpu_percent < 0.0) {
            errors.push_back("Process CPU percent cannot be negative");
        }
    }
    
    // Join all errors into a single string
    if (errors.empty()) {
        return "";
    }
    
    std::string result = "Validation errors: ";
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) result += "; ";
        result += errors[i];
    }
    
    return result;
}

} // namespace btop::distributed

#endif // DISTRIBUTED_MONITORING
