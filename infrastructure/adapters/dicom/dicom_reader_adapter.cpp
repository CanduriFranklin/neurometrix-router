// ============================================================================
// DicomReaderAdapter - Implementation
// ============================================================================
// See dicom_reader_adapter.hpp for the contract.
// ============================================================================

#include "dicom_reader_adapter.hpp"

#include "../../../domain/services/dimensionality_detector.hpp"
#include "../../../domain/value_objects/study_uid.hpp"

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dctk.h>

#include <spdlog/spdlog.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neurometrix::router::infrastructure::dicom {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// Metadata extracted from a single DICOM file, before aggregation across
// the series.
// ---------------------------------------------------------------------------
struct SliceInfo {
    std::string study_uid;
    std::string series_uid;
    std::string patient_id;
    std::int32_t instance_number{0};
    double       slice_position{0.0};
    double       slice_thickness{0.0};
    double       spacing_between_slices{0.0};
    std::uint32_t rows{0};
    std::uint32_t columns{0};
    std::uint32_t number_of_frames{1};
    std::uint32_t number_of_temporal_positions{1};
    std::vector<std::byte> pixel_data;
};

// ---------------------------------------------------------------------------
// Extracts one string tag from a DcmDataset.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string
get_string(DcmDataset* ds, const DcmTagKey& tag) {
    OFString value;
    if (ds->findAndGetOFString(tag, value).good()) {
        return std::string{value.c_str()};
    }
    return {};
}

// ---------------------------------------------------------------------------
// Extracts one numeric tag from a DcmDataset, with fallback.
// ---------------------------------------------------------------------------
[[nodiscard]] double
get_double(DcmDataset* ds, const DcmTagKey& tag, double fallback) {
    Float64 value = 0.0;
    if (ds->findAndGetFloat64(tag, value).good()) {
        return static_cast<double>(value);
    }
    return fallback;
}

[[nodiscard]] std::int32_t
get_int32(DcmDataset* ds, const DcmTagKey& tag, std::int32_t fallback) {
    // Try native Sint32 first (SL, SS, US, UL).
    Sint32 value = 0;
    if (ds->findAndGetSint32(tag, value).good()) {
        return static_cast<std::int32_t>(value);
    }
    // Fallback: read as string (IS, DS VRs).
    OFString str;
    if (ds->findAndGetOFString(tag, str).good() && !str.empty()) {
        try {
            return static_cast<std::int32_t>(std::stoi(str.c_str()));
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// Reads a DICOM file and returns the extracted SliceInfo.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<SliceInfo, application::DicomReadError>
read_single_file(const fs::path& path) {
    SliceInfo info{};

    spdlog::info("[dicom] read_single_file: path='{}'", path.string());
    DcmFileFormat file_format;
    const OFCondition status = file_format.loadFile(path.string().c_str());
    if (!status.good()) {
        spdlog::error("[dicom] loadFile failed for '{}': {}",
                      path.string(), status.text());
        return std::unexpected(application::DicomReadError::InvalidHeader);
    }
    spdlog::info("[dicom] loadFile OK for '{}'", path.string());

    DcmDataset* ds = file_format.getDataset();
    if (ds == nullptr) {
        spdlog::error("[dicom] getDataset returned nullptr for '{}'", path.string());
        return std::unexpected(application::DicomReadError::InvalidHeader);
    }

    info.study_uid  = get_string(ds, DCM_StudyInstanceUID);
    info.series_uid = get_string(ds, DCM_SeriesInstanceUID);
    info.patient_id = get_string(ds, DCM_PatientID);

    spdlog::info("[dicom] tags read from '{}': study_uid='{}' series_uid='{}' patient_id='{}'",
                 path.string(), info.study_uid, info.series_uid, info.patient_id);
    if (info.study_uid.empty()) {
        spdlog::error("[dicom] study_uid is EMPTY for '{}'", path.string());
        return std::unexpected(application::DicomReadError::MissingMandatoryTag);
    }

    info.instance_number       = get_int32(ds, DCM_InstanceNumber, 0);
    info.slice_position        = get_double(ds, DCM_SliceLocation, 0.0);
    info.slice_thickness       = get_double(ds, DCM_SliceThickness, 0.0);
    info.spacing_between_slices= get_double(ds, DCM_SpacingBetweenSlices, 0.0);

    info.rows = static_cast<std::uint32_t>(get_int32(ds, DCM_Rows, 0));
    info.columns = static_cast<std::uint32_t>(get_int32(ds, DCM_Columns, 0));

    // Multi-frame detection.
    info.number_of_frames = static_cast<std::uint32_t>(
        get_int32(ds, DCM_NumberOfFrames, 1));

    // Extract pixel data as raw bytes.
    const Uint16* pixel_array = nullptr;
    unsigned long pixel_count = 0;
    const OFCondition pixel_status =
        ds->findAndGetUint16Array(DCM_PixelData, pixel_array, &pixel_count);

    if (pixel_status.good() && pixel_array != nullptr && pixel_count > 0) {
        const std::size_t total_bytes =
            static_cast<std::size_t>(pixel_count) * sizeof(std::uint16_t);
        info.pixel_data.resize(total_bytes);
        std::memcpy(info.pixel_data.data(), pixel_array, total_bytes);
    } else {
        spdlog::error("[dicom] pixel data extraction failed for '{}': status={} pixels={} ptr={}",
                      path.string(), pixel_status.text(), pixel_count,
                      pixel_array == nullptr ? "null" : "valid");
        return std::unexpected(application::DicomReadError::CorruptedPixelData);
    }

    return info;
}

// ---------------------------------------------------------------------------
// Computes SHA-256 of the given input string and returns it as a lowercase
// hex string.
// ---------------------------------------------------------------------------
[[nodiscard]] std::string sha256_hex(std::string_view input) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        return {};
    }

    std::string result;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1 &&
        EVP_DigestUpdate(ctx, input.data(), input.size()) == 1 &&
        EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) == 1) {

        static constexpr char kHex[] = "0123456789abcdef";
        result.reserve(digest_len * 2);
        for (unsigned int i = 0; i < digest_len; ++i) {
            result.push_back(kHex[(digest[i] >> 4) & 0x0F]);
            result.push_back(kHex[digest[i] & 0x0F]);
        }
    }
    EVP_MD_CTX_free(ctx);
    return result;
}

// ---------------------------------------------------------------------------
// Aggregates the slice infos of a multi-file series into a single payload,
// sorting by InstanceNumber.
// ---------------------------------------------------------------------------
[[nodiscard]] std::expected<std::vector<std::byte>, application::DicomReadError>
aggregate_pixels(
    std::vector<SliceInfo>& slices,
    std::uint64_t max_payload_bytes)
{
    // Sort slices by InstanceNumber (ascending). If numbers are all zero,
    // sort by slice_position.
    const bool any_instance_number =
        std::any_of(slices.begin(), slices.end(),
                    [](const SliceInfo& s) { return s.instance_number != 0; });

    if (any_instance_number) {
        std::sort(slices.begin(), slices.end(),
                  [](const SliceInfo& a, const SliceInfo& b) {
                      return a.instance_number < b.instance_number;
                  });
    } else {
        std::sort(slices.begin(), slices.end(),
                  [](const SliceInfo& a, const SliceInfo& b) {
                      return a.slice_position < b.slice_position;
                  });
    }

    // Sum payload sizes and enforce the ceiling.
    std::uint64_t total_bytes = 0;
    for (const auto& slice : slices) {
        total_bytes += slice.pixel_data.size();
        if (total_bytes > max_payload_bytes) {
            return std::unexpected(application::DicomReadError::CorruptedPixelData);
        }
    }

    std::vector<std::byte> payload;
    payload.reserve(static_cast<std::size_t>(total_bytes));
    for (const auto& slice : slices) {
        payload.insert(payload.end(),
                       slice.pixel_data.begin(),
                       slice.pixel_data.end());
    }
    return payload;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// DicomReaderAdapter::Impl
// ---------------------------------------------------------------------------
struct DicomReaderAdapter::Impl {
    std::uint64_t max_payload_bytes;
};

// ---------------------------------------------------------------------------
// Constructor / Destructor / Move
// ---------------------------------------------------------------------------
DicomReaderAdapter::DicomReaderAdapter(
    std::uint64_t max_payload_bytes) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    pimpl_->max_payload_bytes = max_payload_bytes;
}

DicomReaderAdapter::~DicomReaderAdapter() = default;

DicomReaderAdapter::DicomReaderAdapter(DicomReaderAdapter&&) noexcept = default;
DicomReaderAdapter& DicomReaderAdapter::operator=(DicomReaderAdapter&&) noexcept = default;

// ---------------------------------------------------------------------------
// can_read
// ---------------------------------------------------------------------------
bool DicomReaderAdapter::can_read(std::string_view path) const noexcept {
    try {
        const fs::path p{std::string{path}};
        std::error_code ec;
        const bool exists = fs::exists(p, ec) && !ec;
        spdlog::info("[dicom] can_read('{}') = {}", std::string(path), exists);
        return exists;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// read_study
// ---------------------------------------------------------------------------
std::expected<application::DicomStudy, application::DicomReadError>
DicomReaderAdapter::read_study(std::string_view path) const noexcept {
    try {
        spdlog::info("[dicom] read_study called with path='{}'", std::string(path));
        const fs::path p{std::string{path}};
        std::error_code ec;

        if (!fs::exists(p, ec) || ec) {
            spdlog::error("[dicom] file not found: '{}' ec={}", std::string(path), ec.message());
            return std::unexpected(application::DicomReadError::FileNotFound);
        }
        spdlog::info("[dicom] file exists: '{}'", std::string(path));

        std::vector<SliceInfo> slices;

        if (fs::is_directory(p, ec)) {
            // Multi-file series.
            for (const auto& entry : fs::directory_iterator(p, ec)) {
                if (ec) {
                    return std::unexpected(application::DicomReadError::IOError);
                }
                if (!entry.is_regular_file(ec) || ec) continue;
                const auto& ep = entry.path();
                if (ep.extension() != ".dcm" && ep.extension() != ".DCM") continue;

                auto slice = read_single_file(ep);
                if (!slice) {
                    return std::unexpected(slice.error());
                }
                slices.push_back(std::move(*slice));
            }
            if (slices.empty()) {
                return std::unexpected(application::DicomReadError::FileNotFound);
            }
        } else if (fs::is_regular_file(p, ec) && !ec) {
            auto slice = read_single_file(p);
            if (!slice) {
                return std::unexpected(slice.error());
            }
            slices.push_back(std::move(*slice));
        } else {
            return std::unexpected(application::DicomReadError::IOError);
        }

        // -----------------------------------------------------------------
        // Aggregate pixel data.
        // -----------------------------------------------------------------
        auto payload = aggregate_pixels(slices, pimpl_->max_payload_bytes);
        if (!payload) {
            return std::unexpected(payload.error());
        }

        // -----------------------------------------------------------------
        // Validate StudyInstanceUID through the domain value object.
        // -----------------------------------------------------------------
        spdlog::info("[dicom] creating StudyUid from '{}'", slices.front().study_uid);
        auto study_uid = domain::StudyUid::create(slices.front().study_uid);
        if (!study_uid) {
            spdlog::error("[dicom] StudyUid::create failed for '{}'", slices.front().study_uid);
            return std::unexpected(application::DicomReadError::MissingMandatoryTag);
        }

        // -----------------------------------------------------------------
        // Build StudyMetadata for the domain service.
        // -----------------------------------------------------------------
        domain::StudyMetadata metadata{};
        // Decompose frames into (slice_count x temporal_frames).
        // Multiframe DICOM packs all slices and all time points in a
        // single file, so slices.size() == 1. We use the standard
        // NumberOfTemporalPositions tag (0020,0105) to separate them.
        const std::uint32_t n_frames =
            slices.front().number_of_frames;
        const std::uint32_t n_temporal =
            slices.front().number_of_temporal_positions;
        if (n_temporal > 1 && n_frames >= n_temporal) {
            metadata.slice_count     = n_frames / n_temporal;
            metadata.temporal_frames = n_temporal;
        } else {
            metadata.slice_count     = n_frames;
            metadata.temporal_frames = 1;
        }
        spdlog::info("[dicom] decomposed: total_frames={} temporal={} -> slices={} frames={}",
                     n_frames, n_temporal,
                     metadata.slice_count, metadata.temporal_frames);

        // Slice spacing: prefer SpacingBetweenSlices, fallback SliceThickness.
        const auto& first = slices.front();
        metadata.slice_spacing_mm = static_cast<float>(
            first.spacing_between_slices > 0.0
                ? first.spacing_between_slices
                : first.slice_thickness);

        // -----------------------------------------------------------------
        // Compute SHA-256 hash of the patient identifier.
        // The raw ID never leaves this function.
        // -----------------------------------------------------------------
        const std::string hash = sha256_hex(slices.front().patient_id);
        if (hash.empty()) {
            return std::unexpected(application::DicomReadError::UnknownError);
        }

        // -----------------------------------------------------------------
        // Build the DicomStudy aggregate.
        // -----------------------------------------------------------------
        application::DicomStudy study{
            .study_uid        = std::move(*study_uid),
            .metadata         = metadata,
            .voxel_payload    = std::move(*payload),
            .timestamp_utc_ms = 0,          // set by the caller from received_at
            .patient_id_hash  = hash
        };

        return study;
    } catch (const std::bad_alloc&) {
        return std::unexpected(application::DicomReadError::CorruptedPixelData);
    } catch (...) {
        return std::unexpected(application::DicomReadError::UnknownError);
    }
}

} // namespace neurometrix::router::infrastructure::dicom