#pragma once

// ============================================================================
// Logger - Infrastructure Adapter
// ============================================================================
// Structured JSON logger for the router microservice.
//
// Every log line is emitted as a single JSON object, suitable for direct
// ingestion by observability pipelines (Loki, Elasticsearch, CloudWatch).
//
// Log format (one JSON object per line):
//   {
//     "timestamp":   "2026-10-11T15:04:05.123Z",
//     "level":       "info",
//     "service":     "router",
//     "message":     "Routing decision made",
//     "message_id":  12345,
//     "study_uid":   "1.2.840.113619...",
//     "dimensionality": "3d"
//   }
//
// Design principles:
//   - JSON only. No plain-text logs in production.
//   - Never logs PHI. Only hashed patient IDs and technical identifiers.
//   - Thread-safe.
//   - Configured at startup from the LoggingConfig section.
// ============================================================================

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Forward-declare spdlog to keep this header light. The implementation
// includes <spdlog/spdlog.h>.
namespace spdlog {
class logger;
}

namespace neurometrix::router::infrastructure::logging {

// Structured logger singleton. Access via Logger::instance().
//
// The logger is initialized once at application startup from the
// LoggingConfig section of the configuration loader.
class Logger {
public:
    // Initializes the logger. Must be called exactly once at startup,
    // before any log call.
    //
    // Parameters:
    //   service_name : identifier included in every log line ("router").
    //   level        : one of "trace", "debug", "info", "warn", "error",
    //                  "critical". Defaults to "info" if unrecognized.
    static void initialize(std::string_view service_name,
                           std::string_view level) noexcept;

    // Returns the underlying spdlog logger. Never null after initialize().
    [[nodiscard]] static std::shared_ptr<spdlog::logger> get() noexcept;

    // Explicit shutdown. Flushes pending buffers and releases resources.
    // Called at the end of main().
    static void shutdown() noexcept;

    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger() = delete;

private:
    static std::shared_ptr<spdlog::logger> logger_;
};

// ---------------------------------------------------------------------------
// Structured log helpers
// ---------------------------------------------------------------------------
// These free functions add context fields to the log line without requiring
// the caller to build the JSON manually. They are the recommended way to
// log from application and infrastructure code.
// ---------------------------------------------------------------------------

// Logs a routing decision event with all its context.
void log_routing_decision(
    std::uint64_t message_id,
    std::string_view study_uid,
    std::string_view dimensionality,
    std::string_view target_topic
) noexcept;

// Logs a routing rejection with the reason.
void log_routing_rejection(
    std::uint64_t message_id,
    std::string_view reason_code,
    std::string_view reason_detail
) noexcept;

// Logs a configuration error and the corresponding environment variable.
void log_config_error(
    std::string_view error_code,
    std::string_view variable_name
) noexcept;

// Logs a DICOM read failure.
void log_dicom_error(
    std::uint64_t message_id,
    std::string_view error_code,
    std::string_view path
) noexcept;

// Logs a Kafka publishing failure.
void log_kafka_error(
    std::uint64_t message_id,
    std::string_view error_code,
    std::string_view topic
) noexcept;

} // namespace neurometrix::router::infrastructure::logging
