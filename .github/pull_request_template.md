## Summary

<!-- One-line description of what this change does. -->

## Why

<!-- Motivation: which IMPROVEMENT_PLAN.md / PROJECT_PLAN.md item,
     which real-driver bug, or which external request does this address? -->

## Scope

<!-- Which components change? Application (src/), mock driver (mock-driver/),
     tests, CI, docs? -->

## Test plan

<!-- How was this verified? -->

- [ ] `cmake --build build --config Debug` succeeds
- [ ] `ctest --test-dir build -C Debug` passes
- [ ] Exercised against the Mock ODBC Driver (if behaviour changed)
- [ ] Exercised against at least one real driver (if the change is
      not purely internal)
