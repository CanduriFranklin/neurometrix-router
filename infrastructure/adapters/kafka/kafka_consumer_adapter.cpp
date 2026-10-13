// ============================================================================
// KafkaConsumerAdapter - Implementation
// ============================================================================
// See kafka_consumer_adapter.hpp for the contract.
// ============================================================================

#include "kafka_consumer_adapter.hpp"

#include "../logging/logger.hpp"

#include <librdkafka/rdkafka.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace neurometrix::router::infrastructure::kafka {

namespace {

// ---------------------------------------------------------------------------
// librdkafka RAII wrappers
// ---------------------------------------------------------------------------
struct RdKafkaConfDeleter {
    void operator()(rd_kafka_conf_t* p) const noexcept {
        if (p) rd_kafka_conf_destroy(p);
    }
};
using RdKafkaConfPtr = std::unique_ptr<rd_kafka_conf_t, RdKafkaConfDeleter>;

struct RdKafkaConsumerDeleter {
    void operator()(rd_kafka_t* p) const noexcept {
        if (p) rd_kafka_destroy(p);
    }
};
using RdKafkaConsumerPtr = std::unique_ptr<rd_kafka_t, RdKafkaConsumerDeleter>;

struct RdKafkaTopicPartitionListDeleter {
    void operator()(rd_kafka_topic_partition_list_t* p) const noexcept {
        if (p) rd_kafka_topic_partition_list_destroy(p);
    }
};
using RdKafkaTopicPartitionListPtr =
    std::unique_ptr<rd_kafka_topic_partition_list_t,
                    RdKafkaTopicPartitionListDeleter>;

// ---------------------------------------------------------------------------
// Current UTC timestamp in milliseconds.
// ---------------------------------------------------------------------------
[[nodiscard]] std::uint64_t now_utc_ms() noexcept {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Deserialization of the temporary binary format.
//
// Layout (little-endian):
//   [8 bytes] message_id
//   [8 bytes] received_at_utc (ms)
//   [N bytes] dicom_path (UTF-8)
//
// This layout is a placeholder. When the Protobuf contracts are generated,
// this function will be replaced by a Protobuf parser. The consumer logic
// (poll, commit, invoke use case) does not change.
// ---------------------------------------------------------------------------
struct DeserializedRequest {
    std::uint64_t message_id;
    std::uint64_t received_at_utc;
    std::string   dicom_path;
};

[[nodiscard]] bool
deserialize_request(const char* data, std::size_t size,
                    DeserializedRequest& out) noexcept {
    try {
        constexpr std::size_t kHeaderSize = 16;
        if (data == nullptr || size < kHeaderSize) {
            return false;
        }

        std::uint64_t message_id = 0;
        std::uint64_t received_at = 0;
        std::memcpy(&message_id, data, sizeof(message_id));
        std::memcpy(&received_at, data + sizeof(message_id), sizeof(received_at));

        out.message_id       = message_id;
        out.received_at_utc  = received_at;
        out.dicom_path.assign(data + kHeaderSize, size - kHeaderSize);
        return true;
    } catch (...) {
        return false;
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// KafkaConsumerAdapter::Impl
// ---------------------------------------------------------------------------
struct KafkaConsumerAdapter::Impl {
    RdKafkaConsumerPtr consumer{nullptr};
    std::string        topic_requests;
    std::string        consumer_group;
    std::uint32_t      timeout_ms{5000};
    std::atomic<bool>  stop_requested{false};
    application::IRouteRequestUseCase* use_case{nullptr};
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
KafkaConsumerAdapter::KafkaConsumerAdapter(
    const config::KafkaConfig&               config,
    const security::TLSContext&              tls,
    application::IRouteRequestUseCase& use_case
) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    try {
        pimpl_->topic_requests  = config.topic_requests;
        pimpl_->consumer_group  = config.consumer_group;
        pimpl_->timeout_ms      = config.timeout_ms;
        pimpl_->use_case        = &use_case;

        // Build librdkafka configuration.
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

        // Bootstrap and identity.
        set("bootstrap.servers",  config.bootstrap_servers);
        set("group.id",           config.consumer_group);
        set("client.id",          "router-consumer");

        // Security.
        set("security.protocol",  config.security_protocol);
        set("sasl.mechanism",     config.sasl_mechanism);
        set("sasl.username",      config.sasl_username);
        set("sasl.password",      config.sasl_password);
        set("ssl.ca.location",    tls.ca_path());
        set("ssl.certificate.location", tls.cert_path());
        set("ssl.key.location",   tls.key_path());

        // Consumer tuning for at-least-once semantics.
        set("enable.auto.commit",      "false");  // we commit manually
        set("auto.offset.reset",       "earliest");
        set("enable.partition.eof",    "false");
        set("session.timeout.ms",      "45000");
        set("max.poll.interval.ms",    "300000");


        rd_kafka_conf_t* raw_conf = conf.release();

        char errstr[512] = {0};
        rd_kafka_t* raw_consumer = rd_kafka_new(
            RD_KAFKA_CONSUMER, raw_conf, errstr, sizeof(errstr));

        if (!raw_consumer) {
            return;
        }

        // Redirect the main queue to the consumer queue. Required for the
        // high-level consumer API.
        rd_kafka_poll_set_consumer(raw_consumer);
        pimpl_->consumer.reset(raw_consumer);
    } catch (...) {
        // Swallow: consumer stays null.
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
KafkaConsumerAdapter::~KafkaConsumerAdapter() {
    stop();
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
bool KafkaConsumerAdapter::run() noexcept {
    if (!pimpl_ || !pimpl_->consumer || !pimpl_->use_case) {
        spdlog::error("[consumer] run() called with invalid state: pimpl={}, consumer={}, use_case={}",
                      pimpl_ != nullptr,
                      pimpl_ && pimpl_->consumer != nullptr,
                      pimpl_ && pimpl_->use_case != nullptr);
        return false;
    }

    try {
        spdlog::info("[consumer] run() started. topic={} group={}",
                     pimpl_->topic_requests, pimpl_->consumer_group);

        // ---- Step 1: subscribe to the request topic -----------------------
        RdKafkaTopicPartitionListPtr topics{rd_kafka_topic_partition_list_new(1)};
        if (!topics) {
            spdlog::error("[consumer] failed to allocate topic partition list");
            return false;
        }
        rd_kafka_topic_partition_list_add(
            topics.get(), pimpl_->topic_requests.c_str(), RD_KAFKA_PARTITION_UA);

        const rd_kafka_resp_err_t sub_err = rd_kafka_subscribe(
            pimpl_->consumer.get(), topics.get());
        if (sub_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
            spdlog::error("[consumer] subscribe failed: {}", rd_kafka_err2str(sub_err));
            return false;
        }
        spdlog::info("[consumer] subscribed to topic '{}'", pimpl_->topic_requests);

        // ---- Step 2: consumption loop -------------------------------------
        constexpr int kPollTimeoutMs = 500;
        std::uint64_t iterations = 0;
        std::uint64_t empty_polls = 0;

        while (!pimpl_->stop_requested.load(std::memory_order_acquire)) {
            ++iterations;

            rd_kafka_message_t* msg = rd_kafka_consumer_poll(
                pimpl_->consumer.get(), kPollTimeoutMs);

            if (msg == nullptr) {
                ++empty_polls;
                if (iterations % 40 == 0) {
                    spdlog::debug("[consumer] alive: {} iterations, {} empty polls",
                                  iterations, empty_polls);
                }
                continue;
            }

            spdlog::info("[consumer] message received: partition={} offset={} len={} err={}",
                         msg->partition, msg->offset, msg->len,
                         static_cast<int>(msg->err));

            if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                if (msg->err == RD_KAFKA_RESP_ERR__PARTITION_EOF ||
                    msg->err == RD_KAFKA_RESP_ERR__TIMED_OUT) {
                    rd_kafka_message_destroy(msg);
                    continue;
                }
                spdlog::error("[consumer] fatal broker error: {}",
                              rd_kafka_err2str(msg->err));
                rd_kafka_message_destroy(msg);
                return false;
            }

            // ---- Step 3: deserialize --------------------------------------
            DeserializedRequest request{};
            const bool ok = deserialize_request(
                static_cast<const char*>(msg->payload), msg->len, request);

            if (!ok) {
                spdlog::warn("[consumer] deserialize failed: len={} (expected >= 16)",
                             msg->len);
                (void)rd_kafka_commit_message(pimpl_->consumer.get(), msg, 0);
                rd_kafka_message_destroy(msg);
                continue;
            }

            spdlog::info("[consumer] deserialized OK: id={} ts={} path='{}'",
                         request.message_id, request.received_at_utc,
                         request.dicom_path);

            // ---- Step 4: build use case input -----------------------------
            application::RouteRequestInput input{};
            input.message_id      = request.message_id;
            input.dicom_path      = std::move(request.dicom_path);
            input.received_at_utc = request.received_at_utc;

            spdlog::info("[consumer] invoking use case for id={}",
                         input.message_id);

            // ---- Step 5: invoke use case ----------------------------------
            auto result = pimpl_->use_case->execute(input);

            if (!result) {
                const auto err_str = std::string(application::to_string(result.error()));
                spdlog::error("[consumer] use case FAILED for id={}: error={}",
                              input.message_id, err_str);
                rd_kafka_message_destroy(msg);
                continue;
            }

            spdlog::info("[consumer] use case OK for id={}: accepted={}",
                         input.message_id, result->accepted);

            // ---- Step 6: commit offset ------------------------------------
            const rd_kafka_resp_err_t commit_err = rd_kafka_commit_message(
                pimpl_->consumer.get(), msg, 0);

            if (commit_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                spdlog::error("[consumer] commit failed: {}",
                              rd_kafka_err2str(commit_err));
            } else {
                spdlog::info("[consumer] committed offset {} (partition {})",
                             msg->offset, msg->partition);
            }

            rd_kafka_message_destroy(msg);
        }

        spdlog::info("[consumer] loop exited. iterations={}", iterations);
        (void)rd_kafka_consumer_close(pimpl_->consumer.get());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[consumer] exception: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("[consumer] unknown exception");
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void KafkaConsumerAdapter::stop() noexcept {
    if (!pimpl_) {
        return;
    }
    pimpl_->stop_requested.store(true, std::memory_order_release);
}

} // namespace neurometrix::router::infrastructure::kafka
