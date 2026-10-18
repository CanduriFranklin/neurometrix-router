# Design Patterns

## 1. Domain-Driven Design (DDD)
The DimensionalityDetector is a Domain Service that analyzes metadata and validates it.

## 2. Ports and Adapters
- Inbound Port: RouteRequestUseCase.
- Outbound Ports: IDicomReader, IMessagePublisher.

## 3. Resilience
If metadata is inconsistent, it publishes to router.requests.rejected instead of crashing.
