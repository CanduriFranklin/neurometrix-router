#pragma once

// ============================================================================
// IRouteRequestUseCase - Application Inbound Port
// ============================================================================
// Port that defines the primary use case of the router microservice:
// receiving an incoming MRI study and deciding the target mathematical core
// (2D, 3D, or 4D).
//
// This interface is implemented by RouteRequestUseCase (in use_cases/) and
// is called by the Kafka consumer adapter (in infrastructure/).
//
// Design principles:
//   - Pure application-layer vocabulary. No mention of Kafka, Protobuf,
//     DICOM, or any transport.
//   - Returns std::expected for recoverable errors.
//   - All state changes are reported explicitly through RouteRequestResult.
//   - The use case is idempotent with respect to message_id.
// ============================================================================

#include "../../../domain/value_objects/dimensionality.hpp"
#include "../../../domain/value_objects/study_uid.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace neurometrix::router::application {

struct RouteRequestInput {
    std::uint64_t  message_id      = 0;
    std::string    dicom_path;
    std::uint64_t  received_at_utc = 0;
};

struct RouteRequestSuccess {
    domain::StudyUid       study_uid;
    domain::Dimensionality dimensionality;
    std::string            target_topic_suffix;
    std::uint64_t          processing_time_ms = 0;
};

struct RouteRequestRejection {
    std::string reason_code;
    std::string reason_detail;
};

// Discriminated union of possible outcomes.
// std::optional is used because StudyUid has no default constructor:
// a Value Object must always be valid.
struct RouteRequestResult {
    bool                                 accepted = false;
    std::optional<RouteRequestSuccess>   success;
    std::optional<RouteRequestRejection> rejection;
};

enum class UseCaseError : std::uint8_t {
    InputValidationFailed,
    DicomReadFailed,
    RepositoryUnavailable,
    PublisherUnavailable,
    InternalError,
};

[[nodiscard]] constexpr std::string_view to_string(UseCaseError e) noexcept {
    switch (e) {
        case UseCaseError::InputValidationFailed:  return "input_validation_failed";
        case UseCaseError::DicomReadFailed:        return "dicom_read_failed";
        case UseCaseError::RepositoryUnavailable:  return "repository_unavailable";
        case UseCaseError::PublisherUnavailable:   return "publisher_unavailable";
        case UseCaseError::InternalError:          return "internal_error";
    }
    return "unknown";
}

class IRouteRequestUseCase {
public:
    virtual ~IRouteRequestUseCase() = default;

    [[nodiscard]] virtual std::expected<RouteRequestResult, UseCaseError>
    execute(const RouteRequestInput& input) noexcept = 0;
};

} // namespace neurometrix::router::application