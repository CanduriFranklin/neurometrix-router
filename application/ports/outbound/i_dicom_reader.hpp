#pragma once

// ============================================================================
// IDicomReader - Application Outbound Port
// ============================================================================
// Port that defines the contract for reading DICOM study metadata and
// pixel data from disk or from an object store.
//
// The application layer depends on this interface, never on a specific
// DICOM library (DCMTK, GDCM, etc.).
//
// The infrastructure layer provides an adapter (DicomReaderAdapter) that
// uses a concrete library internally. Changing the library only affects
// the adapter, not the application or the domain.
//
// Design principles:
//   - Returns domain value objects (StudyUid) and domain structs
//     (StudyMetadata) so that the application can pass them directly
//     to the domain service.
//   - Returns std::expected for recoverable errors.
//   - No exceptions. All failures are explicit.
//   - No mention of DCMTK, GDCM, or any specific library.
// ============================================================================

#include "../../../domain/services/dimensionality_detector.hpp"
#include "../../../domain/value_objects/study_uid.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace neurometrix::router::application {

// Errors reported by the DICOM reader port.
enum class DicomReadError : std::uint8_t {
    FileNotFound,
    InvalidHeader,
    MissingMandatoryTag,
    UnsupportedTransferSyntax,
    CorruptedPixelData,
    DecompressionFailure,
    IOError,
    UnknownError,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(DicomReadError e) noexcept {
    switch (e) {
        case DicomReadError::FileNotFound:             return "file_not_found";
        case DicomReadError::InvalidHeader:            return "invalid_header";
        case DicomReadError::MissingMandatoryTag:      return "missing_mandatory_tag";
        case DicomReadError::UnsupportedTransferSyntax:return "unsupported_transfer_syntax";
        case DicomReadError::CorruptedPixelData:       return "corrupted_pixel_data";
        case DicomReadError::DecompressionFailure:     return "decompression_failure";
        case DicomReadError::IOError:                  return "io_error";
        case DicomReadError::UnknownError:             return "unknown_error";
    }
    return "unknown";
}

// Result of reading a DICOM study.
//
// Contains:
//   - study_uid: validated StudyUid value object
//   - metadata:  the fields needed by the DimensionalityDetector domain service
//   - voxel_payload: raw pixel bytes, already decompressed if applicable
struct DicomStudy {
    domain::StudyUid               study_uid;
    domain::StudyMetadata          metadata;
    std::vector<std::byte>         voxel_payload;
    std::uint64_t                  timestamp_utc_ms;
    std::string                    patient_id_hash;
};

// Port for reading DICOM studies.
//
// Contract:
//   - read_study() MUST return a fully validated StudyUid. If the file has
//     an invalid StudyInstanceUID, the adapter returns MissingMandatoryTag
//     or InvalidHeader; it does not silently create an invalid StudyUid.
//   - read_study() MUST handle multi-frame DICOM files (enhanced MR, etc.)
//     transparently. The caller receives a flat voxel_payload and the
//     metadata fields (slice_count, temporal_frames, slice_spacing_mm)
//     already populated.
//   - read_study() MUST NOT exceed a configurable memory ceiling.
//     The adapter enforces this and returns CorruptedPixelData or IOError
//     if the study is too large to load.
//   - read_study() MUST NOT perform any PHI access that is not strictly
//     needed. Only a hash of the patient ID is returned.
class IDicomReader {
public:
    virtual ~IDicomReader() = default;

    // Reads a single DICOM study from the given path.
    // The path may point to a single file or to a directory containing
    // a series of DICOM files belonging to the same study.
    [[nodiscard]] virtual std::expected<DicomStudy, DicomReadError>
    read_study(std::string_view path) const noexcept = 0;

    // Returns true if the given path exists and is readable by the adapter.
    // Used by the application to fail fast before attempting a full read.
    [[nodiscard]] virtual bool can_read(std::string_view path) const noexcept = 0;
};

} // namespace neurometrix::router::application
