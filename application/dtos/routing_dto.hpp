#pragma once

// ============================================================================
// Routing DTOs - Application Boundary
// ============================================================================
// Data Transfer Objects used at the boundary between the infrastructure
// layer (which speaks Protobuf over Kafka) and the application layer
// (which speaks pure C++ types).
//
// The Kafka adapter is responsible for:
//   1. Deserializing the incoming Protobuf message into RoutingRequestDto.
//   2. Calling the use case with this DTO.
//   3. Serializing the returned DTO back into Protobuf.
//
// These DTOs are intentionally plain aggregates: no constructors, no
// validation, no business logic. All validation and business rules live in
// the domain and application layers.
//
// Design principles:
//   - No mention of Protobuf, Kafka, or any transport.
//   - No business logic. Only data.
//   - Only standard library types.
// ============================================================================

#include <cstdint>
#include <string>

namespace neurometrix::router::application::dtos {

// ---------------------------------------------------------------------------
// Input DTO
// ---------------------------------------------------------------------------
// Built by the Kafka consumer adapter from an incoming RoutingRequest
// Protobuf message.
// ---------------------------------------------------------------------------
struct RoutingRequestDto {
    std::uint64_t message_id;         // Immutable, assigned by the API Gateway
    std::string   dicom_path;         // Reference to the DICOM study (S3 URL or path)
    std::uint64_t received_at_utc;    // Unix milliseconds when the message arrived
};

// ---------------------------------------------------------------------------
// Output DTOs
// ---------------------------------------------------------------------------
// Built by the application layer and serialized by the Kafka producer
// adapter into outgoing Protobuf messages.
// ---------------------------------------------------------------------------

// Successful routing decision.
// Serialized to RoutingResponse protobuf and published to the appropriate
// success topic (mri.requests.2d, mri.requests.3d, or mri.requests.4d).
struct RoutingDecisionDto {
    std::uint64_t message_id;
    std::string   study_instance_uid;   // Canonical DICOM UID string
    std::string   dimensionality_suffix;// "2d", "3d", or "4d"
    std::uint64_t occurred_at_utc;      // Unix milliseconds
};

// Routing rejection.
// Serialized to RoutingRejection protobuf and published to a dedicated
// rejection topic (mri.requests.rejected) for observability and alerting.
struct RoutingRejectionDto {
    std::uint64_t message_id;
    std::string   reason_code;          // Stable machine-readable code
    std::string   reason_detail;        // Human-readable explanation
    std::uint64_t occurred_at_utc;      // Unix milliseconds
};

} // namespace neurometrix::router::application::dtos