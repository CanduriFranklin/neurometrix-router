#pragma once

// ============================================================================
// RoutingRequest Entity
// ============================================================================
// Domain entity representing an incoming MRI study that must be routed to
// the appropriate mathematical core (2D, 3D, or 4D) through Kafka.
//
// Design principles:
//   - Immutable after construction. All state is fixed at creation time.
//   - No dependency on Protobuf, Kafka, or any infrastructure concern.
//   - Uses value objects (StudyUid, Dimensionality) for strong typing.
//   - The entity is identified by its message_id (assigned by the API Gateway).
// ============================================================================

#include "../value_objects/dimensionality.hpp"
#include "../value_objects/study_uid.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neurometrix::router::domain {

// Immutable domain entity representing an MRI study to be routed.
class RoutingRequest {
public:
    // Constructs a fully validated RoutingRequest.
    //
    // Preconditions (verified by the caller before construction):
    //   - study_uid is a valid StudyUid (already validated by StudyUid::create)
    //   - dimensionality is a valid Dimensionality
    //   - slice_count is > 0
    //   - temporal_frames is >= 0 (0 means static study)
    //   - transform_matrix has exactly 16 elements (4x4 matrix)
    //
    // The constructor is intentionally explicit and takes only the fields
    // that belong to the domain. Serialization concerns (bytes payload,
    // patient hash) are handled by the application layer through DTOs.
    RoutingRequest(
        std::uint64_t message_id,
        StudyUid study_uid,
        std::string series_uid,
        Dimensionality dimensionality,
        std::uint32_t slice_count,
        std::uint32_t temporal_frames,
        std::vector<float> transform_matrix,
        std::uint64_t timestamp_utc,
        std::string patient_id_hash
    ) noexcept
        : message_id_(message_id),
          study_uid_(std::move(study_uid)),
          series_uid_(std::move(series_uid)),
          dimensionality_(dimensionality),
          slice_count_(slice_count),
          temporal_frames_(temporal_frames),
          transform_matrix_(std::move(transform_matrix)),
          timestamp_utc_(timestamp_utc),
          patient_id_hash_(std::move(patient_id_hash))
    {}

    // ------------------------------------------------------------------------
    // Accessors (all const and noexcept)
    // ------------------------------------------------------------------------
    [[nodiscard]] std::uint64_t message_id() const noexcept { return message_id_; }
    [[nodiscard]] const StudyUid& study_uid() const noexcept { return study_uid_; }
    [[nodiscard]] const std::string& series_uid() const noexcept { return series_uid_; }
    [[nodiscard]] Dimensionality dimensionality() const noexcept { return dimensionality_; }
    [[nodiscard]] std::uint32_t slice_count() const noexcept { return slice_count_; }
    [[nodiscard]] std::uint32_t temporal_frames() const noexcept { return temporal_frames_; }
    [[nodiscard]] std::span<const float> transform_matrix() const noexcept {
        return transform_matrix_;
    }
    [[nodiscard]] std::uint64_t timestamp_utc() const noexcept { return timestamp_utc_; }
    [[nodiscard]] const std::string& patient_id_hash() const noexcept {
        return patient_id_hash_;
    }

    // ------------------------------------------------------------------------
    // Domain invariants
    // ------------------------------------------------------------------------
    // Returns true if the entity respects all domain invariants.
    // Used by tests and by the application layer before persisting or
    // forwarding the entity to the infrastructure.
    [[nodiscard]] bool is_valid() const noexcept {
        if (slice_count_ == 0) return false;
        if (transform_matrix_.size() != 16) return false;
        if (patient_id_hash_.empty()) return false;
        if (dimensionality_ == Dimensionality::FourD && temporal_frames_ == 0) {
            return false; // 4D requires temporal frames
        }
        return true;
    }

    // Returns the Kafka topic suffix ("2d", "3d", "4d") for this request.
    [[nodiscard]] std::string_view topic_suffix() const noexcept {
        return to_topic_suffix(dimensionality_);
    }

private:
    std::uint64_t              message_id_;
    StudyUid                   study_uid_;
    std::string                series_uid_;
    Dimensionality             dimensionality_;
    std::uint32_t              slice_count_;
    std::uint32_t              temporal_frames_;
    std::vector<float>         transform_matrix_;   // 4x4 row-major matrix
    std::uint64_t              timestamp_utc_;
    std::string                patient_id_hash_;    // SHA-256 hash (anonymized)
};

} // namespace neurometrix::router::domain