#pragma once

#ifdef DISTRIBUTED_MONITORING

#include <string>
#include <fstream>
#include <memory>
#include <mutex>

namespace btop::distributed::client {

/**
 * @brief Logging system for daemon mode operation
 * 
 * Provides structured logging with different log levels, log rotation,
 * and thread-safe operation for daemon mode btop_client.
 */
class DaemonLogger {
public:
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3
    };
    
    DaemonLogger();
    ~DaemonLogger();
    
    // Configuration
    bool initialize(const std::string& log_file_path, LogLevel min_level = LogLevel::INFO);
    void setLogLevel(LogLevel level);
    void setMaxFileSize(size_t max_size_bytes);
    void setMaxBackupFiles(int max_backups);
    
    // Logging methods
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    
    // Generic logging
    void log(LogLevel level, const std::string& message);
    
    // Log rotation
    void rotateLogIfNeeded();
    void forceRotate();
    
    // Utility
    bool isInitialized() const;
    std::string getLogFilePath() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

// Global logger instance for daemon mode
extern std::unique_ptr<DaemonLogger> g_daemon_logger;

// Convenience macros for logging
#define DAEMON_LOG_DEBUG(msg) if (g_daemon_logger) g_daemon_logger->debug(msg)
#define DAEMON_LOG_INFO(msg) if (g_daemon_logger) g_daemon_logger->info(msg)
#define DAEMON_LOG_WARNING(msg) if (g_daemon_logger) g_daemon_logger->warning(msg)
#define DAEMON_LOG_ERROR(msg) if (g_daemon_logger) g_daemon_logger->error(msg)

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING