#pragma once

// ============================================================================
// IRoutingRepository - Domain Port
// ============================================================================
// Port (in the Hexagonal Architecture sense) that defines the persistence
// contract required by the router domain.
//
// This interface lives in the domain layer and is implemented by an
// infrastructure adapter (PostgreSQL in our case). The domain never
// depends on the adapter. The adapter depends on the domain.
//
// Design principles:
//   - Returns std::expected for all recoverable errors (no exceptions).
//   - Uses domain types only (StudyUid, RoutingRequest). No DTOs.
//   - No mention of SQL, PostgreSQL, Kafka, or any infrastructure concern.
//   - Methods are const and noexcept where they do not allocate.
// ============================================================================

#include "../entities/routing_request.hpp"
#include "../value_objects/study_uid.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace neurometrix::router::domain {

// Errors reported by the repository port.
// The adapter maps low-level errors (SQL state, connection loss, etc.)
// to these domain-level error codes.
enum class RepositoryError : std::uint8_t {
    NotFound,
    DuplicateKey,
    ConnectionLost,
    SerializationFailure,
    UnknownError,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(RepositoryError e) noexcept {
    switch (e) {
        case RepositoryError::NotFound:             return "not_found";
        case RepositoryError::DuplicateKey:         return "duplicate_key";
        case RepositoryError::ConnectionLost:       return "connection_lost";
        case RepositoryError::SerializationFailure: return "serialization_failure";
        case RepositoryError::UnknownError:         return "unknown_error";
    }
    return "unknown";
}

// Persistence contract for routing requests.
//
// All methods are pure virtual. The adapter implementation lives in
// infrastructure/adapters/postgres/postgres_repository_adapter.hpp.
class IRoutingRepository {
public:
    virtual ~IRoutingRepository() = default;

    // Persists the routing decision for audit and traceability.
    // Returns the number of rows written, or an error.
    //
    // Contract:
    //   - The repository MUST use parameterized queries. Concatenation of
    //     SQL strings is forbidden.
    //   - The repository MUST run inside a transaction with a stable
    //     isolation level (READ COMMITTED is acceptable for our workload).
    //   - Idempotency: calling save() twice with the same message_id MUST
    //     return DuplicateKey, not silently overwrite the existing record.
    [[nodiscard]] virtual std::expected<void, RepositoryError>
    save(const RoutingRequest& request) noexcept = 0;

    // Looks up a routing request by its immutable message_id.
    // Returns std::nullopt if no record exists.
    [[nodiscard]] virtual std::expected<std::optional<RoutingRequest>, RepositoryError>
    find_by_message_id(std::uint64_t message_id) const noexcept = 0;

    // Looks up all routing requests associated with a given DICOM study.
    // Useful for audit trails and for retrying failed routings.
    [[nodiscard]] virtual std::expected<std::vector<RoutingRequest>, RepositoryError>
    find_by_study_uid(const StudyUid& study_uid) const noexcept = 0;

    // Marks a routing request as successfully published to Kafka.
    // This is a two-phase commit boundary: the row exists in "pending"
    // state after save(), and transitions to "published" after Kafka
    // acknowledges the message.
    [[nodiscard]] virtual std::expected<void, RepositoryError>
    mark_as_published(std::uint64_t message_id) noexcept = 0;

    // Soft-deletes a routing request. Used when a request is rejected
    // by downstream validation and must be retained for audit.
    [[nodiscard]] virtual std::expected<void, RepositoryError>
    mark_as_rejected(std::uint64_t message_id, std::string_view reason) noexcept = 0;
};

} // namespace neurometrix::router::domain