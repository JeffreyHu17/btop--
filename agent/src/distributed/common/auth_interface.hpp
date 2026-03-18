#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <vector>

namespace btop {
namespace distributed {

/**
 * Authentication method types supported by the system
 */
enum class AuthMethod {
    TOKEN,      // Token-based authentication
    CERTIFICATE // Certificate-based authentication
};

/**
 * Authentication result status
 */
enum class AuthResult {
    SUCCESS,           // Authentication successful
    INVALID_CREDENTIALS, // Invalid token/certificate
    EXPIRED,           // Credentials have expired
    NETWORK_ERROR,     // Network communication error
    UNSUPPORTED_METHOD // Authentication method not supported
};

/**
 * Credential information structure
 */
struct Credentials {
    AuthMethod method;
    std::string token;           // For token-based auth
    std::string certificate;     // For certificate-based auth
    std::string private_key;     // For certificate-based auth
    std::chrono::system_clock::time_point expires_at;
    
    Credentials() : method(AuthMethod::TOKEN) {}
    
    bool isExpired() const {
        return std::chrono::system_clock::now() >= expires_at;
    }
};

/**
 * Abstract base class for authentication providers
 */
class AuthProvider {
public:
    virtual ~AuthProvider() = default;
    
    /**
     * Validate credentials against the authentication system
     * @param credentials The credentials to validate
     * @return Authentication result
     */
    virtual AuthResult validateCredentials(const Credentials& credentials) = 0;
    
    /**
     * Refresh expired credentials if possible
     * @param credentials The credentials to refresh
     * @return New credentials or empty if refresh failed
     */
    virtual std::unique_ptr<Credentials> refreshCredentials(const Credentials& credentials) = 0;
    
    /**
     * Get supported authentication methods
     * @return Vector of supported authentication methods
     */
    virtual std::vector<AuthMethod> getSupportedMethods() const = 0;
    
    /**
     * Check if a specific authentication method is supported
     * @param method The authentication method to check
     * @return True if supported, false otherwise
     */
    virtual bool supportsMethod(AuthMethod method) const = 0;
};

/**
 * Token-based authentication provider
 */
class TokenAuthProvider : public AuthProvider {
private:
    std::string expected_token_;
    std::chrono::seconds token_lifetime_;
    
public:
    explicit TokenAuthProvider(const std::string& token, 
                              std::chrono::seconds lifetime = std::chrono::hours(24));
    
    AuthResult validateCredentials(const Credentials& credentials) override;
    std::unique_ptr<Credentials> refreshCredentials(const Credentials& credentials) override;
    std::vector<AuthMethod> getSupportedMethods() const override;
    bool supportsMethod(AuthMethod method) const override;
    
    void setToken(const std::string& token);
    void setTokenLifetime(std::chrono::seconds lifetime);
};

/**
 * Certificate-based authentication provider
 */
class CertificateAuthProvider : public AuthProvider {
private:
    std::string ca_cert_path_;
    std::string server_cert_path_;
    std::string server_key_path_;
    
public:
    CertificateAuthProvider(const std::string& ca_cert_path,
                           const std::string& server_cert_path,
                           const std::string& server_key_path);
    
    AuthResult validateCredentials(const Credentials& credentials) override;
    std::unique_ptr<Credentials> refreshCredentials(const Credentials& credentials) override;
    std::vector<AuthMethod> getSupportedMethods() const override;
    bool supportsMethod(AuthMethod method) const override;
    
private:
    bool validateCertificate(const std::string& cert_data) const;
};

/**
 * Multi-method authentication manager that supports both token and certificate auth
 */
class AuthManager {
private:
    std::vector<std::unique_ptr<AuthProvider>> providers_;
    AuthMethod preferred_method_;
    
public:
    AuthManager();
    ~AuthManager() = default;
    
    /**
     * Add an authentication provider
     * @param provider The authentication provider to add
     */
    void addProvider(std::unique_ptr<AuthProvider> provider);
    
    /**
     * Set the preferred authentication method
     * @param method The preferred authentication method
     */
    void setPreferredMethod(AuthMethod method);
    
    /**
     * Authenticate using the provided credentials
     * @param credentials The credentials to authenticate with
     * @return Authentication result
     */
    AuthResult authenticate(const Credentials& credentials);
    
    /**
     * Refresh credentials using available providers
     * @param credentials The credentials to refresh
     * @return New credentials or nullptr if refresh failed
     */
    std::unique_ptr<Credentials> refreshCredentials(const Credentials& credentials);
    
    /**
     * Get all supported authentication methods across all providers
     * @return Vector of supported authentication methods
     */
    std::vector<AuthMethod> getSupportedMethods() const;
    
    /**
     * Check if a specific authentication method is supported
     * @param method The authentication method to check
     * @return True if supported, false otherwise
     */
    bool supportsMethod(AuthMethod method) const;
    
    /**
     * Validate credential expiration and handle refresh if needed
     * @param credentials The credentials to validate
     * @return Updated credentials or nullptr if validation failed
     */
    std::unique_ptr<Credentials> validateAndRefresh(const Credentials& credentials);
};

} // namespace distributed
} // namespace btop