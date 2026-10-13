// ============================================================================
// HealthEndpoint - Implementation
// ============================================================================
// See health_endpoint.hpp for the contract.
// ============================================================================

#include "health_endpoint.hpp"

#include "../logging/logger.hpp"

#include <httplib.h>

#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <utility>

namespace neurometrix::router::infrastructure::http {

// ---------------------------------------------------------------------------
// HealthEndpoint::Impl
// ---------------------------------------------------------------------------
struct HealthEndpoint::Impl {
    std::unique_ptr<httplib::Server> server;
    std::thread                      server_thread;
    std::atomic<bool>                started{false};
    ReadinessCheck                   readiness_check;
    std::string                      bind_addr;
    std::uint16_t                    port{0};
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
HealthEndpoint::HealthEndpoint(
    std::uint16_t port,
    std::string   bind_addr,
    ReadinessCheck check
) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    try {
        pimpl_->server = std::make_unique<httplib::Server>();
        pimpl_->readiness_check = std::move(check);

        // -----------------------------------------------------------------
        // Route: /healthz (liveness)
        //
        // Trivial check: if the server responds at all, the process is
        // alive. Returns 200 with a minimal JSON body.
        // -----------------------------------------------------------------
        pimpl_->server->Get("/healthz",
            [](const httplib::Request& /*req*/, httplib::Response& res) {
                res.status = 200;
                res.set_content(
                    R"({"status":"alive","service":"router"})",
                    "application/json");
            });

        // -----------------------------------------------------------------
        // Route: /readyz (readiness)
        //
        // Calls the readiness callback. Returns 200 if ready, 503 if not.
        // -----------------------------------------------------------------
        pimpl_->server->Get("/readyz",
            [this](const httplib::Request& /*req*/, httplib::Response& res) {
                bool ready = true;
                try {
                    if (pimpl_ && pimpl_->readiness_check) {
                        ready = pimpl_->readiness_check();
                    }
                } catch (...) {
                    ready = false;
                }

                if (ready) {
                    res.status = 200;
                    res.set_content(
                        R"({"status":"ready","service":"router"})",
                        "application/json");
                } else {
                    res.status = 503;
                    res.set_content(
                        R"({"status":"not_ready","service":"router"})",
                        "application/json");
                }
            });

        // -----------------------------------------------------------------
        // Bind to the specified address and port.
        // -----------------------------------------------------------------
        pimpl_->bind_addr = std::move(bind_addr);
        pimpl_->port      = port;
    } catch (...) {
        // Leave pimpl_ in a safe state. start() returns false.
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
HealthEndpoint::~HealthEndpoint() {
    stop();
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool HealthEndpoint::start() noexcept {
    if (!pimpl_ || !pimpl_->server) {
        return false;
    }

    try {
        // Start the HTTP server in a dedicated thread.
        // The listen() method blocks until stop() is called.
        pimpl_->server_thread = std::thread([this]() {
            try {
                if (pimpl_ && pimpl_->server) {
                    (void)pimpl_->server->listen(pimpl_->bind_addr.c_str(), static_cast<int>(pimpl_->port));
                }
            } catch (...) {
                // Swallow. The thread terminates.
            }
        });

        // Wait briefly for the server to bind, then verify it is running.
        // We use a short spin with a bounded timeout to avoid false negatives.
        for (int i = 0; i < 50; ++i) {
            if (pimpl_->server->is_running()) {
                pimpl_->started.store(true, std::memory_order_release);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return pimpl_->server->is_running();
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void HealthEndpoint::stop() noexcept {
    if (!pimpl_) return;

    try {
        if (pimpl_->server) {
            pimpl_->server->stop();
        }
        if (pimpl_->server_thread.joinable()) {
            pimpl_->server_thread.join();
        }
        pimpl_->started.store(false, std::memory_order_release);
    } catch (...) {
        // Swallow.
    }
}

} // namespace neurometrix::router::infrastructure::http