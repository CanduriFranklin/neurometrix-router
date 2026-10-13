# Neurometrix Router

Intelligent routing microservice for MRI dimensionality detection (2D / 3D / 4D).

## Responsibilities

1. Consume DICOM studies from the API Gateway.
2. Read metadata to detect dimensionality (2D, 3D, or 4D).
3. Publish RoutingRequest messages to the appropriate Kafka topic.
4. Never process images itself. Never talk to other cores directly.

## Architecture

- Hexagonal Architecture (Ports and Adapters)
- Domain-Driven Design
- Event-driven via Apache Kafka
- C++23

## Non-Negotiable Rules

1. No code, files, or logic shared with other microservices.
2. Communication exclusively through Kafka.
3. Independent deployment, independent failure domain.

## Build

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

## Test

    ctest --test-dir build --output-on-failure

## Run

    ./build/router
