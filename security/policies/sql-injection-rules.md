# SQL Injection Prevention Policy

## Rules

1. NEVER concatenate user input into SQL queries.
2. ALWAYS use parameterized queries ($1, $2, ...).
3. ALWAYS validate DICOM fields before storing.
4. ALWAYS use a connection pool with TLS enforced.
5. NEVER log raw SQL with user data.

## Enforcement

- Static analysis via clang-tidy and cppcheck.
- Code review checklist.
- Unit tests with malicious payloads in tests/security/.
