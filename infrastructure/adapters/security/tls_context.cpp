// ============================================================================
// TLSContext - Implementation
// ============================================================================
// See tls_context.hpp for the contract.
// ============================================================================

#include "tls_context.hpp"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace neurometrix::router::infrastructure::security {

namespace {

// ---------------------------------------------------------------------------
// RAII wrappers for OpenSSL objects
// ---------------------------------------------------------------------------
// Each wrapper owns a pointer and frees it on destruction, using the
// correct OpenSSL free function for its type. This guarantees no leaks
// even on error paths or in the presence of unexpected returns.
// ---------------------------------------------------------------------------

struct BioDeleter {
    void operator()(BIO* p) const noexcept {
        if (p) BIO_free(p);
    }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

struct X509Deleter {
    void operator()(X509* p) const noexcept {
        if (p) X509_free(p);
    }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

struct EvpPkeyDeleter {
    void operator()(EVP_PKEY* p) const noexcept {
        if (p) EVP_PKEY_free(p);
    }
};
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

// ---------------------------------------------------------------------------
// File helpers
// ---------------------------------------------------------------------------

[[nodiscard]] bool file_exists_and_readable(const std::string& path) noexcept {
    try {
        std::error_code ec;
        const auto status = std::filesystem::status(path, ec);
        if (ec) return false;
        if (!std::filesystem::exists(status)) return false;
        if (!std::filesystem::is_regular_file(status)) return false;
        // Attempt to open for reading.
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) return false;
        std::fclose(fp);
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] std::uint64_t file_size_bytes(const std::string& path) noexcept {
    try {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) return 0;
        return static_cast<std::uint64_t>(size);
    } catch (...) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// PEM loaders
// ---------------------------------------------------------------------------

[[nodiscard]] std::expected<X509Ptr, TlsError>
load_certificate(const std::string& path, TlsError missing_err,
                 TlsError unreadable_err, TlsError empty_err,
                 TlsError format_err) noexcept {
    if (!file_exists_and_readable(path)) {
        return std::unexpected(missing_err);
    }
    if (file_size_bytes(path) == 0) {
        return std::unexpected(empty_err);
    }

    BioPtr bio{BIO_new_file(path.c_str(), "r")};
    if (!bio) {
        return std::unexpected(unreadable_err);
    }

    X509Ptr cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
    if (!cert) {
        return std::unexpected(format_err);
    }
    return cert;
}

[[nodiscard]] std::expected<EvpPkeyPtr, TlsError>
load_private_key(const std::string& path) noexcept {
    if (!file_exists_and_readable(path)) {
        return std::unexpected(TlsError::KeyFileMissing);
    }
    if (file_size_bytes(path) == 0) {
        return std::unexpected(TlsError::EmptyKeyFile);
    }

    BioPtr bio{BIO_new_file(path.c_str(), "r")};
    if (!bio) {
        return std::unexpected(TlsError::KeyFileUnreadable);
    }

    EvpPkeyPtr key{PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr)};
    if (!key) {
        return std::unexpected(TlsError::InvalidKeyFormat);
    }
    return key;
}

// ---------------------------------------------------------------------------
// Fingerprint computation
// ---------------------------------------------------------------------------

[[nodiscard]] std::expected<CaFingerprint, TlsError>
compute_sha256(X509* cert) noexcept {
    CaFingerprint fp{};
    unsigned int len = 0;
    if (X509_digest(cert, EVP_sha256(), fp.bytes.data(), &len) != 1) {
        return std::unexpected(TlsError::InvalidCertFormat);
    }
    if (len != fp.bytes.size()) {
        return std::unexpected(TlsError::InvalidCertFormat);
    }
    return fp;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// CaFingerprint::to_hex
// ---------------------------------------------------------------------------
std::string CaFingerprint::to_hex() const noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// TLSContext constructor
// ---------------------------------------------------------------------------
TLSContext::TLSContext(
    std::string ca_path,
    std::string cert_path,
    std::string key_path,
    CaFingerprint fingerprint,
    std::uint64_t ca_size,
    std::uint64_t cert_size,
    std::uint64_t key_size
) noexcept
    : ca_path_(std::move(ca_path)),
      cert_path_(std::move(cert_path)),
      key_path_(std::move(key_path)),
      ca_fingerprint_(fingerprint),
      ca_size_(ca_size),
      cert_size_(cert_size),
      key_size_(key_size)
{}

// ---------------------------------------------------------------------------
// TLSContext::load
// ---------------------------------------------------------------------------
std::expected<TLSContext, TlsError>
TLSContext::load(
    std::string_view ca_path,
    std::string_view cert_path,
    std::string_view key_path
) noexcept {
    try {
        const std::string ca_str{ca_path};
        const std::string cert_str{cert_path};
        const std::string key_str{key_path};

        // ---- Step 1: load CA certificate ----------------------------------
        auto ca = load_certificate(
            ca_str,
            TlsError::CaFileMissing,
            TlsError::CaFileUnreadable,
            TlsError::EmptyCaFile,
            TlsError::InvalidCertFormat);
        if (!ca) return std::unexpected(ca.error());

        // ---- Step 2: load client certificate ------------------------------
        auto cert = load_certificate(
            cert_str,
            TlsError::CertFileMissing,
            TlsError::CertFileUnreadable,
            TlsError::EmptyCertFile,
            TlsError::InvalidCertFormat);
        if (!cert) return std::unexpected(cert.error());

        // ---- Step 3: load client private key ------------------------------
        auto key = load_private_key(key_str);
        if (!key) return std::unexpected(key.error());

        // ---- Step 4: verify that certificate and key match ----------------
        if (X509_check_private_key(cert->get(), key->get()) != 1) {
            return std::unexpected(TlsError::CertKeyMismatch);
        }

        // ---- Step 5: compute SHA-256 fingerprint of CA --------------------
        auto fingerprint = compute_sha256(ca->get());
        if (!fingerprint) return std::unexpected(fingerprint.error());

        // ---- Step 6: record file sizes for audit --------------------------
        const std::uint64_t ca_size   = file_size_bytes(ca_str);
        const std::uint64_t cert_size = file_size_bytes(cert_str);
        const std::uint64_t key_size  = file_size_bytes(key_str);

        return TLSContext(
            ca_str,
            cert_str,
            key_str,
            *fingerprint,
            ca_size,
            cert_size,
            key_size);
    } catch (...) {
        return std::unexpected(TlsError::UnknownError);
    }
}

} // namespace neurometrix::router::infrastructure::security
