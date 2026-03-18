#pragma once

#ifdef DISTRIBUTED_MONITORING

#include "../common/metrics_data.hpp"

#include <string>

namespace btop::distributed::client {

class MetricsCollector {
public:
	explicit MetricsCollector(bool enable_gpu = true);
	void setGpuEnabled(bool enable_gpu);

	auto collect() -> MetricsData;

private:
	bool enable_gpu_;
	bool initialized_ {};
	std::string hostname_;

	void initialize();
	void warmupCollectors() const;

	static auto parseFrequencyMHz(const std::string& frequency) -> double;
	static auto detectHostname() -> std::string;
	static auto readInterfaceCounter(const std::string& interface_name, const std::string& metric_name) -> std::uint64_t;
};

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
