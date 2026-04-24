## Project Standards

### C++17

- **Naming**: `PascalCase` for classes, `snake_case` for functions/variables, trailing `_` for members, `kPascalCase` or `UPPER_CASE` for constants
- **Headers**: `#pragma once`
- **Include order**: corresponding header → C++ stdlib → third-party → project headers
- **RAII**: Always use RAII wrappers for ODBC handles — never raw handles in test code
- **Error handling**: Always extract full ODBC diagnostic records on failure via `SQLGetDiagRec`

### CMake 3.20+

- Modern target-based CMake (`target_include_directories`, `target_link_libraries`)
- `FetchContent` for external dependencies
- Out-of-source builds only (`cmake -B build`)

### Google Test

- Test files: `test_*.cpp` in `tests/`
- Test fixtures: `*Test` classes
- Test cases: `TEST_F(FixtureName, TestName)` or `TEST(SuiteName, TestName)`

### Git Commits

Conventional commits: `feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `build:`, `ci:`

---

## Architecture Guidelines

### Adding New ODBC Tests

1. Create test class in `src/tests/` inheriting from `TestBase`
2. Use `*_tests.hpp` / `*_tests.cpp` naming
3. Namespace: `odbc_crusher::tests`
4. Return `std::vector<TestResult>` from `run()`
5. Register in `src/main.cpp` and add to `src/tests/CMakeLists.txt`
6. Add GTest unit tests in `tests/` and register in `tests/CMakeLists.txt`
7. Update mock driver if new ODBC functions are exercised

### Adding Dependencies

1. Prefer `FetchContent` for header-only or small libraries
2. Use `find_package()` for system packages
3. Update PROJECT_PLAN.md with the rationale

---

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## ODBC References

- [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
- [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- [unixODBC](http://www.unixodbc.org/)

---

## Quality Checklist

Before considering work complete:

- [ ] Code compiles without warnings on Windows and Linux
- [ ] All unit tests pass (`ctest --test-dir build`)
- [ ] RAII used for all ODBC handles
- [ ] Error handling extracts full diagnostic records
- [ ] PROJECT_PLAN.md updated (if applicable)
- [ ] Git commit message follows conventional commits

---

## Project Philosophy

This tool is for ODBC driver **developers**, not end users. It should be: **thorough** (test edge cases), **clear** (explain what went wrong with SQLSTATE codes), **helpful** (suggest fixes with spec references), **reliable** (never crash — handle all ODBC errors gracefully), **direct** (use native ODBC API, no abstractions that hide behavior), and **fast** (C++ allows microsecond timing).
