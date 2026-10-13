-- ============================================================================
-- Router - Operations Table (V1)
-- ============================================================================
-- Audit table for all routing decisions made by the router microservice.
--
-- Security notes:
--   - No PHI in this table. The column 'patient_id_hash' contains a SHA-256
--     hash, never the raw patient identifier.
--   - SQLSTATE 23505 (unique_violation) on the primary key enforces
--     idempotency at the database level.
--   - All queries against this table MUST use parameterized statements.
--     String concatenation is forbidden.
-- ============================================================================

CREATE TABLE IF NOT EXISTS routing_operations (
    -- Identity
    message_id           BIGINT       NOT NULL,
    study_instance_uid   TEXT         NOT NULL,
    series_instance_uid  TEXT,

    -- Classification
    dimensionality       SMALLINT     NOT NULL,   -- 2, 3, or 4
    slice_count          INTEGER      NOT NULL,
    temporal_frames      INTEGER      NOT NULL DEFAULT 0,

    -- Calibration (4x4 row-major, stored as a CSV string)
    transform_matrix     TEXT         NOT NULL,

    -- Timing
    timestamp_utc        BIGINT       NOT NULL,   -- Unix milliseconds

    -- Anonymization
    patient_id_hash      TEXT         NOT NULL,   -- SHA-256 hex

    -- State machine
    state                TEXT         NOT NULL DEFAULT 'pending',
                                                  -- 'pending' | 'published' | 'rejected'
    rejection_reason     TEXT,

    -- Audit timestamps
    created_at           TIMESTAMPTZ  NOT NULL DEFAULT NOW(),
    updated_at           TIMESTAMPTZ  NOT NULL DEFAULT NOW(),

    -- Constraints
    CONSTRAINT routing_operations_pk        PRIMARY KEY (message_id),
    CONSTRAINT routing_operations_state_ck  CHECK (state IN ('pending', 'published', 'rejected')),
    CONSTRAINT routing_operations_dim_ck    CHECK (dimensionality IN (2, 3, 4))
);

-- ----------------------------------------------------------------------------
-- Indexes
-- ----------------------------------------------------------------------------
-- Lookup by DICOM study (used to reconstruct the full routing history of a
-- patient study).
CREATE INDEX IF NOT EXISTS routing_operations_study_uid_idx
    ON routing_operations (study_instance_uid);

-- Lookup by state (used by cleanup jobs to find stale 'pending' rows).
CREATE INDEX IF NOT EXISTS routing_operations_state_idx
    ON routing_operations (state)
    WHERE state <> 'published';

-- ----------------------------------------------------------------------------
-- Comments for documentation
-- ----------------------------------------------------------------------------
COMMENT ON TABLE routing_operations IS
    'Audit table for routing decisions made by the router microservice. No PHI.';

COMMENT ON COLUMN routing_operations.message_id IS
    'Immutable ID assigned by the API Gateway. Unique per routing attempt.';

COMMENT ON COLUMN routing_operations.patient_id_hash IS
    'SHA-256 hash of the patient identifier. Raw PHI is never stored.';

COMMENT ON COLUMN routing_operations.transform_matrix IS
    '4x4 calibration matrix, row-major, stored as comma-separated floats.';