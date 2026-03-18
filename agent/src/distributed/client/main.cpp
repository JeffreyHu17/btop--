#ifdef DISTRIBUTED_MONITORING

#include "daemon_manager.hpp"
#include "distributed_config.hpp"
#include "metrics_collector.hpp"
#include "network_client.hpp"

#include "btop_config.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace btop::distributed::client {

struct AgentOptions {
	fs::path config_path;
	bool once {};
	bool daemon_override {};
};

enum class ParseResult {
	Ok,
	Help,
	Error,
};

namespace {

constexpr auto kRemoteConfigSyncInterval = std::chrono::seconds(5);

void printUsage() {
	std::cout
		<< "Usage: btop-agent [--config PATH] [--once] [--daemon]\n"
		<< "  --config PATH  Read client config from PATH\n"
		<< "  --once         Collect and send a single sample, then exit\n"
		<< "  --daemon       Run in daemon mode regardless of config\n"
		<< "  --help         Show this help text\n";
}

auto defaultConfigPath() -> fs::path {
	if (const auto config_dir = Config::get_config_dir(); config_dir.has_value()) {
		return *config_dir / "distributed-client.json";
	}
	return fs::current_path() / "distributed-client.json";
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

void maybeDaemonize(const DistributedConfig& config, const AgentOptions& options, DaemonManager& daemon_manager) {
	if (options.once) {
		return;
	}

	if (!options.daemon_override && !config.isDaemonMode()) {
		return;
	}

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

	MetricsCollector collector(config.isGpuEnabled());
	auto last_remote_config_sync = std::chrono::steady_clock::time_point {};

		bool sent_any_sample = false;
		while (true) {
			const auto ready = ensureConnected(client, config);
			const auto metrics = collector.collect();
			const auto sent = client.sendMetrics(metrics);
			sent_any_sample = sent_any_sample || sent;

		if (ready && client.isAuthenticated()) {
			const auto now = std::chrono::steady_clock::now();
			if (last_remote_config_sync == std::chrono::steady_clock::time_point {} ||
			    now - last_remote_config_sync >= kRemoteConfigSyncInterval) {
				if (const auto remote_config = client.fetchAgentConfig(metrics.hostname); remote_config.has_value()) {
					config.setCollectionInterval(std::chrono::milliseconds(remote_config->collection_interval_ms));
					config.setGpuEnabled(remote_config->enable_gpu);
					collector.setGpuEnabled(remote_config->enable_gpu);
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

		std::this_thread::sleep_for(config.getCollectionInterval());
	}

	return sent_any_sample ? 0 : 1;
}

} // namespace

} // namespace btop::distributed::client

int main(int argc, char* argv[]) {
	std::vector<std::string> args;
	args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
	for (int i = 1; i < argc; ++i) {
		args.emplace_back(argv[i]);
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
