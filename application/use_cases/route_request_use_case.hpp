// ============================================================================
// RouteRequestUseCase - Declaration
// ============================================================================
#pragma once

#include "../../domain/repositories/i_routing_repository.hpp"
#include "../ports/inbound/i_route_request_use_case.hpp"
#include "../ports/outbound/i_dicom_reader.hpp"
#include "../ports/outbound/i_message_publisher.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace neurometrix::router::application {

class RouteRequestUseCase : public IRouteRequestUseCase {
public:
    RouteRequestUseCase(
        const IDicomReader&         dicom_reader,
        IMessagePublisher&          publisher,
        domain::IRoutingRepository& repository
    ) noexcept;

    [[nodiscard]] std::expected<RouteRequestResult, UseCaseError>
    execute(const RouteRequestInput& input) noexcept override;

private:
    std::expected<void, UseCaseError>
    validate_input(const RouteRequestInput& input) const noexcept;

    [[nodiscard]] RouteRequestResult
    make_rejection(std::string reason_code, std::string reason_detail) noexcept;

    [[nodiscard]] RouteRequestResult
    make_success(
        const domain::StudyUid& study_uid,
        domain::Dimensionality  dimensionality,
        std::string             target_topic_suffix,
        std::uint64_t           processing_time_ms
    ) noexcept;

    const IDicomReader&         dicom_reader_;
    IMessagePublisher&          publisher_;
    domain::IRoutingRepository& repository_;
};

} // namespace neurometrix::router::application