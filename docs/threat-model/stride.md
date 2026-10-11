# STRIDE Threat Model - Router

| Category           | Threat                              | Mitigation                          |
|--------------------|-------------------------------------|-------------------------------------|
| Spoofing           | Fake DICOM sender                   | mTLS, API Gateway authentication    |
| Tampering          | Modified DICOM metadata             | Hash verification, signature        |
| Repudiation        | Denied study submission             | Audit log in PostgreSQL             |
| Information Discl. | PHI leak in logs                    | Hashed patient IDs, JSON logs       |
| DoS                | Flood of routing requests           | Rate limiting at API Gateway        |
| Elevation          | Container escape                    | Non-root user, read-only FS         |
