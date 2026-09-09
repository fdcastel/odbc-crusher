## Project Standards

### C++17

- **Naming**: `PascalCase` for classes, `snake_case` for functions/variables, trailing `_` for members, `kPascalCase` or `UPPER_CASE` for constants
- **Headers**: `#pragma once`
- **Include order**: corresponding header → project headers → third-party → C++ stdlib
  (C14: the rule used to say the opposite, and **all 23** probe `.cpp` files
  did it this way. A rule that no file in the tree follows is not a standard,
  it is a note about someone's preferences — and here the code is right: on
  Windows `<windows.h>` must precede `<sql.h>`, and the project headers that
  wrap that ordering have to come first for it to hold. The rule now describes
  the code rather than asking for 23 files of churn.)
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
6. Add an **e2e scenario** in `tests/e2e/test_e2e_scenarios.cpp` with a mock
   configuration that makes the new probe *fail* — a probe with no failing
   configuration has not been shown to detect anything. (H4: this step used
   to say "add GTest unit tests in `tests/`"; the per-category wrappers it
   refers to were retired in `cb2f639`, so following it produced a test file
   with nothing to register it against. `tests/unit/` is still the right
   place for a pure function — see `test_guarded_buffer.cpp`.)
7. Update mock driver if new ODBC functions are exercised
8. **A probe's `expected` string is a contract.** If the body cannot fail when
   that expectation is violated, the probe is not finished — rename it or
   strengthen it. (IMPROVEMENT_PLAN_V2 **Q4**.) This is not a hypothetical
   tidiness rule: a sweep for the shape found `test_cursor_scrollable_attr`
   setting an attribute and never reading it back, which is precisely the
   defect PR #304 fixed; `test_bindparam_null_indicator` binding a NULL and
   printing a return code without ever fetching the value; `test_rowset_size`
   setting a rowset size and never fetching a rowset; `test_ird_after_prepare`
   reading `SQL_DESC_COUNT` and stopping, so a descriptor full of unfilled
   records passed; `test_statement_attributes` calling itself "various
   attributes" and reading five; and `test_param_status_per_row_partial_failure`
   promising "the row that violates a server-side constraint" while inserting
   five valid rows. Every one of them reported a pass over the bug it was named
   for, and **a probe that cannot fail is worse than no probe** — no probe is an
   admitted gap, a passing one is a false assurance.

   The tell is a body that ends at `SQL_SUCCEEDED(rc)` while its name or
   `expected` text promises something about a *value*. When you write one, ask
   what the driver would have to do wrong for this probe to notice, and if the
   answer is "return an error", the probe is testing the return code and should
   say so.

### Preserve the primary exception on rollback-path failure

When a `catch (const OdbcError& e)` block issues a rollback (via
`SQLEndTran(SQL_HANDLE_DBC, ..., SQL_ROLLBACK)` or the equivalent)
and the rollback itself fails, **do not let the rollback's
diagnostics overwrite the original exception**. Capture the primary
`e.what()` / `e.format_diagnostics()` first, then attempt the
rollback and append its outcome to a separate diagnostic slot if
relevant. Typical mistake:

```cpp
} catch (const core::OdbcError& e) {
    // BAD: SQLEndTran can throw its own HY010 on a broken txn,
    // and the new exception hides the real cause (e).
    SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
    throw;
}
```

`SQLEndTran` here does not currently throw in the project (we call
the C API directly), but the same pitfall shows up in any
driver-caller code where a cleanup step raises after the primary
error — the rollback's `HY010 "Function sequence error"` is what the
user then sees, not the underlying SQL error. Root-cause text
belongs to the first failure, not the cleanup.

### Adding Dependencies

1. Prefer `FetchContent` for header-only or small libraries
2. Use `find_package()` for system packages
3. Add a row to `docs/IMPROVEMENT_PLAN.md` with the rationale

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

- [ ] Code compiles without warnings on Windows, Linux and macOS
      (enforced: `/WX` on MSVC, `-Werror` on GCC/Clang — E5. The non-MSVC
      set deliberately omits `-Wold-style-cast`, `-Wsign-conversion` and
      `-Wuseless-cast`; see `cmake/CompilerWarnings.cmake` for why.)
- [ ] All unit tests pass (`ctest --test-dir build`)
- [ ] RAII used for all ODBC handles
- [ ] Error handling extracts full diagnostic records
- [ ] `docs/IMPROVEMENT_PLAN.md` row added or updated, in this same commit
      (H11: this line said "PROJECT_PLAN.md updated", which has been the wrong
      file since **H8** moved the work queue into `docs/`. `PROJECT_PLAN.md` is
      now a stable architecture document whose §3 just points at the plan, so
      following this checklist led you to edit the one file that does not track
      work. The plan's own rules are at the top of `docs/IMPROVEMENT_PLAN.md`:
      update it in the same commit as the change, fill the Commit column, and
      never delete a row.)
- [ ] Git commit message follows conventional commits, with the plan row's ID
      in parentheses — e.g. `fix(mock): … (D85)`

---

## Project Philosophy

This tool is for ODBC driver **developers**, not end users. It should be: **thorough** (test edge cases), **clear** (explain what went wrong with SQLSTATE codes), **helpful** (suggest fixes with spec references), **reliable** (never crash — handle all ODBC errors gracefully), **direct** (use native ODBC API, no abstractions that hide behavior), and **fast** (C++ allows microsecond timing).
