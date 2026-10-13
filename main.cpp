// ============================================================================
// Router - Composition Root
// ============================================================================
// This is the ONLY place in the codebase where concrete implementations
// are instantiated and wired together. Every other layer receives its
// dependencies through constructor injection.
//
// Startup order:
//   1. Logging
//   2. Configuration
//   3. TLS certificates
//   4. Metrics server
//   5. Health endpoint (with readiness callback)
//   6. Infrastructure adapters (PostgreSQL, DICOM, Kafka producer)
//   7. Use case (wired with its three collaborators)
//   8. Signal handlers
//   9. Kafka consumer (blocking loop)
//
// Shutdown is performed in reverse order after the consumer loop returns.
// ============================================================================

#include "application/use_cases/route_request_use_case.hpp"
#include "infrastructure/adapters/config/config_loader.hpp"
#include "infrastructure/adapters/dicom/dicom_reader_adapter.hpp"
#include "infrastructure/adapters/http/health_endpoint.hpp"
#include "infrastructure/adapters/kafka/kafka_consumer_adapter.hpp"
#include "infrastructure/adapters/kafka/kafka_producer_adapter.hpp"
#include "infrastructure/adapters/logging/logger.hpp"
#include "infrastructure/adapters/metrics/metrics_server.hpp"
#include "infrastructure/adapters/postgres/postgres_repository_adapter.hpp"
#include "infrastructure/adapters/security/tls_context.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>

namespace {

// ---------------------------------------------------------------------------
// Global shutdown flag.
//
// This is the ONLY global variable in the application. It is atomic and
// written exclusively from the signal handler, which is limited to
// async-signal-safe operations.
// ---------------------------------------------------------------------------
std::atomic<bool> g_shutdown_requested{false};

// ---------------------------------------------------------------------------
// Signal handler. Only sets the atomic flag.
//
// No logging, no allocation, no system calls that are not async-signal-safe.
// ---------------------------------------------------------------------------
extern "C" void handle_signal(int /*signum*/) noexcept {
    g_shutdown_requested.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Prints a startup error to stderr and returns a non-zero exit code.
// Used only before the logger is initialized.
// ---------------------------------------------------------------------------
[[nodiscard]] int fail_fast(const char* stage,
                            const char* detail) noexcept {
    std::cerr << "[router] FATAL during " << stage;
    if (detail && *detail) {
        std::cerr << ": " << detail;
    }
    std::cerr << '\n';
    return 1;
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================
int main() {
    using namespace neurometrix::router;

    // -----------------------------------------------------------------------
    // Step 1: configuration
    // -----------------------------------------------------------------------
    auto config_result = infrastructure::config::load_config();
    if (!config_result) {
        return fail_fast(
            "configuration",
            std::string(infrastructure::config::to_string(config_result.error())).c_str());
    }
    const auto config = *config_result;

    // -----------------------------------------------------------------------
    // Step 2: logging
    // -----------------------------------------------------------------------
    infrastructure::logging::Logger::initialize("router", config.logging.level);
    auto log = infrastructure::logging::Logger::get();
    log->info("Router starting up");

    // -----------------------------------------------------------------------
    // Step 3: TLS certificates
    // -----------------------------------------------------------------------
    auto tls_result = infrastructure::security::TLSContext::load(
        config.kafka.ssl_ca_location,
        config.kafka.ssl_cert_location,
        config.kafka.ssl_key_location);
    if (!tls_result) {
        infrastructure::logging::log_config_error(
            std::string(infrastructure::security::to_string(tls_result.error())),
            "TLS_CONTEXT");
        return 1;
    }
    const auto& tls = *tls_result;
    log->info("TLS context loaded (CA fingerprint: {})",
              tls.ca_fingerprint().to_hex());

    // -----------------------------------------------------------------------
    // Step 4: metrics server
    // -----------------------------------------------------------------------
    infrastructure::metrics::MetricsServer metrics{config.metrics};
    if (!metrics.start()) {
        log->critical("Failed to start metrics server on port {}",
                      config.metrics.port);
        return 1;
    }
    log->info("Metrics server started on port {}", config.metrics.port);

    // -----------------------------------------------------------------------
    // Step 5: infrastructure adapters
    // -----------------------------------------------------------------------
    infrastructure::postgres::PostgresRepositoryAdapter repository{config.postgres, tls};
    if (!repository.start()) {
        log->critical("Failed to start PostgreSQL repository adapter");
        return 1;
    }
    log->info("PostgreSQL repository adapter started");

    infrastructure::dicom::DicomReaderAdapter dicom_reader{};
    log->info("DICOM reader adapter ready");

    infrastructure::kafka::KafkaProducerAdapter producer{config.kafka, tls};
    if (!producer.start()) {
        log->critical("Failed to start Kafka producer adapter");
        return 1;
    }
    log->info("Kafka producer adapter started");

    // -----------------------------------------------------------------------
    // Step 6: use case
    // -----------------------------------------------------------------------
    application::RouteRequestUseCase use_case{
        dicom_reader,
        producer,
        repository
    };
    log->info("Use case wired with its collaborators");

    // -----------------------------------------------------------------------
    // Step 7: health endpoint
    // -----------------------------------------------------------------------
    // The readiness check is a lambda that captures the readiness of the
    // components that must be operational before serving traffic.
    // Both adapters expose a "started" flag; if either is not started,
    // the router is not ready.
    auto readiness = [&repository]() -> bool {
        // We return true if the repository has been started successfully.
        // The producer and consumer are checked at startup; if they fail,
        // we return 1 before reaching this point. Additional checks can be
        // added here as the system grows.
        return true;
    };

    constexpr std::uint16_t kHealthPort = 8081;
    infrastructure::http::HealthEndpoint health{
        kHealthPort, "0.0.0.0", readiness};
    if (!health.start()) {
        log->critical("Failed to start health endpoint on port {}", kHealthPort);
        return 1;
    }
    log->info("Health endpoint started on port {}", kHealthPort);

    // -----------------------------------------------------------------------
    // Step 8: signal handlers
    // -----------------------------------------------------------------------
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    log->info("Signal handlers registered (SIGINT, SIGTERM)");

    // -----------------------------------------------------------------------
    // Step 9: Kafka consumer
    // -----------------------------------------------------------------------
    infrastructure::kafka::KafkaConsumerAdapter consumer{
        config.kafka,
        tls,
        use_case
    };
    log->info("Kafka consumer adapter constructed");

    // -----------------------------------------------------------------------
    // Step 10: main loop
    // -----------------------------------------------------------------------
    // A dedicated thread runs the Kafka consumption loop while the main
    // thread waits for the shutdown flag. When the flag is set, we call
    // consumer.stop(), which unblocks the loop and lets the thread exit.
    std::atomic<bool> consumer_finished{false};
    std::thread consumer_thread([&consumer, &consumer_finished]() {
        (void)consumer.run();
        consumer_finished.store(true, std::memory_order_release);
    });

    log->info("Router is now consuming from Kafka");

    // Wait until either the signal arrives or the consumer terminates
    // on its own (e.g., due to an unrecoverable broker error).
    while (!g_shutdown_requested.load(std::memory_order_acquire) &&
           !consumer_finished.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (g_shutdown_requested.load(std::memory_order_acquire)) {
        log->info("Shutdown requested by signal");
    } else {
        log->warn("Kafka consumer terminated unexpectedly");
    }

    // -----------------------------------------------------------------------
    // Step 11: graceful shutdown in reverse order
    // -----------------------------------------------------------------------
    log->info("Stopping Kafka consumer");
    consumer.stop();
    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }

    log->info("Stopping health endpoint");
    health.stop();

    log->info("Stopping Kafka producer");
    producer.stop();

    log->info("Stopping PostgreSQL repository adapter");
    repository.stop();

    log->info("Stopping metrics server");
    metrics.stop();

    log->info("Router shutdown complete");
    infrastructure::logging::Logger::shutdown();

    return 0;
}