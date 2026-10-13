#pragma once

// ============================================================================
// ConfigLoader - Infrastructure Adapter
// ============================================================================
// Reads and validates the router's runtime configuration from environment
// variables, following the 12-Factor App principle.
//
// Design principles:
//   - No secrets in code. All sensitive values come from environment.
//   - Fail-fast: any missing or malformed required variable is reported
//     through std::expected, and the process exits before doing anything.
//   - Strongly-typed configuration: the rest of the codebase receives a
//     Config struct, not a bag of strings.
//   - No dependency on other adapters. This adapter can be used in isolation.
// ============================================================================

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace neurometrix::router::infrastructure::config {

// Errors reported by the configuration loader.
enum class ConfigError : std::uint8_t {
    MissingRequiredVariable,
    InvalidInteger,
    InvalidBoolean,
    InvalidDuration,
    EmptyValue,
    UnknownError,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(ConfigError e) noexcept {
    switch (e) {
        case ConfigError::MissingRequiredVariable: return "missing_required_variable";
        case ConfigError::InvalidInteger:          return "invalid_integer";
        case ConfigError::InvalidBoolean:          return "invalid_boolean";
        case ConfigError::InvalidDuration:         return "invalid_duration";
        case ConfigError::EmptyValue:              return "empty_value";
        case ConfigError::UnknownError:            return "unknown_error";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Configuration sections
// ---------------------------------------------------------------------------
// Split by concern so each adapter only reads what it needs. This keeps
// the configuration surface small and the coupling between sections minimal.
// ---------------------------------------------------------------------------

struct KafkaConfig {
    std::string bootstrap_servers;
    std::string security_protocol;      // SASL_SSL, SSL, PLAINTEXT
    std::string sasl_mechanism;         // SCRAM-SHA-512, etc.
    std::string sasl_username;
    std::string sasl_password;
    std::string ssl_ca_location;
    std::string ssl_cert_location;
    std::string ssl_key_location;
    std::string topic_requests;
    std::string consumer_group;
    std::uint32_t timeout_ms;
};

struct PostgresConfig {
    std::string  connection_string;      // postgresql://user:pass@host:port/db
    std::string  ssl_mode;               // disable, require, verify-ca, verify-full
    std::string  ssl_cert_location;
    std::string  ssl_key_location;
    std::string  ssl_root_cert_location;
    std::uint32_t pool_min_connections;
    std::uint32_t pool_max_connections;
    std::uint32_t connection_timeout_ms;
};

struct MetricsConfig {
    std::uint16_t port;
    std::string   bind_address;
};

struct LoggingConfig {
    std::string level;     // trace, debug, info, warn, error, critical
    std::string format;    // json, text
};

struct Config {
    KafkaConfig    kafka;
    PostgresConfig postgres;
    MetricsConfig  metrics;
    LoggingConfig  logging;
};

// Loads the full configuration from environment variables.
//
// Returns:
//   - Config on success
//   - ConfigError if any required variable is missing, empty, or malformed
//
// The loader never throws. All errors travel through std::expected.
[[nodiscard]] std::expected<Config, ConfigError> load_config() noexcept;

// ---------------------------------------------------------------------------
// Individual loaders (exposed for testability)
// ---------------------------------------------------------------------------
// Each section can be loaded independently, allowing tests to verify only
// the section they care about.

[[nodiscard]] std::expected<KafkaConfig, ConfigError>
load_kafka_config() noexcept;

[[nodiscard]] std::expected<PostgresConfig, ConfigError>
load_postgres_config() noexcept;

[[nodiscard]] std::expected<MetricsConfig, ConfigError>
load_metrics_config() noexcept;

[[nodiscard]] std::expected<LoggingConfig, ConfigError>
load_logging_config() noexcept;

// ---------------------------------------------------------------------------
// Environment variable names (centralized constants)
// ---------------------------------------------------------------------------
// Avoid magic strings scattered across the codebase. Every name used by the
// loader is declared here. The .env.example file must be kept in sync.
// ---------------------------------------------------------------------------
namespace env_vars {
    // Kafka
    inline constexpr std::string_view kKafkaBootstrapServers  = "KAFKA_BOOTSTRAP_SERVERS";
    inline constexpr std::string_view kKafkaSecurityProtocol  = "KAFKA_SECURITY_PROTOCOL";
    inline constexpr std::string_view kKafkaSaslMechanism     = "KAFKA_SASL_MECHANISM";
    inline constexpr std::string_view kKafkaSaslUsername      = "KAFKA_SASL_USERNAME";
    inline constexpr std::string_view kKafkaSaslPassword      = "KAFKA_SASL_PASSWORD";
    inline constexpr std::string_view kKafkaSslCaLocation     = "KAFKA_SSL_CA_LOCATION";
    inline constexpr std::string_view kKafkaSslCertLocation   = "KAFKA_SSL_CERT_LOCATION";
    inline constexpr std::string_view kKafkaSslKeyLocation    = "KAFKA_SSL_KEY_LOCATION";
    inline constexpr std::string_view kKafkaTopicRequests     = "KAFKA_TOPIC_REQUESTS";
    inline constexpr std::string_view kKafkaConsumerGroup     = "KAFKA_CONSUMER_GROUP";
    inline constexpr std::string_view kKafkaTimeoutMs         = "KAFKA_TIMEOUT_MS";

    // PostgreSQL
    inline constexpr std::string_view kDatabaseUrl            = "DATABASE_URL";
    inline constexpr std::string_view kDatabaseSslMode        = "DATABASE_SSLMODE";
    inline constexpr std::string_view kDatabaseSslCert         = "DATABASE_SSL_CERT";
    inline constexpr std::string_view kDatabaseSslKey          = "DATABASE_SSL_KEY";
    inline constexpr std::string_view kDatabaseSslRootCert     = "DATABASE_SSL_ROOT_CERT";
    inline constexpr std::string_view kDatabasePoolMin        = "DATABASE_POOL_MIN";
    inline constexpr std::string_view kDatabasePoolMax        = "DATABASE_POOL_MAX";
    inline constexpr std::string_view kDatabaseTimeoutMs      = "DATABASE_TIMEOUT_MS";

    // Metrics
    inline constexpr std::string_view kMetricsPort            = "METRICS_PORT";
    inline constexpr std::string_view kMetricsBindAddress     = "METRICS_BIND_ADDRESS";

    // Logging
    inline constexpr std::string_view kLogLevel               = "LOG_LEVEL";
    inline constexpr std::string_view kLogFormat              = "LOG_FORMAT";
}

} // namespace neurometrix::router::infrastructure::config
