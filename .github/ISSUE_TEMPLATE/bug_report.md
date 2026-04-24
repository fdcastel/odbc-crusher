---
name: Bug report
about: Report a crash, incorrect result, or spec-violation in odbc-crusher or the mock driver
title: ''
labels: bug
---

## Summary

<!-- One-line description. -->

## Environment

- ODBC Crusher version (output of `odbc-crusher --version`):
- OS and version:
- Driver under test (name, version, architecture):
- Driver Manager (Windows DM / unixODBC / iODBC):
- Build configuration (prebuilt release binary, or self-built Debug/Release):

## Reproduction

<!-- Connection string (redact secrets), CLI arguments, and the exact
     command you ran. -->

```text
odbc-crusher "Driver={...};..." -v
```

## Expected behavior

<!-- What should have happened, per the ODBC specification or previous
     behavior? -->

## Actual behavior

<!-- What happened instead? Paste the relevant portion of the report
     (console or JSON), plus any stderr output. -->

## Additional context

<!-- Stack trace, driver's own diagnostic trace, anything else
     helpful. -->
