#pragma once

// ============================================================================
// Dimensionality Value Object
// ============================================================================
// Strongly-typed representation of the dimensionality of an MRI study.
//
// This value object is intentionally independent from the Protobuf enum
// declared in contracts/. The domain must not depend on the serialization
// format. The translation between this enum and the Protobuf enum happens
// exclusively in the infrastructure layer.
// ============================================================================

#include <cstdint>
#include <string_view>

namespace neurometrix::router::domain {

// The dimensionality of an MRI study, as detected by the router.
//   - TwoD   : a single slice or a projection
//   - ThreeD : a volumetric acquisition (multiple slices)
//   - FourD  : a volumetric acquisition repeated over time (functional MRI)
enum class Dimensionality : std::uint8_t {
    TwoD   = 2,
    ThreeD = 3,
    FourD  = 4,
};

// Returns the numeric value (2, 3, or 4) of the dimensionality.
[[nodiscard]] constexpr std::uint8_t to_numeric(Dimensionality d) noexcept {
    return static_cast<std::uint8_t>(d);
}

// Returns a stable string identifier, suitable for logs, metrics,
// and Kafka topic suffixes.
[[nodiscard]] constexpr std::string_view to_string(Dimensionality d) noexcept {
    switch (d) {
        case Dimensionality::TwoD:   return "2d";
        case Dimensionality::ThreeD: return "3d";
        case Dimensionality::FourD:  return "4d";
    }
    return "unknown";
}

// Returns the Kafka topic suffix used to route the study to the correct core.
// Convention: "mri.requests.2d", "mri.requests.3d", "mri.requests.4d".
[[nodiscard]] constexpr std::string_view to_topic_suffix(Dimensionality d) noexcept {
    return to_string(d);
}

} // namespace neurometrix::router::domain