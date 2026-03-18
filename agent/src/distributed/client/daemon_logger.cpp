#ifdef DISTRIBUTED_MONITORING

#include "daemon_logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <ctime>

namespace btop::distributed::client {

// Global logger instance
std::unique_ptr<DaemonLogger> g_daemon_logger = nullptr;

class DaemonLogger::Impl {
public:
    Impl() : min_level_(LogLevel::INFO), max_file_size_(10 * 1024 * 1024), // 10MB default
             max_backup_files_(5), initialized_(false) {}
    
    std::string log_file_path_;
    std::ofstream log_file_;
    LogLevel min_level_;
    size_t max_file_size_;
    int max_backup_files_;
    bool initialized_;
    std::mutex log_mutex_;
    
    std::string formatTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }
    
    std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
    
    size_t getFileSize() {
        if (!std::filesystem::exists(log_file_path_)) {
            return 0;
        }
        try {
            return std::filesystem::file_size(log_file_path_);
        } catch (const std::filesystem::filesystem_error&) {
            return 0;
        }
    }
    
    void rotateFiles() {
        log_file_.close();
        
        // Remove oldest backup if it exists
        std::string oldest_backup = log_file_path_ + "." + std::to_string(max_backup_files_);
        if (std::filesystem::exists(oldest_backup)) {
            std::filesystem::remove(oldest_backup);
        }
        
        // Rotate existing backups
        for (int i = max_backup_files_ - 1; i >= 1; i--) {
            std::string old_name = log_file_path_ + "." + std::to_string(i);
            std::string new_name = log_file_path_ + "." + std::to_string(i + 1);
            
            if (std::filesystem::exists(old_name)) {
                std::filesystem::rename(old_name, new_name);
            }
        }
        
        // Move current log to .1
        if (std::filesystem::exists(log_file_path_)) {
            std::string backup_name = log_file_path_ + ".1";
            std::filesystem::rename(log_file_path_, backup_name);
        }
        
        // Reopen log file
        log_file_.open(log_file_path_, std::ios::out | std::ios::app);
    }
};

DaemonLogger::DaemonLogger() : pImpl_(std::make_unique<Impl>()) {}

DaemonLogger::~DaemonLogger() {
    if (pImpl_->log_file_.is_open()) {
        pImpl_->log_file_.close();
    }
}

bool DaemonLogger::initialize(const std::string& log_file_path, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    
    pImpl_->log_file_path_ = log_file_path;
    pImpl_->min_level_ = min_level;
    
    // Create directory if it doesn't exist
    std::filesystem::path log_path(log_file_path);
    std::filesystem::path log_dir = log_path.parent_path();
    
    if (!log_dir.empty() && !std::filesystem::exists(log_dir)) {
        try {
            std::filesystem::create_directories(log_dir);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Failed to create log directory: " << e.what() << std::endl;
            return false;
        }
    }
    
    // Open log file
    pImpl_->log_file_.open(log_file_path, std::ios::out | std::ios::app);
    if (!pImpl_->log_file_.is_open()) {
        std::cerr << "Failed to open log file: " << log_file_path << std::endl;
        return false;
    }
    
    pImpl_->initialized_ = true;
    
    // Log initialization message
    log(LogLevel::INFO, "DaemonLogger initialized - log file: " + log_file_path);
    
    return true;
}

void DaemonLogger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    pImpl_->min_level_ = level;
}

void DaemonLogger::setMaxFileSize(size_t max_size_bytes) {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    pImpl_->max_file_size_ = max_size_bytes;
}

void DaemonLogger::setMaxBackupFiles(int max_backups) {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    pImpl_->max_backup_files_ = max_backups;
}

void DaemonLogger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void DaemonLogger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void DaemonLogger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void DaemonLogger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void DaemonLogger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    
    if (!pImpl_->initialized_ || level < pImpl_->min_level_) {
        return;
    }
    
    if (!pImpl_->log_file_.is_open()) {
        return;
    }
    
    // Format log entry
    std::string timestamp = pImpl_->formatTimestamp();
    std::string level_str = pImpl_->levelToString(level);
    
    pImpl_->log_file_ << "[" << timestamp << "] [" << level_str << "] " << message << std::endl;
    pImpl_->log_file_.flush();
    
    // Check if rotation is needed
    rotateLogIfNeeded();
}

void DaemonLogger::rotateLogIfNeeded() {
    if (!pImpl_->initialized_) {
        return;
    }
    
    size_t current_size = pImpl_->getFileSize();
    if (current_size >= pImpl_->max_file_size_) {
        pImpl_->rotateFiles();
    }
}

void DaemonLogger::forceRotate() {
    std::lock_guard<std::mutex> lock(pImpl_->log_mutex_);
    
    if (pImpl_->initialized_) {
        pImpl_->rotateFiles();
    }
}

bool DaemonLogger::isInitialized() const {
    return pImpl_->initialized_;
}

std::string DaemonLogger::getLogFilePath() const {
    return pImpl_->log_file_path_;
}

} // namespace btop::distributed::client

#endif // DISTRIBUTED_MONITORING