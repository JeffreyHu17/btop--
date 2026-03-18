#pragma once

#ifdef DISTRIBUTED_MONITORING

#include <memory>
#include <string>

namespace btop::distributed::client {

/**
 * @brief Daemon mode manager for btop_client
 *
 * Handles process daemonization, PID file management, signal handling,
 * and logging redirection for headless operation.
 */
class DaemonManager {
public:
    DaemonManager();
    ~DaemonManager();

    bool daemonize();
    void createPidFile(const std::string& pid_file_path);
    void removePidFile();
    void setupSignalHandlers();
    void redirectOutput(const std::string& log_file_path);
    bool isRunningAsDaemon() const;
    void gracefulShutdown();

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
