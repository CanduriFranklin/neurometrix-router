#pragma once

// ============================================================================
// PostgresRepositoryAdapter - Infrastructure Outbound Adapter
// ============================================================================
// Implementation of the domain port IRoutingRepository on top of
// PostgreSQL (via libpq, the official C client).
//
// Responsibilities:
//   1. Establish an mTLS connection to PostgreSQL at startup.
//   2. Persist routing requests for audit and traceability.
//   3. Support the two-phase commit pattern:
//        - save()               -> inserts the row in state 'pending'
//        - mark_as_published()  -> transitions to state 'published'
//        - mark_as_rejected()   -> transitions to state 'rejected'
//   4. Map libpq error codes to the domain's RepositoryError enum.
//
// Security principles:
//   - Parameterized queries ONLY. String concatenation of SQL is
//     forbidden and will be rejected in code review.
//   - sslmode=verify-full enforced by configuration.
//   - No PHI in the database. Only hashed patient IDs are stored.
//
// Design principles:
//   - Implements IRoutingRepository exactly.
//   - No exceptions. All errors returned as std::expected.
//   - RAII for the PGconn handle.
//   - Pimpl idiom to hide libpq headers from the rest of the codebase.
// ============================================================================

#include "../../../domain/repositories/i_routing_repository.hpp"
#include "../config/config_loader.hpp"
#include "../security/tls_context.hpp"

#include <memory>

namespace neurometrix::router::infrastructure::postgres {

// Concrete adapter that persists routing requests to PostgreSQL.
//
// Lifecycle:
//   - Construct with the PostgreSQL configuration and the TLS context.
//   - Call start() once at application startup to open the connection.
//   - Use the five IRoutingRepository methods as the use case requires.
//   - Call stop() during graceful shutdown.
//
// The adapter is not copyable and not movable because it owns a libpq
// connection handle.
class PostgresRepositoryAdapter final
    : public domain::IRoutingRepository {
public:
    // Constructs the adapter. Does not connect to PostgreSQL yet.
    PostgresRepositoryAdapter(
        const config::PostgresConfig& config,
        const security::TLSContext&   tls
    ) noexcept;

    ~PostgresRepositoryAdapter() override;

    PostgresRepositoryAdapter(const PostgresRepositoryAdapter&) = delete;
    PostgresRepositoryAdapter& operator=(const PostgresRepositoryAdapter&) = delete;
    PostgresRepositoryAdapter(PostgresRepositoryAdapter&&) = delete;
    PostgresRepositoryAdapter& operator=(PostgresRepositoryAdapter&&) = delete;

    // Opens the connection to PostgreSQL and verifies it is usable.
    // Must be called once at startup, before any repository method.
    // Returns true on success. On failure, the caller must abort startup.
    [[nodiscard]] bool start() noexcept;

    // Closes the connection. Idempotent.
    void stop() noexcept;

    // ------------------------------------------------------------------------
    // IRoutingRepository implementation
    // ------------------------------------------------------------------------
    [[nodiscard]] std::expected<void, domain::RepositoryError>
    save(const domain::RoutingRequest& request) noexcept override;

    [[nodiscard]] std::expected<std::optional<domain::RoutingRequest>, domain::RepositoryError>
    find_by_message_id(std::uint64_t message_id) const noexcept override;

    [[nodiscard]] std::expected<std::vector<domain::RoutingRequest>, domain::RepositoryError>
    find_by_study_uid(const domain::StudyUid& study_uid) const noexcept override;

    [[nodiscard]] std::expected<void, domain::RepositoryError>
    mark_as_published(std::uint64_t message_id) noexcept override;

    [[nodiscard]] std::expected<void, domain::RepositoryError>
    mark_as_rejected(std::uint64_t message_id, std::string_view reason) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::postgres