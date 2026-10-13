// ============================================================================
// Logger - Implementation
// ============================================================================
// See logger.hpp for the contract.
// ============================================================================

#include "logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <mutex>
#include <string>
#include <utility>

namespace neurometrix::router::infrastructure::logging {

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

namespace {

std::mutex g_init_mutex;

// ---------------------------------------------------------------------------
// JSON escaping
// ---------------------------------------------------------------------------
// Escapes a string so that it can be safely embedded in a JSON value
// without breaking the structure. Handles the mandatory escape sequences
// defined by RFC 8259:
//   \  -> \\
//   "  -> \"
//   \b -> \b
//   \f -> \f
//   \n -> \n
//   \r -> \r
//   \t -> \t
// Control characters < 0x20 are emitted as \u00XX.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string json_escape(std::string_view input) {
    std::string output;
    output.reserve(input.size() + 2);

    for (char c : input) {
        const auto uc = static_cast<unsigned char>(c);
        switch (c) {
            case '\\': output += "\\\\"; break;
            case '"':  output += "\\\""; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (uc < 0x20) {
                    output += std::format("\\u{:04x}", static_cast<unsigned>(uc));
                } else {
                    output += c;
                }
        }
    }
    return output;
}

// ---------------------------------------------------------------------------
// ISO 8601 timestamp with millisecond precision in UTC.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string iso8601_utc_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &tt);
#else
    gmtime_r(&tt, &tm_utc);
#endif
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    return std::format("{}.{:03d}Z", buffer.data(), static_cast<int>(ms.count()));
}

// Maps a string level name to spdlog's level enum.
[[nodiscard]] spdlog::level::level_enum parse_level(std::string_view name) noexcept {
    if (name == "trace")    return spdlog::level::trace;
    if (name == "debug")    return spdlog::level::debug;
    if (name == "info")     return spdlog::level::info;
    if (name == "warn")     return spdlog::level::warn;
    if (name == "error")    return spdlog::level::err;
    if (name == "critical") return spdlog::level::critical;
    if (name == "off")      return spdlog::level::off;
    return spdlog::level::info;
}

// ---------------------------------------------------------------------------
// Emits a fully-formed JSON line through spdlog.
//
// spdlog is used only for its sink, thread-safety, and level filtering.
// The message content is built manually so we control the exact JSON shape.
// ---------------------------------------------------------------------------
void emit(std::string_view level,
          std::string_view message,
          std::string_view extra_fields) noexcept {
    try {
        auto logger = Logger::get();
        if (!logger) return;

        std::string line;
        line.reserve(256 + message.size() + extra_fields.size());
        line += R"({"timestamp":")";
        line += iso8601_utc_now();
        line += R"(","level":")";
        line += level;
        line += R"(","service":"router","message":")";
        line += json_escape(message);
        line += R"(")";
        if (!extra_fields.empty()) {
            line += ",";
            line += extra_fields;
        }
        line += "}";

        const auto lvl = parse_level(level);
        logger->log(lvl, "{}", line);
    } catch (...) {
        // Logging must never throw. Swallow all errors.
    }
}

[[nodiscard]] std::string field(std::string_view name, std::string_view value) {
    return std::format(R"("{}":"{}")", name, json_escape(value));
}

[[nodiscard]] std::string field(std::string_view name, std::uint64_t value) {
    return std::format(R"("{}":{})", name, value);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Logger::initialize
// ---------------------------------------------------------------------------
void Logger::initialize(std::string_view service_name,
                        std::string_view level) noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    try {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        // Pattern %v prints the raw message we pass to log(); the JSON is
        // built manually in emit(). Adding a newline makes each log line
        // a self-contained JSON object.
        sink->set_pattern("%v");

        logger_ = std::make_shared<spdlog::logger>(
            std::string{service_name}, std::move(sink));
        logger_->set_level(parse_level(level));
        logger_->flush_on(spdlog::level::err);

        spdlog::register_logger(logger_);
    } catch (...) {
        logger_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Logger::get
// ---------------------------------------------------------------------------
std::shared_ptr<spdlog::logger> Logger::get() noexcept {
    return logger_;
}

// ---------------------------------------------------------------------------
// Logger::shutdown
// ---------------------------------------------------------------------------
void Logger::shutdown() noexcept {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    try {
        if (logger_) {
            logger_->flush();
        }
        spdlog::shutdown();
        logger_.reset();
    } catch (...) {
        // Swallow all errors on shutdown.
    }
}

// ---------------------------------------------------------------------------
// Structured log helpers
// ---------------------------------------------------------------------------

void log_routing_decision(
    std::uint64_t message_id,
    std::string_view study_uid,
    std::string_view dimensionality,
    std::string_view target_topic
) noexcept {
    const std::string extra = std::format(
        "{},{},{},{}",
        field("message_id", message_id),
        field("study_uid", study_uid),
        field("dimensionality", dimensionality),
        field("target_topic", target_topic));
    emit("info", "Routing decision made", extra);
}

void log_routing_rejection(
    std::uint64_t message_id,
    std::string_view reason_code,
    std::string_view reason_detail
) noexcept {
    const std::string extra = std::format(
        "{},{},{}",
        field("message_id", message_id),
        field("reason_code", reason_code),
        field("reason_detail", reason_detail));
    emit("warn", "Routing rejected", extra);
}

void log_config_error(
    std::string_view error_code,
    std::string_view variable_name
) noexcept {
    const std::string extra = std::format(
        "{},{}",
        field("error_code", error_code),
        field("variable_name", variable_name));
    emit("critical", "Configuration error", extra);
}

void log_dicom_error(
    std::uint64_t message_id,
    std::string_view error_code,
    std::string_view path
) noexcept {
    const std::string extra = std::format(
        "{},{},{}",
        field("message_id", message_id),
        field("error_code", error_code),
        field("path", path));
    emit("error", "DICOM read failure", extra);
}

void log_kafka_error(
    std::uint64_t message_id,
    std::string_view error_code,
    std::string_view topic
) noexcept {
    const std::string extra = std::format(
        "{},{},{}",
        field("message_id", message_id),
        field("error_code", error_code),
        field("topic", topic));
    emit("error", "Kafka publishing failure", extra);
}

} // namespace neurometrix::router::infrastructure::logging