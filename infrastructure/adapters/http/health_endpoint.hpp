#pragma once

// ============================================================================
// HealthEndpoint - Infrastructure Adapter
// ============================================================================
// Exposes two HTTP endpoints required by Kubernetes probes:
//
//   GET /healthz   -> liveness  (200 if the process is running)
//   GET /readyz    -> readiness (200 if the router is ready for traffic)
//
// Liveness is trivial: if the HTTP server responds, the process is alive.
// Readiness is non-trivial: it checks that the Kafka consumer and the
// PostgreSQL repository have been started successfully.
//
// The endpoint binds to a dedicated port (default: 8081), separate from
// the metrics port (9090), so that probes and scrapes do not interfere.
//
// Design principles:
//   - Pimpl idiom: the cpp-httplib header stays out of this file.
//   - No exceptions. All errors are logged and result in a 503 response.
//   - Pluggable readiness: the caller supplies a callback that returns the
//     current readiness state.
// ============================================================================

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace neurometrix::router::infrastructure::http {

// Concrete health endpoint adapter.
//
// Lifecycle:
//   - Construct with the port, the bind address, and a readiness callback.
//   - Call start() once at application startup. It binds the HTTP server
//     and starts serving in a background thread.
//   - Call stop() during graceful shutdown.
class HealthEndpoint final {
public:
    // Callback invoked by /readyz to determine readiness.
    // Returns true if the router is ready to accept traffic.
    using ReadinessCheck = std::function<bool()>;

    // Constructs the endpoint.
    //
    // Parameters:
    //   port       : HTTP port. Default: 8081.
    //   bind_addr  : bind address. Default: "0.0.0.0".
    //   check      : callback invoked by /readyz. If empty, /readyz always
    //                returns 200.
    HealthEndpoint(
        std::uint16_t port,
        std::string   bind_addr,
        ReadinessCheck check
    ) noexcept;

    ~HealthEndpoint();

    HealthEndpoint(const HealthEndpoint&) = delete;
    HealthEndpoint& operator=(const HealthEndpoint&) = delete;
    HealthEndpoint(HealthEndpoint&&) = delete;
    HealthEndpoint& operator=(HealthEndpoint&&) = delete;

    // Starts the HTTP server in a background thread.
    // Returns true if the port was bound successfully.
    [[nodiscard]] bool start() noexcept;

    // Stops the HTTP server. Idempotent.
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace neurometrix::router::infrastructure::http