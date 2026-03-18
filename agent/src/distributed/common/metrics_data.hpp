#pragma once

#ifdef DISTRIBUTED_MONITORING

#include <chrono>
#include <string>
#include <vector>
#include <cstdint>

namespace btop::distributed {

/**
 * @brief Structure containing all system metrics data for transmission
 * 
 * This structure defines the complete set of metrics that can be collected
 * and transmitted between btop_client and monitoring_server.
 */
struct MetricsData {
    std::string hostname;
    std::chrono::system_clock::time_point timestamp;
    
    // CPU Metrics
    struct CpuMetrics {
        double usage_percent;
        std::vector<double> core_usage;
        double temperature_celsius;
        double frequency_mhz;
    } cpu;
    
    // Memory Metrics
    struct MemoryMetrics {
        uint64_t total_bytes;
        uint64_t used_bytes;
        uint64_t available_bytes;
        uint64_t cached_bytes;
        uint64_t swap_total_bytes;
        uint64_t swap_used_bytes;
    } memory;
    
    // Network Metrics
    struct NetworkMetrics {
        uint64_t bytes_sent;
        uint64_t bytes_received;
        uint64_t packets_sent;
        uint64_t packets_received;
        std::string interface_name;
    } network;

    // GPU Metrics (optional)
    struct GpuMetrics {
        uint32_t index;
        uint32_t utilization_percent;
        uint64_t memory_used_bytes;
        uint64_t memory_total_bytes;
        uint32_t memory_utilization_percent;
        uint32_t temperature_celsius;
        uint32_t power_usage_watts;
        uint32_t clock_speed_mhz;
        uint64_t pcie_tx_kbps;
        uint64_t pcie_rx_kbps;
        uint32_t encoder_utilization_percent;
        uint32_t decoder_utilization_percent;
    };
    std::vector<GpuMetrics> gpus;
    
    // Process Metrics
    struct TopProcess {
        uint64_t pid;
        std::string name;
        std::string user;
        uint64_t memory_bytes;
        double cpu_percent;
        uint32_t threads;
        char state;
    };

    struct ProcessMetrics {
        uint32_t total_processes;
        uint32_t running_processes;
        uint32_t sleeping_processes;
        double load_average_1min;
        double load_average_5min;
        double load_average_15min;
        std::vector<TopProcess> top_processes;
        std::vector<TopProcess> all_processes;
    } processes;
    
    // Serialization methods
    std::string toJson() const;
    static MetricsData fromJson(const std::string& json_str);
    
    // Validation methods
    bool validate() const;
    std::string getValidationErrors() const;
};

} // namespace btop::distributed

#endif // DISTRIBUTED_MONITORING
