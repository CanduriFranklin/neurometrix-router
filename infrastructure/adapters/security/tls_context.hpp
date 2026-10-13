#pragma once

// ============================================================================
// TLSContext - Infrastructure Adapter
// ============================================================================
// Centralized management of TLS certificates for the router microservice.
//
// Both the Kafka adapter and the PostgreSQL adapter consume the same
// certificate set:
//   - CA root certificate (for verifying the server)
//   - Client certificate (for mutual TLS authentication)
//   - Client private key (never logged, never exposed)
//
// This adapter:
//   1. Verifies that all three files exist and are readable.
//   2. Verifies that the client certificate and private key match.
//   3. Computes the SHA-256 fingerprint of the CA certificate, so it can
//      be pinned in the audit log and compared across restarts.
//
// Design principles:
//   - Fail-fast at startup. A misconfigured TLS setup must abort the process
//     before any network connection is attempted.
//   - No exceptions. All errors travel through std::expected.
//   - No TLS library in this header. Only paths and metadata are exposed.
//   - Private key content is never logged. Only its file path is recorded.
// ============================================================================

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace neurometrix::router::infrastructure::security {

// Errors reported by the TLS context.
enum class TlsError : std::uint8_t {
    CaFileMissing,
    CertFileMissing,
    KeyFileMissing,
    CaFileUnreadable,
    CertFileUnreadable,
    KeyFileUnreadable,
    EmptyCaFile,
    EmptyCertFile,
    EmptyKeyFile,
    CertKeyMismatch,
    InvalidCertFormat,
    InvalidKeyFormat,
    IoError,
    UnknownError,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(TlsError e) noexcept {
    switch (e) {
        case TlsError::CaFileMissing:      return "ca_file_missing";
        case TlsError::CertFileMissing:    return "cert_file_missing";
        case TlsError::KeyFileMissing:     return "key_file_missing";
        case TlsError::CaFileUnreadable:   return "ca_file_unreadable";
        case TlsError::CertFileUnreadable: return "cert_file_unreadable";
        case TlsError::KeyFileUnreadable:  return "key_file_unreadable";
        case TlsError::EmptyCaFile:        return "empty_ca_file";
        case TlsError::EmptyCertFile:      return "empty_cert_file";
        case TlsError::EmptyKeyFile:       return "empty_key_file";
        case TlsError::CertKeyMismatch:    return "cert_key_mismatch";
        case TlsError::InvalidCertFormat:  return "invalid_cert_format";
        case TlsError::InvalidKeyFormat:   return "invalid_key_format";
        case TlsError::IoError:            return "io_error";
        case TlsError::UnknownError:       return "unknown_error";
    }
    return "unknown";
}

// SHA-256 fingerprint of the CA certificate.
// Stored as 32 bytes. Can be hex-encoded by callers for logs.
struct CaFingerprint {
    std::array<std::uint8_t, 32> bytes;

    [[nodiscard]] std::string to_hex() const noexcept;
};

// TLS context. Immutable after construction. Thread-safe for read access.
//
// The context does not open connections. It only validates that the
// certificate set is correct and provides the paths and metadata to the
// adapters that need them (Kafka, PostgreSQL).
class TLSContext {
public:
    // Loads and validates the TLS certificate set.
    //
    // Parameters:
    //   ca_path   : path to the CA root certificate (PEM)
    //   cert_path : path to the client certificate (PEM)
    //   key_path  : path to the client private key (PEM, unencrypted)
    //
    // Returns:
    //   - TLSContext on success
    //   - TlsError on the first failure encountered
    [[nodiscard]] static std::expected<TLSContext, TlsError>
    load(
        std::string_view ca_path,
        std::string_view cert_path,
        std::string_view key_path
    ) noexcept;

    // ------------------------------------------------------------------------
    // Accessors (all const, all noexcept)
    // ------------------------------------------------------------------------
    [[nodiscard]] const std::string& ca_path() const noexcept { return ca_path_; }
    [[nodiscard]] const std::string& cert_path() const noexcept { return cert_path_; }
    [[nodiscard]] const std::string& key_path() const noexcept { return key_path_; }

    // Fingerprint of the CA certificate. Used for pinning and audit.
    [[nodiscard]] const CaFingerprint& ca_fingerprint() const noexcept {
        return ca_fingerprint_;
    }

    // Size of each file in bytes. Useful for audit logs and for detecting
    // truncation on restart.
    [[nodiscard]] std::uint64_t ca_size_bytes() const noexcept { return ca_size_; }
    [[nodiscard]] std::uint64_t cert_size_bytes() const noexcept { return cert_size_; }
    [[nodiscard]] std::uint64_t key_size_bytes() const noexcept { return key_size_; }

private:
    TLSContext(
        std::string ca_path,
        std::string cert_path,
        std::string key_path,
        CaFingerprint fingerprint,
        std::uint64_t ca_size,
        std::uint64_t cert_size,
        std::uint64_t key_size
    ) noexcept;

    std::string    ca_path_;
    std::string    cert_path_;
    std::string    key_path_;
    CaFingerprint  ca_fingerprint_;
    std::uint64_t  ca_size_;
    std::uint64_t  cert_size_;
    std::uint64_t  key_size_;
};

} // namespace neurometrix::router::infrastructure::security
