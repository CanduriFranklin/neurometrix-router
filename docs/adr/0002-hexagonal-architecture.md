# ADR 0002: Hexagonal Architecture

## Status

Accepted

## Context

The router must be testable, decoupled from infrastructure, and easy to evolve.

## Decision

Use Hexagonal Architecture with three layers:

- domain: pure business logic
- application: use cases and ports
- infrastructure: adapters (Kafka, PostgreSQL, DICOM)

## Consequences

- Domain tests run without Kafka or PostgreSQL.
- Adapters can be swapped without touching the domain.
