# Security Policy

## Reporting a Vulnerability

If you discover a security issue in ODBC Crusher (the CLI tool or the
bundled Mock ODBC Driver), please **do not open a public GitHub issue**.

Instead, email the maintainer directly:

**fdcastel@dataweb.com.br**

Include:

- A description of the issue
- Reproduction steps (connection string, driver, OS, build configuration)
- The impact you see (crash, information disclosure, privilege escalation, etc.)
- Any proposed fix, if you have one

You can expect an initial response within a few business days.

## Scope

This project is a conformance-testing tool aimed at ODBC driver developers.
Its typical deployment is a developer workstation or CI agent, not a
multi-user server. That said, the following classes of issues are in scope:

- Crashes or memory corruption in `odbc-crusher` triggered by a malicious
  driver (the tool loads arbitrary ODBC drivers by design)
- Crashes or memory corruption in the Mock ODBC Driver
- Build-time or install-time code execution through crafted CMake / registry
  artifacts shipped by this repo
- Unintended credential or filesystem access through the JSON / console
  reporters

Out of scope:

- Vulnerabilities in third-party ODBC drivers the tool is pointed at —
  those belong to the respective driver's maintainers.
- Denial-of-service by supplying obviously-invalid configuration.
