#ifdef DISTRIBUTED_MONITORING

#include "daemon_manager.hpp"
#include "daemon_logger.hpp"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <cstring>

namespace btop::distributed::client {

// Static pointer for signal handling
static DaemonManager* g_daemon_instance = nullptr;

class DaemonManager::Impl {
public:
    Impl() : is_daemon_(false), pid_file_path_("") {}
    
    bool is_daemon_;
    std::string pid_file_path_;
    
    static void signalHandler(int /*sig*/) {
        if (g_daemon_instance) {
            g_daemon_instance->gracefulShutdown();
        }
    }
};

DaemonManager::DaemonManager() : pImpl_(std::make_unique<Impl>()) {
    g_daemon_instance = this;
}

DaemonManager::~DaemonManager() {
    if (g_daemon_instance == this) {
        g_daemon_instance = nullptr;
    }
    removePidFile();
}

bool DaemonManager::daemonize() {
    // Fork the first time
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "First fork failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Exit parent process
    if (pid > 0) {
        exit(0);
    }
    
    // Create new session and become session leader
    if (setsid() < 0) {
        std::cerr << "setsid failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Fork the second time to ensure we can't acquire a controlling terminal
    pid = fork();
    if (pid < 0) {
        std::cerr << "Second fork failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Exit first child
    if (pid > 0) {
        exit(0);
    }
    
    // Change working directory to root to avoid keeping any directory in use
    if (chdir("/") < 0) {
        std::cerr << "chdir to / failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set file permissions mask
    umask(0);
    
    // Close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }
    
    // Redirect stdin, stdout, stderr to /dev/null initially
    int fd = open("/dev/null", O_RDWR);
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }
    
    fd = open("/dev/null", O_RDWR);
    if (fd != 1) {
        dup2(fd, 1);
        close(fd);
    }
    
    fd = open("/dev/null", O_RDWR);
    if (fd != 2) {
        dup2(fd, 2);
        close(fd);
    }
    
    pImpl_->is_daemon_ = true;
    return true;
}

void DaemonManager::createPidFile(const std::string& pid_file_path) {
    if (pid_file_path.empty()) {
        return;
    }
    
    pImpl_->pid_file_path_ = pid_file_path;
    
    std::ofstream pid_file(pid_file_path);
    if (pid_file.is_open()) {
        pid_file << getpid() << std::endl;
        pid_file.close();
    } else {
        std::cerr << "Failed to create PID file: " << pid_file_path << std::endl;
    }
}

void DaemonManager::removePidFile() {
    if (!pImpl_->pid_file_path_.empty()) {
        if (unlink(pImpl_->pid_file_path_.c_str()) != 0 && errno != ENOENT) {
            std::cerr << "Failed to remove PID file: " << pImpl_->pid_file_path_ 
                      << " - " << strerror(errno) << std::endl;
        }
        pImpl_->pid_file_path_.clear();
    }
}

void DaemonManager::setupSignalHandlers() {
    // Set up signal handlers for graceful shutdown
    signal(SIGTERM, Impl::signalHandler);
    signal(SIGINT, Impl::signalHandler);
    signal(SIGHUP, Impl::signalHandler);
    
    // Ignore SIGPIPE to handle broken connections gracefully
    signal(SIGPIPE, SIG_IGN);
}

void DaemonManager::redirectOutput(const std::string& log_file_path) {
    if (log_file_path.empty()) {
        return;
    }
    
    // Initialize the global daemon logger
    if (!g_daemon_logger) {
        g_daemon_logger = std::make_unique<DaemonLogger>();
    }
    
    if (!g_daemon_logger->initialize(log_file_path, DaemonLogger::LogLevel::INFO)) {
        std::cerr << "Failed to initialize daemon logger with file: " << log_file_path << std::endl;
        return;
    }
    
    // Open log file for writing (append mode) for stdout/stderr redirection
    int log_fd = open(log_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0) {
        std::cerr << "Failed to open log file: " << log_file_path 
                  << " - " << strerror(errno) << std::endl;
        return;
    }
    
    // Redirect stdout and stderr to log file
    if (dup2(log_fd, STDOUT_FILENO) < 0) {
        std::cerr << "Failed to redirect stdout to log file: " << strerror(errno) << std::endl;
    }
    
    if (dup2(log_fd, STDERR_FILENO) < 0) {
        std::cerr << "Failed to redirect stderr to log file: " << strerror(errno) << std::endl;
    }
    
    close(log_fd);
}

bool DaemonManager::isRunningAsDaemon() const {
    return pImpl_->is_daemon_;
}

void DaemonManager::gracefulShutdown() {
    // Clean up PID file
    removePidFile();
    
    // Exit gracefully
    exit(0);
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING
