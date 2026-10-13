// ============================================================================
// ConfigLoader - Implementation
// ============================================================================
// See config_loader.hpp for the contract.
// ============================================================================

#include "config_loader.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace neurometrix::router::infrastructure::config {

namespace {

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

// Reads an environment variable and returns std::nullopt if it is not
// present. An empty string is considered present but invalid; the caller
// decides whether empty is acceptable.
[[nodiscard]] std::optional<std::string> read_env(std::string_view name) noexcept {
    const std::string key{name};
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string{value};
}

// Reads a required environment variable. Returns the value if present and
// non-empty, otherwise a precise ConfigError.
[[nodiscard]] std::expected<std::string, ConfigError>
read_required(std::string_view name) noexcept {
    auto value = read_env(name);
    if (!value.has_value()) {
        return std::unexpected(ConfigError::MissingRequiredVariable);
    }
    if (value->empty()) {
        return std::unexpected(ConfigError::EmptyValue);
    }
    return *value;
}

// Reads a required environment variable with a fallback default value.
// Useful for values like ports or pool sizes where a sensible default
// exists.
[[nodiscard]] std::string
read_with_default(std::string_view name, std::string_view default_value) noexcept {
    auto value = read_env(name);
    if (!value.has_value() || value->empty()) {
        return std::string{default_value};
    }
    return *value;
}

// Parses an unsigned integer from a string. Uses std::from_chars, which
// does not throw and does not allocate.
[[nodiscard]] std::expected<std::uint64_t, ConfigError>
parse_uint64(std::string_view value) noexcept {
    std::uint64_t result = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto [ptr, ec] = std::from_chars(begin, end, result);
    if (ec != std::errc{} || ptr != end) {
        return std::unexpected(ConfigError::InvalidInteger);
    }
    return result;
}

// Parses a uint32 with range check.
[[nodiscard]] std::expected<std::uint32_t, ConfigError>
parse_uint32(std::string_view value) noexcept {
    auto parsed = parse_uint64(value);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (*parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(ConfigError::InvalidInteger);
    }
    return static_cast<std::uint32_t>(*parsed);
}

// Parses a uint16 with range check.
[[nodiscard]] std::expected<std::uint16_t, ConfigError>
parse_uint16(std::string_view value) noexcept {
    auto parsed = parse_uint64(value);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (*parsed > std::numeric_limits<std::uint16_t>::max()) {
        return std::unexpected(ConfigError::InvalidInteger);
    }
    return static_cast<std::uint16_t>(*parsed);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// load_kafka_config
// ---------------------------------------------------------------------------
std::expected<KafkaConfig, ConfigError> load_kafka_config() noexcept {
    KafkaConfig cfg{};

    auto bootstrap = read_required(env_vars::kKafkaBootstrapServers);
    if (!bootstrap) return std::unexpected(bootstrap.error());
    cfg.bootstrap_servers = std::move(*bootstrap);

    auto protocol = read_required(env_vars::kKafkaSecurityProtocol);
    if (!protocol) return std::unexpected(protocol.error());
    cfg.security_protocol = std::move(*protocol);

    auto mechanism = read_required(env_vars::kKafkaSaslMechanism);
    if (!mechanism) return std::unexpected(mechanism.error());
    cfg.sasl_mechanism = std::move(*mechanism);

    auto username = read_required(env_vars::kKafkaSaslUsername);
    if (!username) return std::unexpected(username.error());
    cfg.sasl_username = std::move(*username);

    auto password = read_required(env_vars::kKafkaSaslPassword);
    if (!password) return std::unexpected(password.error());
    cfg.sasl_password = std::move(*password);

    auto ca = read_required(env_vars::kKafkaSslCaLocation);
    if (!ca) return std::unexpected(ca.error());
    cfg.ssl_ca_location = std::move(*ca);

    auto cert = read_required(env_vars::kKafkaSslCertLocation);
    if (!cert) return std::unexpected(cert.error());
    cfg.ssl_cert_location = std::move(*cert);

    auto key = read_required(env_vars::kKafkaSslKeyLocation);
    if (!key) return std::unexpected(key.error());
    cfg.ssl_key_location = std::move(*key);

    auto topic = read_required(env_vars::kKafkaTopicRequests);
    if (!topic) return std::unexpected(topic.error());
    cfg.topic_requests = std::move(*topic);

    auto group = read_required(env_vars::kKafkaConsumerGroup);
    if (!group) return std::unexpected(group.error());
    cfg.consumer_group = std::move(*group);

    auto timeout = parse_uint32(read_with_default(env_vars::kKafkaTimeoutMs, "5000"));
    if (!timeout) return std::unexpected(timeout.error());
    cfg.timeout_ms = *timeout;

    return cfg;
}

// ---------------------------------------------------------------------------
// load_postgres_config
// ---------------------------------------------------------------------------
std::expected<PostgresConfig, ConfigError> load_postgres_config() noexcept {
    PostgresConfig cfg{};

    auto url = read_required(env_vars::kDatabaseUrl);
    if (!url) return std::unexpected(url.error());
    cfg.connection_string = std::move(*url);

    auto ssl_mode = read_required(env_vars::kDatabaseSslMode);
    if (!ssl_mode) return std::unexpected(ssl_mode.error());
    cfg.ssl_mode = std::move(*ssl_mode);

    auto cert = read_required(env_vars::kDatabaseSslCert);
    if (!cert) return std::unexpected(cert.error());
    cfg.ssl_cert_location = std::move(*cert);

    auto key = read_required(env_vars::kDatabaseSslKey);
    if (!key) return std::unexpected(key.error());
    cfg.ssl_key_location = std::move(*key);

    auto root_cert = read_required(env_vars::kDatabaseSslRootCert);
    if (!root_cert) return std::unexpected(root_cert.error());
    cfg.ssl_root_cert_location = std::move(*root_cert);

    auto pool_min = parse_uint32(read_with_default(env_vars::kDatabasePoolMin, "2"));
    if (!pool_min) return std::unexpected(pool_min.error());
    cfg.pool_min_connections = *pool_min;

    auto pool_max = parse_uint32(read_with_default(env_vars::kDatabasePoolMax, "10"));
    if (!pool_max) return std::unexpected(pool_max.error());
    cfg.pool_max_connections = *pool_max;

    if (cfg.pool_min_connections > cfg.pool_max_connections) {
        return std::unexpected(ConfigError::InvalidInteger);
    }

    auto timeout = parse_uint32(read_with_default(env_vars::kDatabaseTimeoutMs, "5000"));
    if (!timeout) return std::unexpected(timeout.error());
    cfg.connection_timeout_ms = *timeout;

    return cfg;
}

// ---------------------------------------------------------------------------
// load_metrics_config
// ---------------------------------------------------------------------------
std::expected<MetricsConfig, ConfigError> load_metrics_config() noexcept {
    MetricsConfig cfg{};

    auto port = parse_uint16(read_with_default(env_vars::kMetricsPort, "9090"));
    if (!port) return std::unexpected(port.error());
    cfg.port = *port;

    cfg.bind_address = read_with_default(env_vars::kMetricsBindAddress, "0.0.0.0");

    return cfg;
}

// ---------------------------------------------------------------------------
// load_logging_config
// ---------------------------------------------------------------------------
std::expected<LoggingConfig, ConfigError> load_logging_config() noexcept {
    LoggingConfig cfg{};

    cfg.level = read_with_default(env_vars::kLogLevel, "info");
    cfg.format = read_with_default(env_vars::kLogFormat, "json");

    return cfg;
}

// ---------------------------------------------------------------------------
// load_config
// ---------------------------------------------------------------------------
std::expected<Config, ConfigError> load_config() noexcept {
    Config cfg{};

    auto kafka = load_kafka_config();
    if (!kafka) return std::unexpected(kafka.error());
    cfg.kafka = std::move(*kafka);

    auto postgres = load_postgres_config();
    if (!postgres) return std::unexpected(postgres.error());
    cfg.postgres = std::move(*postgres);

    auto metrics = load_metrics_config();
    if (!metrics) return std::unexpected(metrics.error());
    cfg.metrics = *metrics;

    auto logging = load_logging_config();
    if (!logging) return std::unexpected(logging.error());
    cfg.logging = std::move(*logging);

    return cfg;
}

} // namespace neurometrix::router::infrastructure::config