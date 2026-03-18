#ifdef DISTRIBUTED_MONITORING

#include "metrics_collector.hpp"

#include "btop_config.hpp"
#include "btop_shared.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace btop::distributed::client {

namespace {

constexpr int kCollectorWidth = 40;
constexpr std::size_t kTopProcessCount = 8;

auto buildProcessSnapshot(const Proc::proc_info& proc) -> MetricsData::TopProcess {
	MetricsData::TopProcess process {};
	process.pid = proc.pid;
	process.name = !proc.name.empty() ? proc.name : (!proc.short_cmd.empty() ? proc.short_cmd : proc.cmd);
	process.user = proc.user;
	process.memory_bytes = proc.mem;
	process.cpu_percent = proc.cpu_p;
	process.threads = static_cast<std::uint32_t>(proc.threads);
	process.state = proc.state;
	return process;
}

template <typename Container>
auto lastOrZero(const Container& values) -> double {
	return values.empty() ? 0.0 : static_cast<double>(values.back());
}

auto scaleFrequencyMHz(double value, std::string_view unit) -> double {
	if (unit == "GHz" || unit == "ghz") {
		return value * 1000.0;
	}
	if (unit == "kHz" || unit == "khz") {
		return value / 1000.0;
	}
	if (unit == "Hz" || unit == "hz") {
		return value / 1000.0 / 1000.0;
	}
	return value;
}

#ifdef __linux__
auto readUnsignedFile(const fs::path& path) -> std::uint64_t {
	std::ifstream input(path);
	if (!input.is_open()) {
		return 0;
	}

	std::uint64_t value {};
	input >> value;
	return input.fail() ? 0 : value;
}
#endif

} // namespace

MetricsCollector::MetricsCollector(bool enable_gpu) : enable_gpu_(enable_gpu) {}

void MetricsCollector::setGpuEnabled(bool enable_gpu) {
	enable_gpu_ = enable_gpu;

#ifdef GPU_SUPPORT
	if (!initialized_) {
		return;
	}

	if (enable_gpu_) {
		Config::set("shown_gpus", "nvidia amd intel");
	} else {
		Config::set("shown_gpus", "");
	}
#endif
}

auto MetricsCollector::collect() -> MetricsData {
	initialize();

	auto& cpu = Cpu::collect(false);
	auto& mem = Mem::collect(false);
	auto& net = Net::collect(false);
	auto& procs = Proc::collect(false);

	MetricsData data {};
	data.hostname = hostname_;
	data.timestamp = std::chrono::system_clock::now();

	data.cpu.usage_percent = lastOrZero(cpu.cpu_percent.at("total"));
	data.cpu.core_usage.reserve(cpu.core_percent.size());
	for (const auto& core_usage : cpu.core_percent) {
		data.cpu.core_usage.push_back(lastOrZero(core_usage));
	}
	if (!cpu.temp.empty() && !cpu.temp.front().empty()) {
		data.cpu.temperature_celsius = static_cast<double>(cpu.temp.front().back());
	}
	data.cpu.frequency_mhz = parseFrequencyMHz(Cpu::cpuHz);

	data.memory.total_bytes = Mem::get_totalMem();
	data.memory.used_bytes = mem.stats.at("used");
	data.memory.available_bytes = mem.stats.at("available");
	data.memory.cached_bytes = mem.stats.at("cached");
	data.memory.swap_total_bytes = mem.stats.at("swap_total");
	data.memory.swap_used_bytes = mem.stats.at("swap_used");

	data.network.interface_name = Net::selected_iface;
	data.network.bytes_received = net.stat.at("download").total;
	data.network.bytes_sent = net.stat.at("upload").total;
	data.network.packets_received = readInterfaceCounter(Net::selected_iface, "rx_packets");
	data.network.packets_sent = readInterfaceCounter(Net::selected_iface, "tx_packets");

	data.processes.total_processes = static_cast<std::uint32_t>(procs.size());
	data.processes.load_average_1min = cpu.load_avg[0];
	data.processes.load_average_5min = cpu.load_avg[1];
	data.processes.load_average_15min = cpu.load_avg[2];
	for (const auto& proc : procs) {
		if (proc.state == 'R') {
			++data.processes.running_processes;
		} else if (proc.state == 'S') {
			++data.processes.sleeping_processes;
		}
	}

	auto ranked_processes = procs;
	std::stable_sort(ranked_processes.begin(), ranked_processes.end(), [](const auto& left, const auto& right) {
		if (left.cpu_p == right.cpu_p) {
			return left.mem > right.mem;
		}
		return left.cpu_p > right.cpu_p;
	});
	for (std::size_t index = 0; index < ranked_processes.size() && index < kTopProcessCount; ++index) {
		data.processes.top_processes.push_back(buildProcessSnapshot(ranked_processes[index]));
	}
	data.processes.all_processes.reserve(ranked_processes.size());
	for (const auto& proc : ranked_processes) {
		data.processes.all_processes.push_back(buildProcessSnapshot(proc));
	}

#ifdef GPU_SUPPORT
	if (enable_gpu_) {
		std::uint32_t gpu_index = 0;
		for (const auto& gpu : Gpu::collect(false)) {
			MetricsData::GpuMetrics gpu_metrics {};
			gpu_metrics.index = gpu_index++;
			const auto totals_it = gpu.gpu_percent.find("gpu-totals");
			if (totals_it != gpu.gpu_percent.end() && !totals_it->second.empty()) {
				gpu_metrics.utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(totals_it->second.back(), 0, 100));
			}
			gpu_metrics.memory_used_bytes = static_cast<std::uint64_t>(std::max<long long>(0, gpu.mem_used));
			gpu_metrics.memory_total_bytes = static_cast<std::uint64_t>(std::max<long long>(0, gpu.mem_total));
			if (gpu.mem_total > 0 && gpu.mem_used >= 0) {
				const auto memory_percent = static_cast<long long>((gpu.mem_used * 100) / std::max<long long>(1, gpu.mem_total));
				gpu_metrics.memory_utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(memory_percent, 0, 100));
			}
			if (!gpu.temp.empty()) {
				gpu_metrics.temperature_celsius = static_cast<std::uint32_t>(std::max<long long>(0, gpu.temp.back()));
			}
			gpu_metrics.power_usage_watts = static_cast<std::uint32_t>(std::max<long long>(0, gpu.pwr_usage / 1000));
			gpu_metrics.clock_speed_mhz = gpu.gpu_clock_speed;
			gpu_metrics.pcie_tx_kbps = static_cast<std::uint64_t>(std::max<long long>(0, gpu.pcie_tx));
			gpu_metrics.pcie_rx_kbps = static_cast<std::uint64_t>(std::max<long long>(0, gpu.pcie_rx));
			gpu_metrics.encoder_utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(gpu.encoder_utilization, 0, 100));
			gpu_metrics.decoder_utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(gpu.decoder_utilization, 0, 100));
			data.gpus.push_back(gpu_metrics);
		}
	}
#endif

	if (!data.validate()) {
		throw std::runtime_error("collected invalid metrics: " + data.getValidationErrors());
	}

	return data;
}

void MetricsCollector::initialize() {
	if (initialized_) {
		return;
	}

	Global::real_uid = getuid();
	Global::set_uid = geteuid();

	Cpu::width = kCollectorWidth;
	Mem::width = kCollectorWidth;
	Net::width = kCollectorWidth;
	Proc::width = kCollectorWidth;
#ifdef GPU_SUPPORT
	Gpu::width = kCollectorWidth;
#endif

	// Disable UI-only collectors to keep the agent lightweight.
	Config::set("show_disks", false);
	Config::set("swap_disk", false);
	Config::set("show_battery", false);
	Config::set("net_sync", false);
	if (!enable_gpu_) {
#ifdef GPU_SUPPORT
		Config::set("shown_gpus", "");
#endif
	}

	Shared::init();
	hostname_ = detectHostname();
	warmupCollectors();
	initialized_ = true;
}

void MetricsCollector::warmupCollectors() const {
	Cpu::collect(false);
	Mem::collect(false);
	Net::collect(false);
	Proc::collect(false);
#ifdef GPU_SUPPORT
	if (enable_gpu_) {
		Gpu::collect(false);
	}
#endif
	std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

auto MetricsCollector::parseFrequencyMHz(const std::string& frequency) -> double {
	if (frequency.empty()) {
		return 0.0;
	}

	for (std::size_t i = 0; i < frequency.size(); ++i) {
		if (!(std::isdigit(static_cast<unsigned char>(frequency[i])) || frequency[i] == '.')) {
			continue;
		}

		const auto number_start = i;
		auto number_end = i;
		while (number_end < frequency.size() &&
		       (std::isdigit(static_cast<unsigned char>(frequency[number_end])) || frequency[number_end] == '.')) {
			++number_end;
		}

		double value {};
		const auto number = frequency.substr(number_start, number_end - number_start);
		try {
			value = std::stod(number);
		} catch (const std::exception&) {
			continue;
		}

		auto unit_start = number_end;
		while (unit_start < frequency.size() && std::isspace(static_cast<unsigned char>(frequency[unit_start]))) {
			++unit_start;
		}
		auto unit_end = unit_start;
		while (unit_end < frequency.size() && std::isalpha(static_cast<unsigned char>(frequency[unit_end]))) {
			++unit_end;
		}

		return scaleFrequencyMHz(value, std::string_view(frequency).substr(unit_start, unit_end - unit_start));
	}

	return 0.0;
}

auto MetricsCollector::detectHostname() -> std::string {
	std::array<char, 256> hostname {};
	if (gethostname(hostname.data(), hostname.size() - 1) == 0 && hostname.front() != '\0') {
		return hostname.data();
	}
	return "unknown-host";
}

auto MetricsCollector::readInterfaceCounter(const std::string& interface_name, const std::string& metric_name) -> std::uint64_t {
	if (interface_name.empty()) {
		return 0;
	}

#ifdef __linux__
	return readUnsignedFile(fs::path("/sys/class/net") / interface_name / "statistics" / metric_name);
#else
	(void)metric_name;
	return 0;
#endif
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
