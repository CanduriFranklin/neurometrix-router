#pragma once

// ============================================================================
// DicomReaderAdapter - Infrastructure Outbound Adapter
// ============================================================================
// Implementation of the application outbound port IDicomReader on top of
// DCMTK (the standard DICOM toolkit).
//
// Responsibilities:
//   1. Read single-file or multi-file DICOM series from a local path.
//   2. Extract the metadata required by DimensionalityDetector:
//        - slice_count
//        - temporal_frames
//        - slice_spacing_mm
//   3. Extract the voxel payload as raw bytes.
//   4. Compute the SHA-256 hash of the patient identifier before returning.
//   5. Validate the StudyInstanceUID through the domain value object.
//
// Security and privacy principles:
//   - The raw patient identifier NEVER leaves this adapter. Only the
//     SHA-256 hash is returned.
//   - The adapter enforces a configurable memory ceiling to prevent
//     denial-of-service through artificially large inputs.
//   - DICOM transfer syntaxes that require external decoders (JPEG2000,
//     RLE) return UnsupportedTransferSyntax if the decoder is not
//     available.
//
// Design principles:
//   - Implements IDicomReader exactly.
//   - No exceptions. All errors returned as DicomReadError.
//   - Pimpl idiom: DCMTK headers are hidden from this header.
// ============================================================================

#include "../../../application/ports/outbound/i_dicom_reader.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace neurometrix::router::infrastructure::dicom {

// Concrete adapter that reads DICOM studies from the local filesystem.
//
// Lifecycle:
//   - Construct with the maximum allowed payload size in bytes.
//   - Call read_study() as needed. The adapter is stateless between calls.
//
// The adapter is not copyable but is movable (DCMTK handles are local to
// each call; no persistent resources are held).
class DicomReaderAdapter final
    : public application::IDicomReader {
public:
    // Constructs the adapter.
    //
    // Parameters:
    //   max_payload_bytes : maximum allowed total voxel payload in bytes.
    //                       Any study exceeding this limit is rejected
    //                       with DicomReadError::CorruptedPixelData.
    //                       Default: 2 GiB.
    explicit DicomReaderAdapter(
        std::uint64_t max_payload_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL
    ) noexcept;

    ~DicomReaderAdapter() override;

    DicomReaderAdapter(const DicomReaderAdapter&) = delete;
    DicomReaderAdapter& operator=(const DicomReaderAdapter&) = delete;
    DicomReaderAdapter(DicomReaderAdapter&&) noexcept;
    DicomReaderAdapter& operator=(DicomReaderAdapter&&) noexcept;

    // ------------------------------------------------------------------------
    // IDicomReader implementation
    // ------------------------------------------------------------------------
    [[nodiscard]] std::expected<application::DicomStudy, application::DicomReadError>
    read_study(std::string_view path) const noexcept override;

    [[nodiscard]] bool can_read(std::string_view path) const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::dicom