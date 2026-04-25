# Contributing to ODBC Crusher

Thanks for your interest in improving ODBC Crusher! Here's how to get started.

## Development Setup

1. **Clone** the repository:
   ```bash
   git clone https://github.com/fdcastel/odbc-crusher.git
   cd odbc-crusher
   ```

2. **Build** (Debug):
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --config Debug
   ```

3. **Run unit tests**:
   ```bash
   ctest --test-dir build -C Debug --output-on-failure
   ```

## Project Structure

```
src/
  core/           RAII wrappers for ODBC handles (Environment, Connection, Statement, Error)
  discovery/      Driver introspection (DriverInfo, TypeInfo, FunctionInfo)
  tests/          ODBC conformance test suites (the tests that odbc-crusher runs)
  reporting/      Console and JSON output formatters
  cli/            CLI argument parsing
  main.cpp        Entry point
tests/            GTest unit tests for the above
mock-driver/      A mock ODBC driver DLL for CI testing
cmake/            CMake modules (compiler warnings, platform, version)
include/          Public headers (version.hpp)
```

## Adding a New Test Category

1. Create `src/tests/my_tests.hpp` and `src/tests/my_tests.cpp`, following the existing pattern (inherit from `TestBase`).
2. Add the `.cpp` to `src/tests/CMakeLists.txt`.
3. Register the category in `src/main.cpp` (add to the `categories` vector).
4. Optionally add an E2E scenario in `tests/e2e/test_e2e_scenarios.cpp` if a specific mock-driver configuration should produce a deterministic outcome for one of your tests.
5. Build and run `ctest` to verify.

Test layout:
- `tests/unit/` — GTest unit tests of project plumbing (RAII handles, error parsing, crash guard, discovery). One test per file.
- `tests/e2e/` — Subprocess harness that spawns the `odbc-crusher` binary against the mock driver and asserts on per-test JSON outcomes. New end-to-end scenarios go in `test_e2e_scenarios.cpp`.

Each test method should return a `TestResult` with:
- A descriptive `test_name`
- The ODBC function being tested
- Expected and actual behavior
- An ODBC spec reference
- A severity and conformance level

## Code Style

- C++17 throughout.
- RAII for all ODBC handles — no manual `SQLFreeHandle` calls in test code.
- Naming: `snake_case` for functions and variables, `PascalCase` for classes.
- Keep test methods self-contained. Each test should allocate its own statement handle.

## Mock ODBC Driver

The `mock-driver/` directory contains a fully functional ODBC driver DLL that supports configurable behavior via connection string parameters. Use it for writing tests that don't require a real database.

To build and test the mock driver:

```bash
cmake -B mock-driver/build -S mock-driver
cmake --build mock-driver/build --config Debug
ctest --test-dir mock-driver/build -C Debug --output-on-failure
```

## Static Analysis

CI runs **CodeQL** (`security-and-quality` ruleset, C++) on every push and PR — findings appear in the GitHub Security tab and as inline review comments. Sanitizers (ASan + UBSan) cover the runtime side (`.github/workflows/ci.yml` → `sanitizers` job).

The repo also ships a curated `.clang-tidy` config for IDE/local use. To run it from the command line:

```bash
cmake -B build-tidy -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build-tidy src/main.cpp src/core/*.cpp
```

The config is intentionally not wired into a CI job yet — see `.clang-tidy` for the rationale.

## Pull Requests

- Keep PRs focused on a single change.
- Make sure `ctest` passes locally before opening a PR.
- CI runs on Windows, Linux, and macOS — check the workflow results.

## Reporting Issues

Please include:
- The ODBC driver name and version you're testing.
- The full command line you used.
- The output (or relevant excerpt).
- Your OS and compiler version.
