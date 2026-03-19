#ifdef DISTRIBUTED_MONITORING

#include "daemon_manager.hpp"
#include "distributed_config.hpp"
#include "metrics_collector.hpp"
#include "network_client.hpp"

#if !defined(_WIN32) && defined(BTOP_AGENT_TUI_BACKEND_BTOP)
#include "btop_config.hpp"
#endif

#include <chrono>
#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#if defined(BTOP_AGENT_TUI_BACKEND_BTOP)
#include "btop.hpp"
#elif defined(BTOP_AGENT_TUI_BACKEND_BTOP4WIN)
extern int btop4win_main(int argc, char** argv);
#endif

namespace btop::distributed::client {

struct AgentOptions {
	fs::path config_path;
	bool once {};
	bool daemon_override {};
};

enum class LaunchMode {
	Agent,
	Tui,
};

enum class ParseResult {
	Ok,
	Help,
	Error,
};

namespace {

constexpr auto kRemoteConfigSyncInterval = std::chrono::seconds(5);
constexpr auto kShutdownPollInterval = std::chrono::milliseconds(250);

void printUsage() {
	std::cout
		<< "Usage: btop-agent [tui [btop options...]] [--config PATH] [--once]";
#ifdef _WIN32
	std::cout << '\n';
#else
	std::cout << " [--daemon]\n";
#endif
	std::cout
		<< "  --config PATH  Read client config from PATH\n"
		<< "  --once         Collect and send a single sample, then exit\n"
#ifdef _WIN32
		<< "  --daemon       Accepted for compatibility; background execution is managed by install.ps1\n"
#else
		<< "  --daemon       Run in daemon mode regardless of config\n"
#endif
		<< "  tui            Launch the embedded btop TUI\n"
		<< "  --help         Show this help text\n";
}

auto lowerCopy(std::string value) -> std::string {
	for (auto& character : value) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	return value;
}

auto normalizedProgramName(std::string_view argv0) -> std::string {
	auto name = lowerCopy(fs::path(argv0).filename().string());
	if (name.ends_with(".exe")) {
		name.resize(name.size() - 4);
	}
	return name;
}

auto defaultConfigPath() -> fs::path {
	auto preferred_path = fs::path {};
#ifndef _WIN32
	if (const auto xdg_config = std::getenv("XDG_CONFIG_HOME"); xdg_config != nullptr && *xdg_config != '\0') {
		preferred_path = fs::path(xdg_config) / "btop-agent" / "distributed-client.json";
	}
#endif
#ifdef _WIN32
	if (preferred_path.empty()) {
		auto preferred_appdata = fs::path {};
		if (const auto appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata != '\0') {
			preferred_appdata = fs::path(appdata) / "btop-agent" / "distributed-client.json";
			if (fs::exists(preferred_appdata)) {
				return preferred_appdata;
			}
			preferred_path = preferred_appdata;
		}
		if (const auto programdata = std::getenv("ProgramData"); programdata != nullptr && *programdata != '\0') {
			const auto machine_path = fs::path(programdata) / "btop-agent" / "distributed-client.json";
			if (fs::exists(machine_path)) {
				return machine_path;
			}
			if (preferred_path.empty()) {
				preferred_path = machine_path;
			}
		}
	}
#else
	if (preferred_path.empty()) {
		if (const auto home = std::getenv("HOME"); home != nullptr && *home != '\0') {
			preferred_path = fs::path(home) / ".config" / "btop-agent" / "distributed-client.json";
		}
	}
	if (!preferred_path.empty() && fs::exists(preferred_path)) {
		return preferred_path;
	}
	if (const auto legacy_config_dir = Config::get_config_dir(); legacy_config_dir.has_value()) {
		const auto legacy_path = *legacy_config_dir / "distributed-client.json";
		if (fs::exists(legacy_path)) {
			return legacy_path;
		}
	}
#endif
	if (!preferred_path.empty()) {
		const auto local_path = fs::current_path() / "distributed-client.json";
		if (fs::exists(local_path)) {
			return local_path;
		}
		return preferred_path;
	}
	return fs::current_path() / "distributed-client.json";
}

auto shouldLaunchTui(const std::string& program_name, const std::vector<std::string>& args) -> bool {
	if (program_name == "btop") {
		return true;
	}
	if (args.empty()) {
		return false;
	}
	return args.front() == "tui" || args.front() == "--tui";
}

auto parseArguments(const std::vector<std::string>& args, AgentOptions& options) -> ParseResult {
	options.config_path = defaultConfigPath();

	for (std::size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--help" || args[i] == "-h") {
			printUsage();
			return ParseResult::Help;
		}
		if (args[i] == "--once") {
			options.once = true;
			continue;
		}
		if (args[i] == "--daemon") {
			options.daemon_override = true;
			continue;
		}
		if (args[i] == "--config") {
			if (i + 1 >= args.size()) {
				std::cerr << "missing value for --config\n";
				return ParseResult::Error;
			}
			options.config_path = args[++i];
			continue;
		}

		std::cerr << "unknown argument: " << args[i] << '\n';
		return ParseResult::Error;
	}

	return ParseResult::Ok;
}

auto ensureConnected(NetworkClient& client, const DistributedConfig& config) -> bool {
	if (!client.isConnected() && !client.connect(config.getServerAddress(), config.getServerPort())) {
		return false;
	}

	const auto token = config.getAuthToken();
	if (!token.empty() && !client.isAuthenticated()) {
		return client.authenticate(token);
	}

	return client.isConnected();
}

auto waitForNextCollection(const DistributedConfig& config, const DaemonManager& daemon_manager) -> bool {
	const auto deadline = std::chrono::steady_clock::now() + config.getCollectionInterval();
	while (std::chrono::steady_clock::now() < deadline) {
		if (daemon_manager.shutdownRequested()) {
			return false;
		}
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
		std::this_thread::sleep_for(std::min(kShutdownPollInterval, remaining));
	}
	return !daemon_manager.shutdownRequested();
}

void maybeDaemonize(const DistributedConfig& config, const AgentOptions& options, DaemonManager& daemon_manager) {
	if (options.once) {
		return;
	}

	if (!options.daemon_override && !config.isDaemonMode()) {
		return;
	}

#ifdef _WIN32
	static bool warned_about_windows_daemon = false;
	if (!warned_about_windows_daemon) {
		std::cerr << "warning: daemon mode is managed externally on Windows; continuing without in-process daemonization\n";
		warned_about_windows_daemon = true;
	}
	(void)daemon_manager;
	return;
#else
	daemon_manager.setupSignalHandlers();
	if (!daemon_manager.daemonize()) {
		throw std::runtime_error("failed to daemonize process");
	}

	if (!config.getLogFile().empty()) {
		daemon_manager.redirectOutput(config.getLogFile());
	}
	if (!config.getPidFile().empty()) {
		daemon_manager.createPidFile(config.getPidFile());
	}
#endif
}

auto runAgent(const AgentOptions& options) -> int {
	DistributedConfig config;
	if (!config.loadFromFile(options.config_path.string())) {
		std::cerr << "failed to load config: " << options.config_path << '\n';
		return 1;
	}
	if (!config.validate()) {
		std::cerr << "invalid agent config: " << options.config_path << '\n';
		return 1;
	}

	if (config.getMode() == DistributedConfig::OperatingMode::LOCAL) {
		std::cerr << "warning: client mode is 'local'; btop-agent will still attempt remote reporting\n";
	}

	DaemonManager daemon_manager;
	maybeDaemonize(config, options, daemon_manager);

	NetworkClient client;
	client.setReconnectDelay(std::chrono::milliseconds(config.getReconnectDelay()));
	client.setMaxRetryAttempts(config.getMaxReconnectAttempts());

#ifdef _WIN32
	if (config.isGpuEnabled()) {
		std::cerr << "warning: GPU collection is not available in the current Windows fused build; disabling it\n";
		config.setGpuEnabled(false);
	}
#endif

	MetricsCollector collector(config.isGpuEnabled());
	auto last_remote_config_sync = std::chrono::steady_clock::time_point {};
#ifdef _WIN32
	auto warned_about_remote_gpu_request = false;
#endif

	bool sent_any_sample = false;
	while (true) {
		if (daemon_manager.shutdownRequested()) {
			return 0;
		}
		const auto ready = ensureConnected(client, config);
		const auto metrics = collector.collect();
		const auto now = std::chrono::steady_clock::now();
		const auto sent = client.sendMetrics(metrics);
		sent_any_sample = sent_any_sample || sent;
		if (daemon_manager.shutdownRequested()) {
			return 0;
		}

		if (ready && client.isAuthenticated()) {
			if (last_remote_config_sync == std::chrono::steady_clock::time_point {} ||
			    now - last_remote_config_sync >= kRemoteConfigSyncInterval) {
				if (const auto remote_config = client.fetchAgentConfig(metrics.hostname); remote_config.has_value()) {
					config.setCollectionInterval(std::chrono::milliseconds(remote_config->collection_interval_ms));
					auto remote_gpu_enabled = remote_config->enable_gpu;
#ifdef _WIN32
						if (remote_gpu_enabled) {
							if (!warned_about_remote_gpu_request) {
								std::cerr << "warning: ignoring remote GPU enable request on Windows fused builds\n";
								warned_about_remote_gpu_request = true;
							}
							remote_gpu_enabled = false;
						}
#endif
					config.setGpuEnabled(remote_gpu_enabled);
					collector.setGpuEnabled(remote_gpu_enabled);
				}
				last_remote_config_sync = now;
			}
		}

		if (!sent) {
			std::cerr << "failed to send metrics to "
			          << config.getServerAddress() << ':' << config.getServerPort() << '\n';
			if (options.once) {
				return 1;
			}
		}

		if (options.once) {
			return 0;
		}

		if (!waitForNextCollection(config, daemon_manager)) {
			return 0;
		}
	}

	return sent_any_sample ? 0 : 1;
}

auto runEmbeddedTui(const std::string& program_name, int argc, char* argv[]) -> int {
#if defined(BTOP_AGENT_TUI_BACKEND_BTOP)
	std::vector<std::string_view> tui_args;
	tui_args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);

	const auto invoked_as_btop = (program_name == "btop");
	const auto start_index = (!invoked_as_btop && argc > 1 && (std::string_view(argv[1]) == "tui" || std::string_view(argv[1]) == "--tui"))
		? 2
		: 1;

	for (int i = start_index; i < argc; ++i) {
		tui_args.emplace_back(argv[i]);
	}
	return btop_main(tui_args);
#elif defined(BTOP_AGENT_TUI_BACKEND_BTOP4WIN)
	std::vector<char*> tui_argv;
	tui_argv.reserve(argc > 0 ? static_cast<std::size_t>(argc) : 1);
	tui_argv.push_back(argv[0]);

	const auto invoked_as_btop = (program_name == "btop");
	const auto start_index = (!invoked_as_btop && argc > 1 && (std::string_view(argv[1]) == "tui" || std::string_view(argv[1]) == "--tui"))
		? 2
		: 1;
	for (int i = start_index; i < argc; ++i) {
		tui_argv.push_back(argv[i]);
	}
	return btop4win_main(static_cast<int>(tui_argv.size()), tui_argv.data());
#else
	(void)program_name;
	(void)argc;
	(void)argv;
	std::cerr << "Error: this btop-agent build does not include the embedded TUI\n";
	return 1;
#endif
}

} // namespace

} // namespace btop::distributed::client

int main(int argc, char* argv[]) {
	const auto program_name = btop::distributed::client::normalizedProgramName(argc > 0 ? argv[0] : "btop-agent");

	std::vector<std::string> args;
	args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i]);
	}

	if (btop::distributed::client::shouldLaunchTui(program_name, args)) {
		return btop::distributed::client::runEmbeddedTui(program_name, argc, argv);
	}

	btop::distributed::client::AgentOptions options;
	switch (btop::distributed::client::parseArguments(args, options)) {
	case btop::distributed::client::ParseResult::Ok:
		break;
	case btop::distributed::client::ParseResult::Help:
		return 0;
	case btop::distributed::client::ParseResult::Error:
		return 1;
	}

	try {
		return btop::distributed::client::runAgent(options);
	} catch (const std::exception& error) {
		std::cerr << "btop-agent failed: " << error.what() << '\n';
		return 1;
	}
}

#else

#include <iostream>

int main() {
	std::cerr << "Error: btop-agent requires DISTRIBUTED_MONITORING support\n";
	return 1;
}

#endif // DISTRIBUTED_MONITORING
