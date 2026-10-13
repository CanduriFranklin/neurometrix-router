#pragma once

// ============================================================================
// KafkaProducerAdapter - Infrastructure Outbound Adapter
// ============================================================================
// Implementation of the application outbound port IMessagePublisher on top
// of Apache Kafka (via librdkafka).
//
// This is the only place in the router that knows about Kafka, librdkafka,
// topics, partitions, and SASL_SSL. The application and domain layers
// remain Kafka-agnostic.
//
// Responsibilities:
//   1. Initialize a librdkafka producer with the config provided by
//      KafkaConfig and the certificates provided by TLSContext.
//   2. Publish RoutingDecisionMade events to the success topic.
//   3. Publish RoutingRejected events to a dedicated rejection topic.
//   4. Ensure delivery with a bounded timeout.
//
// Design principles:
//   - Implements IMessagePublisher exactly.
//   - Single producer instance, reused for all publishes. librdkafka
//     producers are internally thread-safe and pool connections.
//   - No exceptions. All errors returned as PublishError.
//   - RAII for the librdkafka producer handle.
//   - mTLS enforced. The certificates are never optional.
// ============================================================================

#include "../../../application/ports/outbound/i_message_publisher.hpp"
#include "../config/config_loader.hpp"
#include "../security/tls_context.hpp"

#include <memory>
#include <string>

namespace neurometrix::router::infrastructure::kafka {

// Concrete adapter that publishes domain events to Kafka.
//
// Lifecycle:
//   - Construct with the Kafka configuration and the TLS context.
//   - Call start() once at application startup.
//   - Call publish_*() from the application use case.
//   - Call stop() during graceful shutdown.
//
// The adapter is not copyable and not movable because it owns a librdkafka
// producer handle that must not be duplicated.
class KafkaProducerAdapter final
    : public application::IMessagePublisher {
public:
    // Constructs the adapter. Does not connect to Kafka yet.
    //
    // Parameters:
    //   config : Kafka configuration (bootstrap servers, SASL, TLS paths).
    //   tls    : TLS context. Must have been validated at startup.
    //
    // Constructor is noexcept: no connection is attempted here.
    KafkaProducerAdapter(
        const config::KafkaConfig& config,
        const security::TLSContext& tls
    ) noexcept;

    ~KafkaProducerAdapter() override;

    KafkaProducerAdapter(const KafkaProducerAdapter&) = delete;
    KafkaProducerAdapter& operator=(const KafkaProducerAdapter&) = delete;
    KafkaProducerAdapter(KafkaProducerAdapter&&) = delete;
    KafkaProducerAdapter& operator=(KafkaProducerAdapter&&) = delete;

    // Connects to the Kafka cluster and verifies the broker is reachable.
    // Must be called once at startup, before any publish_*() call.
    // Returns true on success. On failure, the caller must abort startup.
    [[nodiscard]] bool start() noexcept;

    // Flushes pending messages and closes the producer.
    // Safe to call more than once.
    void stop() noexcept;

    // ------------------------------------------------------------------------
    // IMessagePublisher implementation
    // ------------------------------------------------------------------------
    [[nodiscard]] std::expected<void, application::PublishError>
    publish_routing_decision(
        const domain::RoutingDecisionMade& event) noexcept override;

    [[nodiscard]] std::expected<void, application::PublishError>
    publish_routing_rejection(
        const domain::RoutingRejected& event) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::kafka
