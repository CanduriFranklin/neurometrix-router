#pragma once

// ============================================================================
// MetricsServer - Infrastructure Adapter
// ============================================================================
// Exposes the router's runtime metrics in Prometheus format on HTTP port
// 9090 (configurable).
//
// Uses prometheus-cpp with ENABLE_PULL=ON. The HTTP exposition is served
// by civetweb, an embedded HTTP server that prometheus-cpp pulls in
// automatically.
//
// Metric names are prefixed with "neurometrix_router_" to avoid collisions
// when multiple microservices share a Prometheus instance.
//
// Design principles:
//   - No dynamic labels (no cardinality explosion).
//   - Graceful shutdown of the HTTP server.
//   - Pimpl idiom: prometheus-cpp headers stay out of this header.
//   - No exceptions. All errors are logged and metrics stop updating.
// ============================================================================

#include "../config/config_loader.hpp"

#include <cstdint>
#include <memory>

namespace neurometrix::router::infrastructure::metrics {

// Concrete metrics server adapter.
//
// Lifecycle:
//   - Construct with the MetricsConfig.
//   - Call start() once at application startup. It binds the HTTP server.
//   - Record metrics via the increment/observe methods.
//   - Call stop() during graceful shutdown.
class MetricsServer final {
public:
    explicit MetricsServer(const config::MetricsConfig& config) noexcept;
    ~MetricsServer();

    MetricsServer(const MetricsServer&) = delete;
    MetricsServer& operator=(const MetricsServer&) = delete;
    MetricsServer(MetricsServer&&) = delete;
    MetricsServer& operator=(MetricsServer&&) = delete;

    // Binds the HTTP server and starts serving /metrics.
    // Returns true on success.
    [[nodiscard]] bool start() noexcept;

    // Stops the HTTP server. Idempotent.
    void stop() noexcept;

    // ------------------------------------------------------------------------
    // Metric recording methods
    // ------------------------------------------------------------------------
    // These methods are called from the application use case, the Kafka
    // adapters, and the DICOM adapter. Each one is thread-safe.

    void record_request_received() noexcept;
    void record_request_accepted() noexcept;
    void record_request_rejected() noexcept;

    // Records the end-to-end processing time, in seconds.
    void observe_processing_duration(double seconds) noexcept;

    void record_kafka_publish_error() noexcept;
    void record_dicom_read_error() noexcept;
    void record_repository_error() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::metrics