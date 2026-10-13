// ============================================================================
// RouteRequestUseCase - Implementation
// ============================================================================
#include "route_request_use_case.hpp"

#include "../../domain/events/routing_event.hpp"
#include "../../domain/services/dimensionality_detector.hpp"

#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace neurometrix::router::application {

namespace {

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
[[nodiscard]] std::uint64_t now_utc_ms() noexcept {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Identity 4x4 matrix (row-major), fallback when the DICOM reader does not
// provide a calibration matrix. Kept as a function-local static to avoid
// rebuilding the buffer on every call.
// ---------------------------------------------------------------------------
[[nodiscard]] std::vector<float> identity_transform_matrix() {
    static const std::vector<float> kIdentity{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    return kIdentity;
}

constexpr std::string_view kRejectionInconsistentMetadata = "inconsistent_metadata";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
RouteRequestUseCase::RouteRequestUseCase(
    const IDicomReader&         dicom_reader,
    IMessagePublisher&          publisher,
    domain::IRoutingRepository& repository
) noexcept
    : dicom_reader_(dicom_reader),
      publisher_(publisher),
      repository_(repository)
{}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------
std::expected<RouteRequestResult, UseCaseError>
RouteRequestUseCase::execute(const RouteRequestInput& input) noexcept {
    const auto start = std::chrono::steady_clock::now();

    // ---- Step 1: validate input -------------------------------------------
    if (auto valid = validate_input(input); !valid) {
        return std::unexpected(valid.error());
    }

    // ---- Step 2: read DICOM study -----------------------------------------
    if (!dicom_reader_.can_read(input.dicom_path)) {
        return std::unexpected(UseCaseError::DicomReadFailed);
    }
    auto study_result = dicom_reader_.read_study(input.dicom_path);
    if (!study_result) {
        return std::unexpected(UseCaseError::DicomReadFailed);
    }
    const auto& study = *study_result;

    // ---- Step 3: detect dimensionality ------------------------------------
    auto detection = domain::DimensionalityDetector::detect(study.metadata);

    // ---- Step 4: build domain entity for audit ----------------------------
    const domain::Dimensionality placeholder_dim =
        detection.has_value() ? *detection : domain::Dimensionality::TwoD;

    domain::RoutingRequest request{
        input.message_id,
        study.study_uid,
        std::string{},                      // series_uid (pending reader support)
        placeholder_dim,
        study.metadata.slice_count,
        study.metadata.temporal_frames,
        identity_transform_matrix(),
        study.timestamp_utc_ms,
        study.patient_id_hash
    };

    if (!request.is_valid()) {
        return std::unexpected(UseCaseError::InternalError);
    }

    // ---- Step 5: persist audit record -------------------------------------
    if (auto saved = repository_.save(request); !saved) {
        return std::unexpected(UseCaseError::RepositoryUnavailable);
    }

    // ---- Step 6: rejection path -------------------------------------------
    if (!detection) {
        const std::string reason = std::string(domain::to_string(detection.error()));
        const std::uint64_t now = now_utc_ms();

        domain::RoutingRejected event{
            input.message_id,
            reason,
            now
        };

        if (auto published = publisher_.publish_routing_rejection(event); !published) {
            return std::unexpected(UseCaseError::PublisherUnavailable);
        }

        // Best-effort audit update. Failure here leaves the row in "pending"
        // and is intended to be reconciled offline by the audit reconciler.
        // TODO(metrics): emit a counter on failure so the reconciler has a signal.
        [[maybe_unused]] const auto mark = repository_.mark_as_rejected(
            input.message_id, reason);

        return make_rejection(
            std::string(kRejectionInconsistentMetadata),
            reason);
    }

    // ---- Step 7: success path ---------------------------------------------
    const domain::Dimensionality dimensionality = *detection;
    const std::string suffix(domain::to_topic_suffix(dimensionality));
    const std::uint64_t now = now_utc_ms();

    domain::RoutingDecisionMade event{
        input.message_id,
        study.study_uid,
        dimensionality,
        suffix,
        input.dicom_path,
        now
    };

    if (auto published = publisher_.publish_routing_decision(event); !published) {
        return std::unexpected(UseCaseError::PublisherUnavailable);
    }

    // Best-effort audit update. Same caveat as the rejection branch.
    [[maybe_unused]] const auto mark = repository_.mark_as_published(
        input.message_id);

    // ---- Step 8: processing duration & return success ---------------------
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    return make_success(
        study.study_uid,
        dimensionality,
        suffix,
        elapsed_ms);
}

// ---------------------------------------------------------------------------
// validate_input
// ---------------------------------------------------------------------------
std::expected<void, UseCaseError>
RouteRequestUseCase::validate_input(const RouteRequestInput& input) const noexcept {
    if (input.message_id == 0) {
        return std::unexpected(UseCaseError::InputValidationFailed);
    }
    if (input.dicom_path.empty()) {
        return std::unexpected(UseCaseError::InputValidationFailed);
    }
    if (input.received_at_utc == 0) {
        return std::unexpected(UseCaseError::InputValidationFailed);
    }
    return {};
}

// ---------------------------------------------------------------------------
// make_rejection
// ---------------------------------------------------------------------------
RouteRequestResult
RouteRequestUseCase::make_rejection(
    std::string reason_code,
    std::string reason_detail
) noexcept {
    RouteRequestResult result;
    result.accepted  = false;
    result.rejection = RouteRequestRejection{
        std::move(reason_code),
        std::move(reason_detail)
    };
    return result;
}

// ---------------------------------------------------------------------------
// make_success
// ---------------------------------------------------------------------------
RouteRequestResult
RouteRequestUseCase::make_success(
    const domain::StudyUid& study_uid,
    domain::Dimensionality dimensionality,
    std::string target_topic_suffix,
    std::uint64_t processing_time_ms
) noexcept {
    RouteRequestResult result;
    result.accepted = true;
    result.success  = RouteRequestSuccess{
        study_uid,
        dimensionality,
        std::move(target_topic_suffix),
        processing_time_ms
    };
    return result;
}

} // namespace neurometrix::router::application