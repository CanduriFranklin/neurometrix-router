#pragma once

// ============================================================================
// Routing Events - Domain Layer
// ============================================================================
// Domain events represent meaningful facts that happened inside the domain.
// They are immutable, past-tense, and carry only domain data.
//
// They do NOT know about Kafka, Protobuf, JSON, or any transport.
// The infrastructure layer is responsible for translating these events
// into wire-format messages.
// ============================================================================

#include "../value_objects/dimensionality.hpp"
#include "../value_objects/study_uid.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace neurometrix::router::domain {

// ---------------------------------------------------------------------------
// RoutingDecisionMade
// ---------------------------------------------------------------------------
// Emitted when the router has successfully classified an incoming study
// and decided the target core (2D, 3D, or 4D).
//
// This event is the authoritative record of the routing decision and is
// the trigger for the infrastructure to publish the RoutingRequest to
// the appropriate Kafka topic.
// ---------------------------------------------------------------------------
class RoutingDecisionMade final {
public:
    RoutingDecisionMade(
        std::uint64_t message_id,
        StudyUid study_uid,
        Dimensionality dimensionality,
        std::string target_topic_suffix,
        std::string dicom_path,
        std::uint64_t occurred_at_utc
    ) noexcept
        : message_id_(message_id),
          study_uid_(std::move(study_uid)),
          dimensionality_(dimensionality),
          target_topic_suffix_(std::move(target_topic_suffix)),
          dicom_path_(std::move(dicom_path)),
          occurred_at_utc_(occurred_at_utc)
    {}

    [[nodiscard]] std::uint64_t message_id() const noexcept { return message_id_; }
    [[nodiscard]] const StudyUid& study_uid() const noexcept { return study_uid_; }
    [[nodiscard]] Dimensionality dimensionality() const noexcept { return dimensionality_; }
    [[nodiscard]] const std::string& target_topic_suffix() const noexcept {
        return target_topic_suffix_;
    }
    [[nodiscard]] const std::string& dicom_path() const noexcept {
        return dicom_path_;
    }
    [[nodiscard]] std::uint64_t occurred_at_utc() const noexcept { return occurred_at_utc_; }

    // Stable event name, useful for logs, metrics, and event store keys.
    [[nodiscard]] static constexpr std::string_view event_name() noexcept {
        return "RoutingDecisionMade";
    }

private:
    std::uint64_t   message_id_;
    StudyUid        study_uid_;
    Dimensionality  dimensionality_;
    std::string     target_topic_suffix_;   // "2d", "3d", or "4d"
    std::string     dicom_path_;            // source path or object-store URI
    std::uint64_t   occurred_at_utc_;       // Unix milliseconds
};

// ---------------------------------------------------------------------------
// RoutingRejected
// ---------------------------------------------------------------------------
// Emitted when the router refuses to route a study, for example because
// the metadata was inconsistent or the dimensionality could not be
// resolved.
//
// This event is the authoritative record of the rejection and allows
// downstream systems to observe and alert on rejected studies.
// ---------------------------------------------------------------------------
class RoutingRejected final {
public:
    RoutingRejected(
        std::uint64_t message_id,
        std::string reason,
        std::uint64_t occurred_at_utc
    ) noexcept
        : message_id_(message_id),
          reason_(std::move(reason)),
          occurred_at_utc_(occurred_at_utc)
    {}

    [[nodiscard]] std::uint64_t message_id() const noexcept { return message_id_; }
    [[nodiscard]] const std::string& reason() const noexcept { return reason_; }
    [[nodiscard]] std::uint64_t occurred_at_utc() const noexcept { return occurred_at_utc_; }

    [[nodiscard]] static constexpr std::string_view event_name() noexcept {
        return "RoutingRejected";
    }

private:
    std::uint64_t  message_id_;
    std::string    reason_;
    std::uint64_t  occurred_at_utc_;
};

} // namespace neurometrix::router::domain