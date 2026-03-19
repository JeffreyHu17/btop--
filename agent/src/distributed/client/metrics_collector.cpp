#ifdef DISTRIBUTED_MONITORING

#include "metrics_collector.hpp"

#include "btop_config.hpp"
#include "btop_shared.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <cwchar>
#ifdef _WIN32
#include <windows.h>
#include <iphlpapi.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#else
#include <unistd.h>
#endif

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

#ifdef _WIN32
auto wideToUtf8(const wchar_t* value) -> std::string {
	if (value == nullptr || *value == L'\0') {
		return {};
	}
	const auto length = static_cast<int>(std::wcslen(value));
	const auto size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}
	std::string result(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), size, nullptr, nullptr);
	return result;
}
#endif

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
#ifdef _WIN32
	enable_gpu_ = false;
#else
	enable_gpu_ = enable_gpu;
#endif

	if (!initialized_) {
		return;
	}

#ifdef _WIN32
	(void)enable_gpu;
	Config::set("show_gpu", false);
#elif defined(GPU_SUPPORT)
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
#ifdef _WIN32
	data.memory.swap_total_bytes = mem.stats.at("page_total");
	data.memory.swap_used_bytes = mem.stats.at("page_used");
#else
	data.memory.swap_total_bytes = mem.stats.at("swap_total");
	data.memory.swap_used_bytes = mem.stats.at("swap_used");
#endif

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

#ifdef _WIN32
	if (enable_gpu_ && Cpu::has_gpu) {
		MetricsData::GpuMetrics gpu_metrics {};
		gpu_metrics.index = 0;
		if (const auto usage_it = cpu.cpu_percent.find("gpu"); usage_it != cpu.cpu_percent.end()) {
			gpu_metrics.utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(
				static_cast<long long>(lastOrZero(usage_it->second)), 0, 100
			));
		}
		if (const auto total_it = mem.stats.find("gpu_total"); total_it != mem.stats.end()) {
			gpu_metrics.memory_total_bytes = total_it->second;
		}
		if (const auto used_it = mem.stats.find("gpu_used"); used_it != mem.stats.end()) {
			gpu_metrics.memory_used_bytes = used_it->second;
		}
		if (gpu_metrics.memory_total_bytes > 0) {
			const auto memory_percent = static_cast<long long>(
				(gpu_metrics.memory_used_bytes * 100) / std::max<std::uint64_t>(1, gpu_metrics.memory_total_bytes)
			);
			gpu_metrics.memory_utilization_percent = static_cast<std::uint32_t>(std::clamp<long long>(memory_percent, 0, 100));
		}
		if (!cpu.gpu_temp.empty()) {
			gpu_metrics.temperature_celsius = static_cast<std::uint32_t>(std::max<long long>(0, cpu.gpu_temp.back()));
		}
		gpu_metrics.clock_speed_mhz = parseFrequencyMHz(Cpu::gpu_clock);
		data.gpus.push_back(gpu_metrics);
	}
#elif defined(GPU_SUPPORT)
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

#ifndef _WIN32
	Global::real_uid = getuid();
	Global::set_uid = geteuid();
#endif

	Cpu::width = kCollectorWidth;
	Mem::width = kCollectorWidth;
	Net::width = kCollectorWidth;
	Proc::width = kCollectorWidth;
#ifdef GPU_SUPPORT
	Gpu::width = kCollectorWidth;
#endif

#ifdef _WIN32
	wchar_t self_path[MAX_PATH] = {0};
	if (GetModuleFileNameW(nullptr, self_path, MAX_PATH) != 0) {
		Config::conf_dir = fs::path(self_path).remove_filename();
		Config::conf_file = Config::conf_dir / "btop.conf";
		std::vector<std::string> load_warnings;
		Config::load(Config::conf_file, load_warnings);
	}
#endif

	// Disable UI-only collectors to keep the agent lightweight.
	Config::set("show_disks", false);
	Config::set("show_battery", false);
	Config::set("net_sync", false);
#ifdef _WIN32
	enable_gpu_ = false;
	Config::set("show_gpu", false);
#else
	Config::set("swap_disk", false);
	if (!enable_gpu_) {
#ifdef GPU_SUPPORT
		Config::set("shown_gpus", "");
#endif
	}
#endif

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
#ifdef _WIN32
	std::array<char, 256> hostname {};
	DWORD hostname_size = static_cast<DWORD>(hostname.size());
	if (GetComputerNameA(hostname.data(), &hostname_size) != 0 && hostname_size > 0) {
		return hostname.data();
	}
#else
	std::array<char, 256> hostname {};
	if (gethostname(hostname.data(), hostname.size() - 1) == 0 && hostname.front() != '\0') {
		return hostname.data();
	}
#endif
	return "unknown-host";
}

auto MetricsCollector::readInterfaceCounter(const std::string& interface_name, const std::string& metric_name) -> std::uint64_t {
	if (interface_name.empty()) {
		return 0;
	}

#ifdef _WIN32
	ULONG buffer_size = 0;
	if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &buffer_size) != ERROR_BUFFER_OVERFLOW) {
		return 0;
	}

	auto adapters = std::unique_ptr<IP_ADAPTER_ADDRESSES, decltype(&std::free)> {
		reinterpret_cast<IP_ADAPTER_ADDRESSES*>(std::malloc(buffer_size)),
		std::free
	};
	if (adapters == nullptr) {
		return 0;
	}
	if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters.get(), &buffer_size) != NO_ERROR) {
		return 0;
	}

	for (auto adapter = adapters.get(); adapter != nullptr; adapter = adapter->Next) {
		if (wideToUtf8(adapter->FriendlyName) != interface_name) {
			continue;
		}

		MIB_IF_ROW2 entry {};
		entry.InterfaceIndex = adapter->IfIndex;
		if (GetIfEntry2(&entry) != NO_ERROR) {
			return 0;
		}

		if (metric_name == "rx_packets") {
			return static_cast<std::uint64_t>(entry.InUcastPkts + entry.InNUcastPkts);
		}
		if (metric_name == "tx_packets") {
			return static_cast<std::uint64_t>(entry.OutUcastPkts + entry.OutNUcastPkts);
		}
		return 0;
	}
	return 0;
	#elif defined(__linux__)
		return readUnsignedFile(fs::path("/sys/class/net") / interface_name / "statistics" / metric_name);
	#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
		struct ifaddrs* interfaces = nullptr;
		if (getifaddrs(&interfaces) != 0) {
			return 0;
		}

		auto result = std::uint64_t {0};
		for (auto* entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
			if (entry->ifa_name == nullptr || entry->ifa_data == nullptr || interface_name != entry->ifa_name) {
				continue;
			}
			if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_LINK) {
				continue;
			}

			const auto* stats = static_cast<if_data*>(entry->ifa_data);
			if (metric_name == "rx_packets") {
				result = static_cast<std::uint64_t>(stats->ifi_ipackets);
				break;
			}
			if (metric_name == "tx_packets") {
				result = static_cast<std::uint64_t>(stats->ifi_opackets);
				break;
			}
		}
		freeifaddrs(interfaces);
		return result;
	#else
		(void)metric_name;
		return 0;
#endif
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
