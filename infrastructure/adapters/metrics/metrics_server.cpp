// ============================================================================
// MetricsServer - Implementation
// ============================================================================
// See metrics_server.hpp for the contract.
// ============================================================================

#include "metrics_server.hpp"

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <memory>
#include <string>
#include <vector>

namespace neurometrix::router::infrastructure::metrics {

namespace {

// Bucket boundaries for the processing-duration histogram, in seconds.
// Chosen to cover the typical range of the router: fast for small 2D
// studies, up to several seconds for large 4D studies with DICOM reads.
[[nodiscard]] std::vector<double> processing_duration_buckets() {
    return {
        0.001,  //  1 ms
        0.005,  //  5 ms
        0.010,  // 10 ms
        0.050,  // 50 ms
        0.100,  // 100 ms
        0.500,  // 500 ms
        1.000,  //   1 s
        5.000,  //   5 s
        10.000  //  10 s
    };
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MetricsServer::Impl
// ---------------------------------------------------------------------------
struct MetricsServer::Impl {
    std::shared_ptr<prometheus::Registry> registry;
    std::unique_ptr<prometheus::Exposer>  exposer;

    // Counters
    prometheus::Counter* requests_received{nullptr};
    prometheus::Counter* requests_accepted{nullptr};
    prometheus::Counter* requests_rejected{nullptr};
    prometheus::Counter* kafka_publish_errors{nullptr};
    prometheus::Counter* dicom_read_errors{nullptr};
    prometheus::Counter* repository_errors{nullptr};

    // Histogram
    prometheus::Histogram* processing_duration{nullptr};

    bool started{false};
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MetricsServer::MetricsServer(const config::MetricsConfig& config) noexcept
    : pimpl_(std::make_unique<Impl>())
{
    try {
        pimpl_->registry = std::make_shared<prometheus::Registry>();

        // -----------------------------------------------------------------
        // Counters
        // -----------------------------------------------------------------
        auto& received = prometheus::BuildCounter()
            .Name("neurometrix_router_requests_received_total")
            .Help("Total routing requests received from Kafka")
            .Register(*pimpl_->registry);
        pimpl_->requests_received = &received.Add({});

        auto& accepted = prometheus::BuildCounter()
            .Name("neurometrix_router_requests_accepted_total")
            .Help("Total routing requests accepted (decision made)")
            .Register(*pimpl_->registry);
        pimpl_->requests_accepted = &accepted.Add({});

        auto& rejected = prometheus::BuildCounter()
            .Name("neurometrix_router_requests_rejected_total")
            .Help("Total routing requests rejected (business rejection)")
            .Register(*pimpl_->registry);
        pimpl_->requests_rejected = &rejected.Add({});

        auto& kafka_err = prometheus::BuildCounter()
            .Name("neurometrix_router_kafka_publish_errors_total")
            .Help("Total Kafka publishing failures")
            .Register(*pimpl_->registry);
        pimpl_->kafka_publish_errors = &kafka_err.Add({});

        auto& dicom_err = prometheus::BuildCounter()
            .Name("neurometrix_router_dicom_read_errors_total")
            .Help("Total DICOM read failures")
            .Register(*pimpl_->registry);
        pimpl_->dicom_read_errors = &dicom_err.Add({});

        auto& repo_err = prometheus::BuildCounter()
            .Name("neurometrix_router_repository_errors_total")
            .Help("Total repository failures")
            .Register(*pimpl_->registry);
        pimpl_->repository_errors = &repo_err.Add({});

        // -----------------------------------------------------------------
        // Histogram
        // -----------------------------------------------------------------
        auto& duration = prometheus::BuildHistogram()
            .Name("neurometrix_router_processing_duration_seconds")
            .Help("End-to-end processing time per routing request")
            .Register(*pimpl_->registry);
        pimpl_->processing_duration = &duration.Add(
            {}, processing_duration_buckets());

        // -----------------------------------------------------------------
        // Exposer (HTTP server)
        // -----------------------------------------------------------------
        const std::string bind_address =
            std::to_string(config.port);
        // Exposer accepts either "host:port" or just ":port". Passing
        // "0.0.0.0:9090" ensures we listen on all interfaces.
        const std::string endpoint = "0.0.0.0:" + bind_address;
        pimpl_->exposer = std::make_unique<prometheus::Exposer>(endpoint);
        pimpl_->exposer->RegisterCollectable(pimpl_->registry);
    } catch (...) {
        // Leave pimpl_ in a safe state. start() will return false.
    }
}

// ---------------------------------------------------------------------------
// Destructor
// ---------------------------------------------------------------------------
MetricsServer::~MetricsServer() {
    stop();
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------
bool MetricsServer::start() noexcept {
    if (!pimpl_ || !pimpl_->exposer) {
        return false;
    }
    pimpl_->started = true;
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void MetricsServer::stop() noexcept {
    if (!pimpl_) return;
    try {
        pimpl_->exposer.reset();
        pimpl_->registry.reset();
        pimpl_->started = false;
    } catch (...) {
        // Swallow.
    }
}

// ---------------------------------------------------------------------------
// Recording methods
// ---------------------------------------------------------------------------
void MetricsServer::record_request_received() noexcept {
    if (pimpl_ && pimpl_->requests_received) {
        pimpl_->requests_received->Increment();
    }
}

void MetricsServer::record_request_accepted() noexcept {
    if (pimpl_ && pimpl_->requests_accepted) {
        pimpl_->requests_accepted->Increment();
    }
}

void MetricsServer::record_request_rejected() noexcept {
    if (pimpl_ && pimpl_->requests_rejected) {
        pimpl_->requests_rejected->Increment();
    }
}

void MetricsServer::observe_processing_duration(double seconds) noexcept {
    if (pimpl_ && pimpl_->processing_duration) {
        pimpl_->processing_duration->Observe(seconds);
    }
}

void MetricsServer::record_kafka_publish_error() noexcept {
    if (pimpl_ && pimpl_->kafka_publish_errors) {
        pimpl_->kafka_publish_errors->Increment();
    }
}

void MetricsServer::record_dicom_read_error() noexcept {
    if (pimpl_ && pimpl_->dicom_read_errors) {
        pimpl_->dicom_read_errors->Increment();
    }
}

void MetricsServer::record_repository_error() noexcept {
    if (pimpl_ && pimpl_->repository_errors) {
        pimpl_->repository_errors->Increment();
    }
}

} // namespace neurometrix::router::infrastructure::metrics