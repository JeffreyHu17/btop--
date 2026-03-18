#include "auth_interface.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace btop {
namespace distributed {

// TokenAuthProvider implementation
TokenAuthProvider::TokenAuthProvider(const std::string& token, std::chrono::seconds lifetime)
    : expected_token_(token), token_lifetime_(lifetime) {
}

AuthResult TokenAuthProvider::validateCredentials(const Credentials& credentials) {
    if (credentials.method != AuthMethod::TOKEN) {
        return AuthResult::UNSUPPORTED_METHOD;
    }
    
    if (credentials.isExpired()) {
        return AuthResult::EXPIRED;
    }
    
    if (credentials.token != expected_token_) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    return AuthResult::SUCCESS;
}

std::unique_ptr<Credentials> TokenAuthProvider::refreshCredentials(const Credentials& credentials) {
    if (credentials.method != AuthMethod::TOKEN) {
        return nullptr;
    }
    
    // For token auth, we can't refresh - would need external token service
    // Return nullptr to indicate refresh not possible
    return nullptr;
}

std::vector<AuthMethod> TokenAuthProvider::getSupportedMethods() const {
    return {AuthMethod::TOKEN};
}

bool TokenAuthProvider::supportsMethod(AuthMethod method) const {
    return method == AuthMethod::TOKEN;
}

void TokenAuthProvider::setToken(const std::string& token) {
    expected_token_ = token;
}

void TokenAuthProvider::setTokenLifetime(std::chrono::seconds lifetime) {
    token_lifetime_ = lifetime;
}

// CertificateAuthProvider implementation
CertificateAuthProvider::CertificateAuthProvider(const std::string& ca_cert_path,
                                               const std::string& server_cert_path,
                                               const std::string& server_key_path)
    : ca_cert_path_(ca_cert_path), server_cert_path_(server_cert_path), server_key_path_(server_key_path) {
}

AuthResult CertificateAuthProvider::validateCredentials(const Credentials& credentials) {
    if (credentials.method != AuthMethod::CERTIFICATE) {
        return AuthResult::UNSUPPORTED_METHOD;
    }
    
    if (credentials.isExpired()) {
        return AuthResult::EXPIRED;
    }
    
    if (credentials.certificate.empty()) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    // Validate certificate against CA
    if (!validateCertificate(credentials.certificate)) {
        return AuthResult::INVALID_CREDENTIALS;
    }
    
    return AuthResult::SUCCESS;
}

std::unique_ptr<Credentials> CertificateAuthProvider::refreshCredentials(const Credentials& credentials) {
    if (credentials.method != AuthMethod::CERTIFICATE) {
        return nullptr;
    }
    
    // For certificate auth, we can't refresh - would need certificate authority
    // Return nullptr to indicate refresh not possible
    return nullptr;
}

std::vector<AuthMethod> CertificateAuthProvider::getSupportedMethods() const {
    return {AuthMethod::CERTIFICATE};
}

bool CertificateAuthProvider::supportsMethod(AuthMethod method) const {
    return method == AuthMethod::CERTIFICATE;
}

bool CertificateAuthProvider::validateCertificate(const std::string& cert_data) const {
    // Basic certificate validation - in real implementation would use OpenSSL
    // For now, just check that certificate data is not empty and contains basic PEM structure
    if (cert_data.empty()) {
        return false;
    }
    
    // Check for PEM certificate markers
    if (cert_data.find("-----BEGIN CERTIFICATE-----") == std::string::npos ||
        cert_data.find("-----END CERTIFICATE-----") == std::string::npos) {
        return false;
    }
    
    // In a real implementation, this would:
    // 1. Parse the certificate using OpenSSL
    // 2. Verify signature against CA certificate
    // 3. Check certificate validity period
    // 4. Verify certificate chain
    
    return true; // Simplified validation for now
}

// AuthManager implementation
AuthManager::AuthManager() : preferred_method_(AuthMethod::TOKEN) {
}

void AuthManager::addProvider(std::unique_ptr<AuthProvider> provider) {
    if (provider) {
        providers_.push_back(std::move(provider));
    }
}

void AuthManager::setPreferredMethod(AuthMethod method) {
    preferred_method_ = method;
}

AuthResult AuthManager::authenticate(const Credentials& credentials) {
    // Try to find a provider that supports the credential method
    for (const auto& provider : providers_) {
        if (provider->supportsMethod(credentials.method)) {
            return provider->validateCredentials(credentials);
        }
    }
    
    return AuthResult::UNSUPPORTED_METHOD;
}

std::unique_ptr<Credentials> AuthManager::refreshCredentials(const Credentials& credentials) {
    // Try to find a provider that can refresh the credentials
    for (const auto& provider : providers_) {
        if (provider->supportsMethod(credentials.method)) {
            auto refreshed = provider->refreshCredentials(credentials);
            if (refreshed) {
                return refreshed;
            }
        }
    }
    
    return nullptr;
}

std::vector<AuthMethod> AuthManager::getSupportedMethods() const {
    std::vector<AuthMethod> methods;
    
    for (const auto& provider : providers_) {
        auto provider_methods = provider->getSupportedMethods();
        for (AuthMethod method : provider_methods) {
            // Avoid duplicates
            if (std::find(methods.begin(), methods.end(), method) == methods.end()) {
                methods.push_back(method);
            }
        }
    }
    
    return methods;
}

bool AuthManager::supportsMethod(AuthMethod method) const {
    for (const auto& provider : providers_) {
        if (provider->supportsMethod(method)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<Credentials> AuthManager::validateAndRefresh(const Credentials& credentials) {
    // First check if credentials are expired
    if (credentials.isExpired()) {
        // Try to refresh
        return refreshCredentials(credentials);
    }
    
    // Credentials not expired, validate them
    AuthResult result = authenticate(credentials);
    if (result == AuthResult::SUCCESS) {
        // Return a copy of the valid credentials
        auto valid_creds = std::make_unique<Credentials>(credentials);
        return valid_creds;
    }
    
    return nullptr;
}

} // namespace distributed
} // namespace btop