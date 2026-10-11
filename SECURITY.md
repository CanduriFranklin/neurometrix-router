# Security Policy - Router Microservice

## Reporting a Vulnerability

Report suspected vulnerabilities privately to security@neurometrix.local.
Do not open public issues for security problems.

## Non-Negotiable Security Rules

1. All Kafka and PostgreSQL connections MUST use mTLS.
2. All SQL queries MUST use parameterized statements. String concatenation is forbidden.
3. No secrets in code, configuration files, or environment templates.
4. All external inputs (DICOM tags, protobuf payloads) MUST be validated before use.
5. Container images MUST run as non-root user.
6. Dependencies MUST be scanned with Trivy before deployment.
7. SBOM MUST be generated on every release.

## SQL Injection Protection

The router uses parameterized queries exclusively. Example:

    $1 = SELECT * FROM studies WHERE study_uid = $1

String concatenation of SQL is prohibited and enforced by static analysis.
