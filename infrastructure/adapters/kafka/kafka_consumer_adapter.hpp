#pragma once

// ============================================================================
// KafkaConsumerAdapter - Infrastructure Inbound Adapter
// ============================================================================
// Implementation of the inbound edge that reads routing requests from Kafka
// and dispatches them to the application use case IRouteRequestUseCase.
//
// This is the entry point of the entire router. Without it, no message
// arrives from upstream.
//
// Responsibilities:
//   1. Subscribe to the configured request topic with the configured
//      consumer group.
//   2. Read messages one at a time.
//   3. Deserialize each message into RouteRequestInput.
//   4. Invoke the use case.
//   5. Commit the offset AFTER successful processing (at-least-once).
//   6. Log every outcome and export Prometheus metrics.
//
// Design principles:
//   - At-least-once delivery. Offsets are committed manually, after the
//     use case returns. If the process crashes mid-processing, Kafka
//     redelivers the message.
//   - Idempotency is delegated to the use case, which the repository
//     enforces via the message_id uniqueness constraint.
//   - Graceful shutdown: responds to stop() by flushing within a bounded
//     timeout.
//   - No exceptions. All errors are logged and the loop continues.
//   - No Protobuf in this header. Serialization is an internal detail of
//     the .cpp file.
// ============================================================================

#include "../../../application/ports/inbound/i_route_request_use_case.hpp"
#include "../config/config_loader.hpp"
#include "../security/tls_context.hpp"

#include <atomic>
#include <memory>

namespace neurometrix::router::infrastructure::kafka {

// Concrete adapter that consumes routing requests from Kafka and forwards
// them to the application use case.
//
// Lifecycle:
//   - Construct with the Kafka configuration, the TLS context, and the
//     use case to invoke.
//   - Call start() once at application startup. It blocks until stop()
//     is called from another thread or a signal handler.
//   - Call stop() from a signal handler or a shutdown thread.
//
// The adapter is not copyable and not movable because it owns a
// librdkafka consumer handle and a running thread.
class KafkaConsumerAdapter final {
public:
    // Constructs the adapter. Does not connect to Kafka yet.
    //
    // Parameters:
    //   config   : Kafka configuration (bootstrap servers, SASL, TLS paths).
    //   tls      : TLS context. Must have been validated at startup.
    //   use_case : Reference to the use case that will process each message.
    //              The use case must outlive this adapter.
    KafkaConsumerAdapter(
        const config::KafkaConfig&                    config,
        const security::TLSContext&                   tls,
        application::IRouteRequestUseCase&      use_case
    ) noexcept;

    ~KafkaConsumerAdapter();

    KafkaConsumerAdapter(const KafkaConsumerAdapter&) = delete;
    KafkaConsumerAdapter& operator=(const KafkaConsumerAdapter&) = delete;
    KafkaConsumerAdapter(KafkaConsumerAdapter&&) = delete;
    KafkaConsumerAdapter& operator=(KafkaConsumerAdapter&&) = delete;

    // Connects to the Kafka cluster, subscribes to the request topic, and
    // enters the consumption loop.
    //
    // This method blocks until stop() is called from another thread, or
    // until the broker becomes unreachable and all reconnect attempts
    // have been exhausted.
    //
    // Returns true if the loop terminated cleanly (via stop()).
    // Returns false if the consumer could not be started or terminated
    // due to an unrecoverable error.
    [[nodiscard]] bool run() noexcept;

    // Requests a graceful shutdown. Safe to call from any thread, including
    // a signal handler (via std::atomic and rd_kafka_consumer_close).
    // Idempotent.
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::kafka