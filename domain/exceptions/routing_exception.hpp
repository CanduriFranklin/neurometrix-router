#pragma once

// ============================================================================
// Domain Exceptions - Router
// ============================================================================
// Exception hierarchy for the router domain layer.
//
// Design principle:
//   - std::expected is used for RECOVERABLE errors (invalid user input,
//     business rule violations that are expected to occur in production).
//   - Exceptions are used ONLY for FATAL errors: invariant violations,
//     programming bugs, or states that should never exist in a running
//     system. If one of these is ever thrown, it is a defect.
//
// All exceptions derive from RoutingException, which derives from
// std::runtime_error. This gives us a single catchable root for the
// router domain, distinct from infrastructure or standard exceptions.
// ============================================================================

#include <stdexcept>
#include <string>

namespace neurometrix::router::domain {

// Base class for all router domain exceptions.
// Never instantiated directly; always use one of the derived classes.
class RoutingException : public std::runtime_error {
public:
    explicit RoutingException(const std::string& message)
        : std::runtime_error(message) {}
};

// Thrown when a domain invariant is violated at runtime.
// Example: a RoutingRequest is constructed with slice_count == 0.
// This should have been caught by validation before construction,
// so reaching this point indicates a programming defect.
class InvariantViolationException final : public RoutingException {
public:
    explicit InvariantViolationException(const std::string& invariant)
        : RoutingException("Domain invariant violated: " + invariant) {}
};

// Thrown when the router attempts to route a study whose dimensionality
// cannot be resolved to any known target core.
// Example: a study classified as 5D by mistake.
class UnsupportedDimensionalityException final : public RoutingException {
public:
    explicit UnsupportedDimensionalityException(const std::string& description)
        : RoutingException("Unsupported dimensionality: " + description) {}
};

// Thrown when a value object is constructed with an input that should have
// been rejected earlier by its factory method.
// Example: a StudyUid instance created without going through StudyUid::create.
class InvalidValueObjectException final : public RoutingException {
public:
    explicit InvalidValueObjectException(const std::string& value_object_name)
        : RoutingException("Invalid value object: " + value_object_name) {}
};

// Thrown when the configuration of the router is internally inconsistent.
// Example: the Kafka topic suffix for FOUR_D is empty.
class RoutingConfigurationException final : public RoutingException {
public:
    explicit RoutingConfigurationException(const std::string& detail)
        : RoutingException("Routing configuration error: " + detail) {}
};

} // namespace neurometrix::router::domain