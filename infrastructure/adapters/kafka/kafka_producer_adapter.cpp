// ============================================================================
// KafkaProducerAdapter - Implementation
// ============================================================================
// See kafka_producer_adapter.hpp for the contract.
// ============================================================================

#include "kafka_producer_adapter.hpp"

#include "../../../domain/events/routing_event.hpp"
#include "../logging/logger.hpp"

#include <librdkafka/rdkafka.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neurometrix::router::infrastructure::kafka {

// ---------------------------------------------------------------------------
// librdkafka RAII wrappers
// ---------------------------------------------------------------------------
namespace {

struct RdKafkaConfDeleter {
    void operator()(rd_kafka_conf_t* p) const noexcept {
        if (p) rd_kafka_conf_destroy(p);
    }
};
using RdKafkaConfPtr = std::unique_ptr<rd_kafka_conf_t, RdKafkaConfDeleter>;

struct RdKafkaProducerDeleter {
    void operator()(rd_kafka_t* p) const noexcept {
        if (p) rd_kafka_destroy(p);
    }
};
using RdKafkaProducerPtr = std::unique_ptr<rd_kafka_t, RdKafkaProducerDeleter>;

struct RdKafkaTopicDeleter {
    void operator()(rd_kafka_topic_t* p) const noexcept {
        if (p) rd_kafka_topic_destroy(p);
    }
};
using RdKafkaTopicPtr = std::unique_ptr<rd_kafka_topic_t, RdKafkaTopicDeleter>;

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------
// Composes the destination topic for a routed study.
//
// Fixed convention: core-<dim>.requests
// where <dim> is "2d", "3d", or "4d".
//
// This template is independent from the input topic (KAFKA_TOPIC_REQUESTS).
// Adding a new dimensionality means creating a new topic
// "core-<dim>.requests" and extending the Dimensionality enum. No change
// is required here.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string compose_topic(std::string_view suffix) {
    std::string out;
    out.reserve(5 + suffix.size() + 9);  // "core-" + suffix + ".requests"
    out += "core-";
    out += suffix;
    out += ".requests";
    return out;
}

[[nodiscard]] std::string compose_rejection_topic(const std::string& base) {
    // Convention: <base>.rejected
    std::string out;
    out.reserve(base.size() + 9);
    out += base;
    out += ".rejected";
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// KafkaProducerAdapter::Impl
// ---------------------------------------------------------------------------
struct KafkaProducerAdapter::Impl {
    RdKafkaProducerPtr producer{nullptr};
    std::string        topic_base;
    std::uint32_t      timeout_ms{5000};
    bool               started{false};
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
KafkaProducerAdapter::KafkaProducerAdapter(
    const config::KafkaConfig& config,
    const security::TLSContext& tls
) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    try {
        // Store the timeout for later use in publish calls.
        pimpl_->timeout_ms = config.timeout_ms;
        pimpl_->topic_base = config.topic_requests;

        // Build the librdkafka configuration.
        RdKafkaConfPtr conf{rd_kafka_conf_new()};
        if (!conf) {
            return;
        }

        auto set = [&](const char* key, const std::string& value) -> bool {
            char errstr[512] = {0};
            return rd_kafka_conf_set(
                conf.get(), key, value.c_str(), errstr, sizeof(errstr))
                == RD_KAFKA_CONF_OK;
        };

        // Bootstrap and security.
        set("bootstrap.servers", config.bootstrap_servers);
        set("security.protocol", config.security_protocol);
        set("sasl.mechanism",    config.sasl_mechanism);
        set("sasl.username",     config.sasl_username);
        set("sasl.password",     config.sasl_password);

        // mTLS certificates from the validated TLSContext.
        set("ssl.ca.location",   tls.ca_path());
        set("ssl.certificate.location", tls.cert_path());
        set("ssl.key.location",  tls.key_path());

        // Producer tuning for mission-critical workloads.
        set("acks",              "all");        // full ISR acknowledgment
        set("enable.idempotence","true");       // no duplicates on retry
        set("compression.type",  "zstd");       // Zstandard compression
        set("message.timeout.ms", std::to_string(pimpl_->timeout_ms));

        // Detach the conf from its RAII wrapper because rd_kafka_new takes
        // ownership of it on success.
        rd_kafka_conf_t* raw_conf = conf.release();

        char errstr[512] = {0};
        rd_kafka_t* raw_producer = rd_kafka_new(
            RD_KAFKA_PRODUCER, raw_conf, errstr, sizeof(errstr));

        if (!raw_producer) {
            // rd_kafka_new destroys raw_conf on failure.
            return;
        }
        pimpl_->producer.reset(raw_producer);
    } catch (...) {
        // Swallow: producer stays null.
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
KafkaProducerAdapter::~KafkaProducerAdapter() {
    stop();
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool KafkaProducerAdapter::start() noexcept {
    if (!pimpl_ || !pimpl_->producer) {
        return false;
    }
    try {
        // Verify the broker is reachable by querying metadata.
        const rd_kafka_metadata_t* metadata = nullptr;
        const rd_kafka_resp_err_t err = rd_kafka_metadata(
            pimpl_->producer.get(), 0, nullptr, &metadata,
            static_cast<int>(pimpl_->timeout_ms));

        if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            return false;
        }
        if (metadata) {
            rd_kafka_metadata_destroy(metadata);
        }
        pimpl_->started = true;
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void KafkaProducerAdapter::stop() noexcept {
    if (!pimpl_ || !pimpl_->producer) {
        return;
    }
    try {
        // Flush pending messages with a bounded timeout, then destroy.
        (void)rd_kafka_flush(
            pimpl_->producer.get(),
            static_cast<int>(pimpl_->timeout_ms));
        pimpl_->producer.reset();
        pimpl_->started = false;
    } catch (...) {
        // Swallow: shutdown must not throw.
    }
}

// ---------------------------------------------------------------------------
// publish_routing_decision
// ---------------------------------------------------------------------------
std::expected<void, application::PublishError>
KafkaProducerAdapter::publish_routing_decision(
    const domain::RoutingDecisionMade& event) noexcept
{
    if (!pimpl_ || !pimpl_->producer || !pimpl_->started) {
        return std::unexpected(application::PublishError::BrokerUnavailable);
    }

    try {
        // ---- Step 1: compose the destination topic -------------------------
        const std::string topic_str =
            compose_topic(event.target_topic_suffix());

        // ---- Step 2: serialize the event -----------------------------------
        // NOTE: this is a temporary binary format. It will be replaced by
        // Protobuf serialization once the .proto contracts are generated.
        // The port and the use case remain unaffected.
        // Binary wire format (Little-Endian):
        //   [8]  message_id
        //   [8]  timestamp_utc (Unix ms)
        //   [4]  study_uid length
        //   [N]  study_uid (UTF-8)
        //   [4]  dicom_path length
        //   [M]  dicom_path (UTF-8)
        std::string payload;
        {
            const std::uint64_t mid = event.message_id();
            const std::uint64_t ts  = event.occurred_at_utc();
            const auto& uid   = event.study_uid().value();
            const auto& path  = event.dicom_path();

            const std::uint32_t uid_len  = static_cast<std::uint32_t>(uid.size());
            const std::uint32_t path_len = static_cast<std::uint32_t>(path.size());

            payload.reserve(8 + 8 + 4 + uid_len + 4 + path_len);
            payload.append(reinterpret_cast<const char*>(&mid),      sizeof(mid));
            payload.append(reinterpret_cast<const char*>(&ts),       sizeof(ts));
            payload.append(reinterpret_cast<const char*>(&uid_len),  sizeof(uid_len));
            payload.append(uid);
            payload.append(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
            payload.append(path);
        }

        // ---- Step 3: key the message by message_id -------------------------
        // Keying by message_id ensures that all messages with the same
        // identifier land on the same partition, preserving order.
        const std::string key = std::to_string(event.message_id());

        // ---- Step 4: publish ------------------------------------------------
        char errstr[512] = {0};
        const rd_kafka_resp_err_t err = rd_kafka_producev(
            pimpl_->producer.get(),
            RD_KAFKA_V_TOPIC(topic_str.c_str()),
            RD_KAFKA_V_KEY(key.data(), key.size()),
            RD_KAFKA_V_VALUE(payload.data(), payload.size()),
            RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
            RD_KAFKA_V_END);

        if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            logging::log_kafka_error(event.message_id(),
                                     rd_kafka_err2str(err),
                                     topic_str);
            return std::unexpected(application::PublishError::UnknownError);
        }

        // ---- Step 5: flush with bounded timeout ----------------------------
        const rd_kafka_resp_err_t flush_err = rd_kafka_flush(
            pimpl_->producer.get(),
            static_cast<int>(pimpl_->timeout_ms));

        if (flush_err == RD_KAFKA_RESP_ERR__TIMED_OUT) {
            return std::unexpected(application::PublishError::Timeout);
        }
        if (flush_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            return std::unexpected(application::PublishError::UnknownError);
        }

        // ---- Step 6: structured log ----------------------------------------
        logging::log_routing_decision(
            event.message_id(),
            event.study_uid().value(),
            std::string(domain::to_string(event.dimensionality())),
            topic_str);

        return {};
    } catch (...) {
        return std::unexpected(application::PublishError::UnknownError);
    }
}

// ---------------------------------------------------------------------------
// publish_routing_rejection
// ---------------------------------------------------------------------------
std::expected<void, application::PublishError>
KafkaProducerAdapter::publish_routing_rejection(
    const domain::RoutingRejected& event) noexcept
{
    if (!pimpl_ || !pimpl_->producer || !pimpl_->started) {
        return std::unexpected(application::PublishError::BrokerUnavailable);
    }

    try {
        const std::string topic_str = compose_rejection_topic(pimpl_->topic_base);

        std::string payload;
        {
            std::uint64_t mid = event.message_id();
            std::uint64_t ts  = event.occurred_at_utc();
            const auto& reason = event.reason();

            payload.reserve(16 + reason.size());
            payload.append(reinterpret_cast<const char*>(&mid), sizeof(mid));
            payload.append(reinterpret_cast<const char*>(&ts),  sizeof(ts));
            payload.append(reason);
        }

        const std::string key = std::to_string(event.message_id());

        const rd_kafka_resp_err_t err = rd_kafka_producev(
            pimpl_->producer.get(),
            RD_KAFKA_V_TOPIC(topic_str.c_str()),
            RD_KAFKA_V_KEY(key.data(), key.size()),
            RD_KAFKA_V_VALUE(payload.data(), payload.size()),
            RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
            RD_KAFKA_V_END);

        if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            logging::log_kafka_error(event.message_id(),
                                     rd_kafka_err2str(err),
                                     topic_str);
            return std::unexpected(application::PublishError::UnknownError);
        }

        const rd_kafka_resp_err_t flush_err = rd_kafka_flush(
            pimpl_->producer.get(),
            static_cast<int>(pimpl_->timeout_ms));

        if (flush_err == RD_KAFKA_RESP_ERR__TIMED_OUT) {
            return std::unexpected(application::PublishError::Timeout);
        }
        if (flush_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            return std::unexpected(application::PublishError::UnknownError);
        }

        logging::log_routing_rejection(
            event.message_id(),
            "routing_rejected",
            event.reason());

        return {};
    } catch (...) {
        return std::unexpected(application::PublishError::UnknownError);
    }
}

} // namespace neurometrix::router::infrastructure::kafka
