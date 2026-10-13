// ============================================================================
// PostgresRepositoryAdapter - Implementation
// ============================================================================
// See postgres_repository_adapter.hpp for the contract.
// ============================================================================

#include "postgres_repository_adapter.hpp"

#include "../logging/logger.hpp"

#include <libpq-fe.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neurometrix::router::infrastructure::postgres {

namespace {

// ---------------------------------------------------------------------------
// libpq RAII wrappers
// ---------------------------------------------------------------------------
struct PgResultDeleter {
    void operator()(PGresult* p) const noexcept {
        if (p) PQclear(p);
    }
};
using PgResultPtr = std::unique_ptr<PGresult, PgResultDeleter>;

// ---------------------------------------------------------------------------
// SQLSTATE to RepositoryError mapping.
//
// Every SQLSTATE used here is documented in PostgreSQL Appendix A:
//   https://www.postgresql.org/docs/current/errcodes-appendix.html
// ---------------------------------------------------------------------------
[[nodiscard]] domain::RepositoryError
map_sqlstate(const char* sqlstate) noexcept {
    if (sqlstate == nullptr) {
        return domain::RepositoryError::UnknownError;
    }
    // 23505 : unique_violation
    if (std::strcmp(sqlstate, "23505") == 0) {
        return domain::RepositoryError::DuplicateKey;
    }
    // 40001 : serialization_failure
    if (std::strcmp(sqlstate, "40001") == 0) {
        return domain::RepositoryError::SerializationFailure;
    }
    // 08xxx : connection exceptions (class 08)
    if (std::strncmp(sqlstate, "08", 2) == 0) {
        return domain::RepositoryError::ConnectionLost;
    }
    return domain::RepositoryError::UnknownError;
}

// ---------------------------------------------------------------------------
// Builds a libpq connection string from PostgresConfig and TLSContext.
// All parameters are key=value pairs separated by spaces.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string
build_connection_string(const config::PostgresConfig& cfg,
                        const security::TLSContext&   tls) {
    std::string conn = cfg.connection_string;
    if (conn.empty()) return conn;

    const bool is_uri =
        conn.rfind("postgresql://", 0) == 0 ||
        conn.rfind("postgres://",   0) == 0;

    auto add_kv = [&](std::string_view k, std::string_view v) {
        if (v.empty()) return;
        if (is_uri) {
            conn += (conn.find('?') == std::string::npos ? '?' : '&');
            conn += k;
            conn += '=';
            conn += v;
        } else {
            if (!conn.empty() && conn.back() != ' ') conn += ' ';
            conn += k;
            conn += '=';
            conn += v;
        }
    };

    add_kv("sslmode",         cfg.ssl_mode);
    add_kv("connect_timeout", std::to_string(cfg.connection_timeout_ms / 1000));

    const bool tls_enabled =
        cfg.ssl_mode != "disable" && cfg.ssl_mode != "allow";
    if (tls_enabled) {
        add_kv("sslcert",     tls.cert_path());
        add_kv("sslkey",      tls.key_path());
        add_kv("sslrootcert", tls.ca_path());
    }

    return conn;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PostgresRepositoryAdapter::Impl
// ---------------------------------------------------------------------------
struct PostgresRepositoryAdapter::Impl {
    PGconn*     conn{nullptr};
    std::string conninfo;
    bool        started{false};

    ~Impl() {
        if (conn) {
            PQfinish(conn);
            conn = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
PostgresRepositoryAdapter::PostgresRepositoryAdapter(
    const config::PostgresConfig& config,
    const security::TLSContext&   tls
) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    try {
        pimpl_->conninfo = build_connection_string(config, tls);
    } catch (...) {
        pimpl_->conninfo.clear();
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
PostgresRepositoryAdapter::~PostgresRepositoryAdapter() {
    stop();
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool PostgresRepositoryAdapter::start() noexcept {
    if (!pimpl_ || pimpl_->conninfo.empty()) {
        return false;
    }
    try {
        pimpl_->conn = PQconnectdb(pimpl_->conninfo.c_str());
        if (!pimpl_->conn) {
            std::fprintf(stderr, "[postgres] PQconnectdb returned null\n");
            return false;
        }
        if (PQstatus(pimpl_->conn) != CONNECTION_OK) {
            std::fprintf(stderr, "[postgres] %s\n", PQerrorMessage(pimpl_->conn));
            std::fprintf(stderr, "[postgres] conninfo: %s\n", pimpl_->conninfo.c_str());
            return false;
        }
        pimpl_->started = true;
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void PostgresRepositoryAdapter::stop() noexcept {
    if (!pimpl_) return;
    try {
        if (pimpl_->conn) {
            PQfinish(pimpl_->conn);
            pimpl_->conn = nullptr;
        }
        pimpl_->started = false;
    } catch (...) {
        // Swallow.
    }
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------
std::expected<void, domain::RepositoryError>
PostgresRepositoryAdapter::save(
    const domain::RoutingRequest& request) noexcept
{
    if (!pimpl_ || !pimpl_->started) {
        return std::unexpected(domain::RepositoryError::ConnectionLost);
    }

    try {
        // ------------------------------------------------------------------
        // Parameterized query. NEVER concatenate.
        //
        // The SQL text is a compile-time literal. Only the parameters
        // ($1, $2, ...) carry user-controlled data. This is the primary
        // defense against SQL injection.
        // ------------------------------------------------------------------
        static constexpr const char* kSql =
            "INSERT INTO routing_operations ("
            "    message_id, study_instance_uid, series_instance_uid,"
            "    dimensionality, slice_count, temporal_frames,"
            "    transform_matrix, timestamp_utc, patient_id_hash,"
            "    state"
            ") VALUES ("
            "    $1, $2, $3, $4, $5, $6, $7, $8, $9, 'pending'"
            ")";

        // Serialize the 4x4 transform matrix as a comma-separated string.
        std::string matrix_str;
        {
            const auto matrix = request.transform_matrix();
            matrix_str.reserve(matrix.size() * 12);
            for (std::size_t i = 0; i < matrix.size(); ++i) {
                if (i > 0) matrix_str += ',';
                matrix_str += std::to_string(matrix[i]);
            }
        }

        const std::string message_id_str   = std::to_string(request.message_id());
        const std::string study_uid_str    = request.study_uid().value();
        const std::string series_uid_str   = request.series_uid();
        const std::string dimensionality   = std::to_string(
            static_cast<unsigned>(request.dimensionality()));
        const std::string slice_count_str  = std::to_string(request.slice_count());
        const std::string temporal_str     = std::to_string(request.temporal_frames());
        const std::string timestamp_str    = std::to_string(request.timestamp_utc());
        const std::string patient_hash     = request.patient_id_hash();

        const char* param_values[9] = {
            message_id_str.c_str(),
            study_uid_str.c_str(),
            series_uid_str.c_str(),
            dimensionality.c_str(),
            slice_count_str.c_str(),
            temporal_str.c_str(),
            matrix_str.c_str(),
            timestamp_str.c_str(),
            patient_hash.c_str()
        };

        PgResultPtr result{
            PQexecParams(
                pimpl_->conn,
                kSql,
                9,              // number of parameters
                nullptr,        // let libpq infer types
                param_values,
                nullptr,        // lengths (not needed for text)
                nullptr,        // formats (text by default)
                0)              // result format: text
        };

        if (!result) {
            return std::unexpected(domain::RepositoryError::UnknownError);
        }

        const ExecStatusType status = PQresultStatus(result.get());
        if (status == PGRES_COMMAND_OK) {
            return {};
        }

        const char* sqlstate = PQresultErrorField(
            result.get(), PG_DIAG_SQLSTATE);
        const auto err = map_sqlstate(sqlstate);
        logging::log_kafka_error(
            request.message_id(),
            std::string(domain::to_string(err)),
            "postgres_save");
        return std::unexpected(err);
    } catch (...) {
        return std::unexpected(domain::RepositoryError::UnknownError);
    }
}

// ---------------------------------------------------------------------------
// find_by_message_id
// ---------------------------------------------------------------------------
std::expected<std::optional<domain::RoutingRequest>, domain::RepositoryError>
PostgresRepositoryAdapter::find_by_message_id(
    std::uint64_t message_id) const noexcept
{
    if (!pimpl_ || !pimpl_->started) {
        return std::unexpected(domain::RepositoryError::ConnectionLost);
    }
    try {
        static constexpr const char* kSql =
            "SELECT message_id, study_instance_uid, series_instance_uid,"
            "       dimensionality, slice_count, temporal_frames,"
            "       transform_matrix, timestamp_utc, patient_id_hash"
            "  FROM routing_operations"
            " WHERE message_id = $1";

        const std::string mid = std::to_string(message_id);
        const char* params[1] = { mid.c_str() };

        PgResultPtr result{PQexecParams(
            pimpl_->conn, kSql, 1, nullptr, params,
            nullptr, nullptr, 0)};

        if (!result) {
            return std::unexpected(domain::RepositoryError::UnknownError);
        }

        const ExecStatusType status = PQresultStatus(result.get());
        if (status != PGRES_TUPLES_OK) {
            const char* sqlstate = PQresultErrorField(
                result.get(), PG_DIAG_SQLSTATE);
            return std::unexpected(map_sqlstate(sqlstate));
        }

        const int rows = PQntuples(result.get());
        if (rows == 0) {
            return std::optional<domain::RoutingRequest>{};
        }

        // ------------------------------------------------------------------
        // Reconstruct the RoutingRequest from the row.
        //
        // NOTE: The full reconstruction requires the transform matrix to be
        // parsed from the comma-separated string and the patient hash to be
        // applied to a new StudyUid. This is intentionally minimal for now;
        // the full reconstruction will be completed when the schema is
        // finalized together with the DICOM reader.
        // ------------------------------------------------------------------
        return std::optional<domain::RoutingRequest>{};
    } catch (...) {
        return std::unexpected(domain::RepositoryError::UnknownError);
    }
}

// ---------------------------------------------------------------------------
// find_by_study_uid
// ---------------------------------------------------------------------------
std::expected<std::vector<domain::RoutingRequest>, domain::RepositoryError>
PostgresRepositoryAdapter::find_by_study_uid(
    const domain::StudyUid& study_uid) const noexcept
{
    if (!pimpl_ || !pimpl_->started) {
        return std::unexpected(domain::RepositoryError::ConnectionLost);
    }
    try {
        static constexpr const char* kSql =
            "SELECT message_id, study_instance_uid, series_instance_uid,"
            "       dimensionality, slice_count, temporal_frames,"
            "       transform_matrix, timestamp_utc, patient_id_hash"
            "  FROM routing_operations"
            " WHERE study_instance_uid = $1"
            " ORDER BY timestamp_utc ASC";

        const std::string uid = study_uid.value();
        const char* params[1] = { uid.c_str() };

        PgResultPtr result{PQexecParams(
            pimpl_->conn, kSql, 1, nullptr, params,
            nullptr, nullptr, 0)};

        if (!result) {
            return std::unexpected(domain::RepositoryError::UnknownError);
        }
        if (PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
            const char* sqlstate = PQresultErrorField(
                result.get(), PG_DIAG_SQLSTATE);
            return std::unexpected(map_sqlstate(sqlstate));
        }

        // Placeholder: the full reconstruction will be implemented when the
        // schema and DICOM reader are finalized together.
        return std::vector<domain::RoutingRequest>{};
    } catch (...) {
        return std::unexpected(domain::RepositoryError::UnknownError);
    }
}

// ---------------------------------------------------------------------------
// mark_as_published
// ---------------------------------------------------------------------------
std::expected<void, domain::RepositoryError>
PostgresRepositoryAdapter::mark_as_published(
    std::uint64_t message_id) noexcept
{
    if (!pimpl_ || !pimpl_->started) {
        return std::unexpected(domain::RepositoryError::ConnectionLost);
    }
    try {
        static constexpr const char* kSql =
            "UPDATE routing_operations"
            "   SET state = 'published',"
            "       updated_at = NOW()"
            " WHERE message_id = $1";

        const std::string mid = std::to_string(message_id);
        const char* params[1] = { mid.c_str() };

        PgResultPtr result{PQexecParams(
            pimpl_->conn, kSql, 1, nullptr, params,
            nullptr, nullptr, 0)};

        if (!result) {
            return std::unexpected(domain::RepositoryError::UnknownError);
        }
        if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
            const char* sqlstate = PQresultErrorField(
                result.get(), PG_DIAG_SQLSTATE);
            return std::unexpected(map_sqlstate(sqlstate));
        }
        return {};
    } catch (...) {
        return std::unexpected(domain::RepositoryError::UnknownError);
    }
}

// ---------------------------------------------------------------------------
// mark_as_rejected
// ---------------------------------------------------------------------------
std::expected<void, domain::RepositoryError>
PostgresRepositoryAdapter::mark_as_rejected(
    std::uint64_t message_id,
    std::string_view reason) noexcept
{
    if (!pimpl_ || !pimpl_->started) {
        return std::unexpected(domain::RepositoryError::ConnectionLost);
    }
    try {
        static constexpr const char* kSql =
            "UPDATE routing_operations"
            "   SET state = 'rejected',"
            "       rejection_reason = $2,"
            "       updated_at = NOW()"
            " WHERE message_id = $1";

        const std::string mid = std::to_string(message_id);
        const std::string reason_str{reason};

        const char* params[2] = { mid.c_str(), reason_str.c_str() };

        PgResultPtr result{PQexecParams(
            pimpl_->conn, kSql, 2, nullptr, params,
            nullptr, nullptr, 0)};

        if (!result) {
            return std::unexpected(domain::RepositoryError::UnknownError);
        }
        if (PQresultStatus(result.get()) != PGRES_COMMAND_OK) {
            const char* sqlstate = PQresultErrorField(
                result.get(), PG_DIAG_SQLSTATE);
            return std::unexpected(map_sqlstate(sqlstate));
        }
        return {};
    } catch (...) {
        return std::unexpected(domain::RepositoryError::UnknownError);
    }
}

} // namespace neurometrix::router::infrastructure::postgres