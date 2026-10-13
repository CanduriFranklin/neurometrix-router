#pragma once

// ============================================================================
// IMessagePublisher - Application Outbound Port
// ============================================================================
// Port that defines the contract for publishing domain events to a
// message broker.
//
// The application layer depends on this interface, never on Kafka directly.
// The infrastructure layer provides an adapter (KafkaProducerAdapter) that
// translates domain events into wire-format messages and publishes them.
//
// Design principles:
//   - Pure application-layer vocabulary. No mention of Kafka, Protobuf,
//     topics, partitions, or consumer groups.
//   - Returns std::expected for recoverable errors.
//   - No exceptions. All failures are explicit.
//   - Templated on the event type so the adapter knows the wire schema.
// ============================================================================

#include "../../../domain/events/routing_event.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace neurometrix::router::application {

// Errors reported by the publisher port.
enum class PublishError : std::uint8_t {
    BrokerUnavailable,
    SerializationFailure,
    TopicNotFound,
    AuthorizationFailure,
    Timeout,
    UnknownError,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(PublishError e) noexcept {
    switch (e) {
        case PublishError::BrokerUnavailable:    return "broker_unavailable";
        case PublishError::SerializationFailure: return "serialization_failure";
        case PublishError::TopicNotFound:        return "topic_not_found";
        case PublishError::AuthorizationFailure: return "authorization_failure";
        case PublishError::Timeout:              return "timeout";
        case PublishError::UnknownError:         return "unknown_error";
    }
    return "unknown";
}

// Port for publishing domain events to a broker.
//
// Contract:
//   - publish() MUST be idempotent with respect to the event's message_id.
//     If the broker receives the same message_id twice, downstream consumers
//     must be able to detect the duplicate.
//   - publish() MUST NOT block indefinitely. The adapter enforces a
//     configurable timeout (default: 5 seconds).
//   - publish() MUST use TLS with mutual authentication in production.
class IMessagePublisher {
public:
    virtual ~IMessagePublisher() = default;

    // Publishes a successful routing decision to the broker.
    // The adapter maps the event to a Protobuf message and publishes it
    // to the topic derived from event.target_topic_suffix().
    [[nodiscard]] virtual std::expected<void, PublishError>
    publish_routing_decision(const domain::RoutingDecisionMade& event) noexcept = 0;

    // Publishes a routing rejection to the broker.
    // Rejections are published to a dedicated topic for observability
    // and alerting, separate from the success path.
    [[nodiscard]] virtual std::expected<void, PublishError>
    publish_routing_rejection(const domain::RoutingRejected& event) noexcept = 0;
};

} // namespace neurometrix::router::application
