#pragma once

// ============================================================================
// StudyInstanceUID Value Object
// ============================================================================
// Strongly-typed representation of a DICOM StudyInstanceUID.
//
// Per the DICOM standard (PS3.5 Section 9):
//   - A UID is a string of dot-separated numeric components.
//   - Maximum total length: 64 characters.
//   - Each component must be a decimal integer with no leading zeros,
//     except the single digit "0".
//   - Components are separated by '.' and cannot be empty.
//
// This value object guarantees that any StudyUid instance holds a valid
// identifier. Construction is only possible through the factory method
// `create`, which returns std::expected.
// ============================================================================

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace neurometrix::router::domain {

// Domain-level errors produced when validating a StudyInstanceUID.
enum class StudyUidError : std::uint8_t {
    Empty,
    TooLong,
    InvalidCharacter,
    EmptyComponent,
    LeadingZero,
};

// Returns a stable string identifier suitable for logs and metrics.
[[nodiscard]] constexpr std::string_view to_string(StudyUidError e) noexcept {
    switch (e) {
        case StudyUidError::Empty:            return "empty";
        case StudyUidError::TooLong:          return "too_long";
        case StudyUidError::InvalidCharacter: return "invalid_character";
        case StudyUidError::EmptyComponent:   return "empty_component";
        case StudyUidError::LeadingZero:      return "leading_zero";
    }
    return "unknown";
}

// Value object representing a validated DICOM StudyInstanceUID.
class StudyUid {
public:
    static constexpr std::size_t kMaxLength = 64;

    // Factory method. Returns the constructed value object, or the error
    // describing why the input was rejected.
    [[nodiscard]] static std::expected<StudyUid, StudyUidError>
    create(std::string_view raw);

    // Access the canonical string form.
    [[nodiscard]] const std::string& value() const noexcept { return value_; }

    // Comparison operators for use in containers and DDD aggregates.
    [[nodiscard]] bool operator==(const StudyUid& other) const noexcept {
        return value_ == other.value_;
    }
    [[nodiscard]] bool operator!=(const StudyUid& other) const noexcept {
        return !(*this == other);
    }

private:
    explicit StudyUid(std::string value) noexcept : value_(std::move(value)) {}

    std::string value_;
};

// ---------------------------------------------------------------------------
// Inline implementation of the factory method
// ---------------------------------------------------------------------------
// Kept inline to avoid a separate .cpp file for a small value object.
// If the validation logic grows, move it to a dedicated .cpp file.
// ---------------------------------------------------------------------------
inline std::expected<StudyUid, StudyUidError>
StudyUid::create(std::string_view raw) {
    if (raw.empty()) {
        return std::unexpected(StudyUidError::Empty);
    }
    if (raw.size() > kMaxLength) {
        return std::unexpected(StudyUidError::TooLong);
    }

    bool previous_was_dot = true;      // true at start, so first component is checked
    bool current_has_digit = false;    // has any digit appeared in the current component?
    bool current_has_leading_zero = false;

    for (char c : raw) {
        if (c == '.') {
            if (previous_was_dot) {
                return std::unexpected(StudyUidError::EmptyComponent);
            }
            // A component that is exactly "0" is valid per DICOM PS3.5.
            // Components like "01" are already rejected above when the
            // second digit is read, so no extra check is needed here.
            previous_was_dot = true;
            current_has_digit = false;
            current_has_leading_zero = false;
            continue;
        }

        if (c < '0' || c > '9') {
            return std::unexpected(StudyUidError::InvalidCharacter);
        }

        if (!current_has_digit) {
            // First digit of the current component.
            if (c == '0') {
                current_has_leading_zero = true;
            }
        } else {
            // A leading zero is allowed only if the component is exactly "0".
            // As soon as another digit appears, we must know that the first
            // digit was not '0'. If it was, we already flagged it, and it will
            // be rejected when we see a second digit.
            if (current_has_leading_zero) {
                return std::unexpected(StudyUidError::LeadingZero);
            }
        }

        current_has_digit = true;
        previous_was_dot = false;
    }

    if (previous_was_dot) {
        return std::unexpected(StudyUidError::EmptyComponent);
    }
    if (current_has_leading_zero && raw.size() > 1) {
        // Case where the UID is exactly "0": allowed.
        // Case where the UID is "0" followed by a dot: rejected earlier.
        // Case where the last component starts with 0 and has more digits:
        // rejected inside the loop.
        // We only reach here for the single-component case "0", which is valid.
    }

    return StudyUid{std::string{raw}};
}

} // namespace neurometrix::router::domain