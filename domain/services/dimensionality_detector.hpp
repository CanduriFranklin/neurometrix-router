#pragma once

// ============================================================================
// DimensionalityDetector - Domain Service
// ============================================================================
// Pure domain service that decides the dimensionality of an MRI study
// (2D, 3D, or 4D) from metadata that has already been extracted from the
// DICOM files by an infrastructure adapter.
//
// This service is stateless, deterministic, and side-effect free.
// It does not read DICOM files. It does not publish messages. It only
// applies the routing rules of the domain.
//
// Detection rules (in priority order):
//   1. If temporal_frames > 1                        -> FourD
//   2. If slice_count > 1 AND slice_spacing_mm > 0   -> ThreeD
//   3. Otherwise                                     -> TwoD
//
// The service returns std::expected to signal invalid input (e.g., negative
// values or impossible combinations) without throwing exceptions.
// ============================================================================

#include "../value_objects/dimensionality.hpp"

#include <cstdint>
#include <expected>
#include <string_view>

namespace neurometrix::router::domain {

// Errors reported when the detector cannot classify a study.
enum class DetectionError : std::uint8_t {
    NegativeSliceCount,
    NegativeTemporalFrames,
    NegativeSliceSpacing,
    InconsistentFourD,      // temporal_frames > 1 but slice_count <= 1
    ZeroDimensions,         // slice_count == 0 and temporal_frames == 0
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(DetectionError e) noexcept {
    switch (e) {
        case DetectionError::NegativeSliceCount:     return "negative_slice_count";
        case DetectionError::NegativeTemporalFrames: return "negative_temporal_frames";
        case DetectionError::NegativeSliceSpacing:   return "negative_slice_spacing";
        case DetectionError::InconsistentFourD:      return "inconsistent_four_d";
        case DetectionError::ZeroDimensions:         return "zero_dimensions";
    }
    return "unknown";
}

// Input metadata extracted from the DICOM series by an infrastructure adapter.
// Only the fields that matter for the dimensionality decision are included.
struct StudyMetadata {
    std::uint32_t slice_count;          // Number of slices in the series
    std::uint32_t temporal_frames;      // Number of temporal frames (0 or 1 for static)
    float         slice_spacing_mm;     // Distance between slices, in millimeters
};

// Domain service that classifies a study.
class DimensionalityDetector {
public:
    // Classifies the given metadata.
    //
    // Returns:
    //   - Dimensionality::TwoD   if the study is a single slice or projection
    //   - Dimensionality::ThreeD if the study is a static volumetric acquisition
    //   - Dimensionality::FourD  if the study is a volumetric acquisition
    //                            repeated over time
    //
    // Returns an error if the metadata is inconsistent.
    [[nodiscard]] static std::expected<Dimensionality, DetectionError>
    detect(const StudyMetadata& metadata) noexcept {
        // Validate ranges first.
        if (metadata.slice_count == 0 && metadata.temporal_frames == 0) {
            return std::unexpected(DetectionError::ZeroDimensions);
        }
        if (metadata.slice_spacing_mm < 0.0f) {
            return std::unexpected(DetectionError::NegativeSliceSpacing);
        }

        const bool has_temporal   = metadata.temporal_frames > 1;
        const bool has_volume     = metadata.slice_count > 1
                                    && metadata.slice_spacing_mm > 0.0f;

        // Rule 1: temporal dimension present -> 4D
        if (has_temporal) {
            if (metadata.slice_count <= 1) {
                // Temporal frames exist but no volumetric data:
                // this is a temporal 2D acquisition, which we do not support.
                return std::unexpected(DetectionError::InconsistentFourD);
            }
            return Dimensionality::FourD;
        }

        // Rule 2: volumetric acquisition without temporal -> 3D
        if (has_volume) {
            return Dimensionality::ThreeD;
        }

        // Rule 3: otherwise -> 2D
        return Dimensionality::TwoD;
    }
};

} // namespace neurometrix::router::domain