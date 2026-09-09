# Is odbc-crusher finding the bugs we fix?

**A controlled before/after against one driver, two builds.**

| | |
|---|---|
| Baseline | Firebird ODBC Driver **3.0.1.21** (official release) — source `FirebirdSQL/firebird-odbc-driver` @ [`dee624f`](https://github.com/FirebirdSQL/firebird-odbc-driver/commit/dee624fac13182f569532a5785e32eb5d27bed98) |
| Candidate | Firebird ODBC Driver **3.5.1-rc2** — upstream `master` @ `35fae3e` plus thirteen open pull requests |
| Crusher | `68630c6`, same binary for both |
| Server | Firebird 5.0.3 via PSFirebird, `windows-2022`, `CHARSET=UTF8` |
| Runs | [official](https://github.com/fdcastel/odbc-crusher/actions/runs/34367166127) · [patched](https://github.com/fdcastel/odbc-crusher/actions/runs/34367182766) |
| Provenance | `OK` on both, from the installed DLL's VERSIONINFO — `3.0.1.21` and `3.5.1.0` |

The two runs are identical in every respect except the driver: same server, same
database path, same connection string character for character, same crusher
build from the same CI run. That is what makes the difference between the two
reports attributable to the driver and nothing else.

---

## Scoreboard

| | official 3.0.1.21 | patched 3.5.1-rc2 |
|---|---|---|
| Probes scored | 193 | 197 |
| Passed | 146 | 171 |
| Failed | 33 | 13 |
| Errors | 1 | 0 |
| **Pass rate** | **75.6 %** | **86.8 %** |
| Crashed categories | **1 — Descriptor Tests** | 0 |
| Findings | 44 | 23 |

**20 probes moved FAIL → PASS. None regressed.**

The four-probe difference in the totals is the crash: on 3.0.1.21 the Descriptor
Tests category died and took its five probes with it, so the baseline's pass
rate is *optimistic* — it is computed over the probes that survived.

---

## Part 1 — What crusher caught

Twenty probes moved, but they are **three driver bugs**, not twenty. Counting
findings instead of causes is the single easiest way to overstate a tool like
this.

### 1. Numeric → text parameter truncation (PR #292) — 14 probes

Every `test_bindparam_<numeric>_to_varchar/char_roundtrip` probe failed on
3.0.1.21 and passes on rc2, all with one signature:

```
test_bindparam_int_to_varchar_roundtrip
  actual: Round-trip value mismatch: row 10: expected '10' got '1'

test_bindparam_double_to_varchar_fractional_roundtrip
  actual: row 1: got '1', expected ~1.500000  …  row 10: got '1', expected ~10.500000
```

That is exactly the defect PR #292 describes: `setSqlLen(actual_length)` shrinks
the sqlvar after the first execute, a rebind re-describes the parameter from the
shrunken sqlvar, and `record->length` collapses to the length of the *first*
value. Rows 1–9 are single-digit and survive untouched; row 10 loses a digit and
`1.5` loses its fraction — with `SQL_SUCCESS` at every step and a correct row
count.

This is the strongest possible result for the tool. Silent data loss with a
success return is the class of bug that has no other detector, and crusher
found it fourteen times over, with the exact truncation signature and the exact
row at which it starts.

### 2. The parameter-array executor is chosen at prepare time (PR #308) — 6 probes

The entire Array Parameter category failed on 3.0.1.21 and passes on rc2:

```
test_column_wise_array_binding    Array execution with PARAMSET_SIZE=3 succeeded (ret=0),
                                  but the rows are not there: COUNT(*) = 1 but expected 3
test_row_wise_array_binding       got {9991} instead of {9991, 9992}
test_param_status_array           status array: [0xffff, 0xffff, 0xffff]      (untouched)
test_params_processed_count       params_processed=0 (expected 4)
test_param_operation_array        params_processed=0; status: [0xffff × 4]
test_param_status_per_row_partial_failure   succ=0 err=0 other=5
```

One root cause: crusher sets `SQL_ATTR_PARAMSET_SIZE` *after* `SQLPrepare`,
which the specification allows and which pyodbc's `fast_executemany` does, and
3.0.1.21 had already bound itself to the single-row executor. One row is
inserted, `SQL_SUCCESS` is returned, and the status array and processed count
are never written. `0xffff` in the output above is crusher's own sentinel — the
probes are correctly reading their own initialisation back, which is why they
can tell "the driver wrote nothing" from "the driver wrote the wrong thing".

### 3. `SQLCopyDesc` access violation (PR #297) — a whole category

```
Descriptor Tests (DRIVER CRASH)   ERROR / CRITICAL
  actual: Access violation (0xC0000005) - likely a bug in the ODBC driver
```

3.0.1.21 crashes; rc2 runs all five descriptor probes clean. This one is already
settled history: the fix commit's own message reads *"Reported by odbc-crusher
v3.5.0-rc1 stress test"*, and the driver repository carries
`tests/test_phase7_crusher_fixes.cpp`, a file whose entire purpose is
crusher-identified bugs. It lists five (OC-1 … OC-5); OC-1 is this one, and it
is the only one of the five fixed in rc2.

---

## Part 2 — What crusher could not have caught

Seventeen behavioural fixes separate the two builds. Crusher detected **three**.
The remaining fourteen are not near-misses — for most of them there is no probe
that touches the API at all. Each row below was verified against the probe
source, not inferred from the reports.

| Fix | Detected? | Why |
|---|---|---|
| **#292** numeric → text parameter | ✅ | 14 probes, exact signature |
| **#308** executor chosen at prepare time | ✅ | 6 probes |
| **#297** `SQLCopyDesc` SIGSEGV | ✅ | category crash |
| **#310** `SQL_ATTR_PARAM_OPERATION_PTR` | ⚠️ capable | `test_param_operation_array` asserts `SQL_PARAM_UNUSED` correctly — but on 3.0.1.21 it failed for #308's reason, so this pair cannot show it detecting #310 *independently* |
| **#308** element-0 stride (the *first* defect) | ❌ | `test_column_wise_array_binding` binds the exact triggering shape — `SQL_C_SLONG` with `BufferLength = 0` — then verifies only the `NAME` column. `verify_rows_persisted` runs `SELECT <val> … ORDER BY <pk>` and never reads the PK. Under the bug the IDs collapse to one value and the strings step correctly, so the probe passes over a corrupted column — and the `ORDER BY` over equal keys is the same trap PR #308 documents in the original report |
| **#313 / #309** dangling bind-offset after a mid-array throw | ❌ | `test_param_status_per_row_partial_failure` executes once and reads the status array. It never reuses the handle afterwards, which is where the damage is (access violation, or a garbage row) |
| **#311** cumulative `SQLRowCount` after an array execute | ❌ | `SQLRowCount` is never called in `array_param_tests.cpp` (0 occurrences), and `SQL_PARAM_ARRAY_ROW_COUNTS` / `SQL_PARAM_ARRAY_SELECTS` appear nowhere in `src/` — so neither the behaviour nor the driver's own claim about it is read |
| **#303 / #301** `SQLPrepare` discards `ROWS_FETCHED_PTR` / `ROW_STATUS_PTR` | ❌ | Both tokens are absent from `src/` entirely |
| **#304** eight settable attributes unreadable via `SQLGetStmtAttr` | ❌ | `test_statement_attributes` reads five attributes, none of them these. `SQL_ATTR_CURSOR_SCROLLABLE` is *set* by `test_cursor_scrollable_attr` and never read back — which is the whole bug |
| **#315 / #306** column-wise rowsets ignore `SQL_ATTR_ROW_BIND_OFFSET_PTR` | ❌ | `ROW_BIND_OFFSET` and `PARAM_BIND_OFFSET` appear nowhere in `src/` |
| **#314 / #307** `SQL_ATTR_KEYSET_SIZE` overwrites the rowset size | ❌ | `KEYSET_SIZE` appears nowhere in `src/` |
| **#317 / #316** implementation descriptors and `SQLColAttribute` (7 sub-defects) | ❌ | `SQL_DESC_CONCISE_TYPE`, `SQL_DESC_NAME`, `SQL_DESC_NULLABLE`, `SQL_DESC_DATETIME_INTERVAL_CODE` are never read anywhere. `SQLColAttribute` is called only for `SQL_DESC_UNSIGNED`. `test_ird_after_prepare` reads `SQL_DESC_COUNT` and passes on any success — it does not even check the count is right, let alone that the records describe anything |
| **#302 / #300** numeric parameter sent as NULL after a character-typed bind | ❌ | There is no "NULL then value on the same parameter" probe. `test_bindparam_null_indicator` binds one NULL, executes, and checks the return code — it does not even read the value back |
| **#296 / #295** `SQL_C_GUID` parameter binding corrupts the wire | ❌ | `SQL_C_GUID` appears once, in `test_guid_type`, on the **output** path (`SQLGetData`). The bug is on the input path |
| **#294** `SQL_DBMS_VER` returned the engine version, not the product version | ❌ | The value is printed in the report header and never asserted. `SQL_DBMS_VER` is used only as a subject for buffer-length and Unicode probes |
| **#298** garbled diagnostic record on a failed `SQLDriverConnect` | ❌ | Crusher never attempts a connect that fails. Its only `SQLDriverConnect` probe reconnects an already-connected handle |
| **#279** null-indicator offset in array binding | — | Predates the pair's baseline in effect; the array category was failing wholesale on 3.0.1.21 for #308's reason |

### The shape of the gap

Ten of the fourteen misses fall into two holes:

**There is no block-cursor data path.** `test_rowset_size` sets
`SQL_ATTR_ROW_ARRAY_SIZE = 100`, reads it back, and passes. It never fetches a
rowset. Nothing in the suite binds an array of columns, fetches more than one
row at a time, reads a rows-fetched counter, inspects a row-status array, or
applies a bind offset. That is four of the thirteen PRs (#301, #306, #307, and
half of #316) with no reachable probe — and block cursors are how every ODBC
reporting tool reads bulk data.

**The descriptor API is touched but never interrogated.** Five probes obtain
descriptor handles and read `SQL_DESC_COUNT`. The seven defects in #316 are all
about what the *records* say — type codes, names, nullability, length fields at
full width — and crusher never asks. It found the crash in that area precisely
because a crash needs no assertion.

---

## Part 3 — What crusher found that the thirteen PRs do not fix

Thirteen probes fail on **both** builds. Twelve are live bugs in rc2 and are
the return on the exercise that has nothing to do with the fixes under test. The
thirteenth turned out to be a defect in the probe, and is struck through below —
left in place rather than deleted, because it was published as a driver finding
and a correction is worth more than a quiet edit.

| Cluster | Evidence |
|---|---|
| **`{fn …}` translation is incomplete** | `{fn LENGTH}`, `{fn YEAR}`, `{fn MONTH}`, `{fn DAYOFWEEK}`, `{fn DATABASE}` are advertised in `SQLGetInfo(SQL_STRING_FUNCTIONS / SQL_TIMEDATE_FUNCTIONS / SQL_SYSTEM_FUNCTIONS)` and fail with `-104 Token unknown` when executed. `test_scalar_function_claim_vs_execute` is the probe that matters here: it cross-checks the driver's own claim against execution, and reports `STRING:6/7 NUMERIC:4/4 TIMEDATE:3/5 SYSTEM:1/2` |
| ~~**`{CALL …}` is not translated at all**~~ — **withdrawn, this was a crusher bug** | The probe hard-coded the identifiers `proc` and `func`, which exist in no database, and counted an error return as "not translated". Firebird resolves the procedure while translating — it must choose between `execute procedure p` and `select * from p` — so it answered `Unknown procedure 'PROC'` seven times. Both triage reports classified this correctly as `BUG_IN_CRUSHER`; this summary did not, and stated it as a driver defect. With a procedure the catalog actually contains, the driver translates **7/7** (`IMPROVEMENT_PLAN_V2` **R3**). The count of live bugs in this section is **12**, not 13. |
| **`SQL_DIAG_ROW_COUNT` is a 32-bit write into a 64-bit slot** | `SQL_DIAG_ROW_COUNT = -4294967296` — that is `0xFFFFFFFF00000000`: the low half written, the high half left as the caller's `-1`. Sharper than the driver's own note, which records only that the field "stays 0". This is OC-2 from the driver's own crusher-fixes file, still open, and the triage report's first punch-list item |
| **`SQL_ATTR_ASYNC_ENABLE` accepted then ignored** | set returns success, get reports OFF. OC-4, still open |
| **Missing spec-mandated error checks** | `SQLCloseCursor` succeeds with no cursor open (should be `24000`); `SQLSetConnectAttr` accepts attribute `99999` (should be `HY092`) |
| **Every engine exception is `HY000`** | a syntax error should be `42000` |
| **`PARAMSET_SIZE = 1` writes neither output** | `processed=0; status=65535`. The array executor runs only for paramset > 1, so the single-set case still writes nothing — a gap none of the thirteen PRs mentions |
| **Truncated `SQLGetInfo` reports the written length, not the length available** | OC-5, still open |

Four of the five bugs in the driver's own `test_phase7_crusher_fixes.cpp` are
still skipped. Crusher is still reporting them, run after run.

---

## Part 4 — What this exercise found in crusher itself

Four defects, all found by pointing the tool at a real driver pair on a platform
it had not been run against.

1. **Four probes inserted the wrong number of columns, and the cleanup then hung
   the entire run** (`H18`, [`1df48e1`](https://github.com/fdcastel/odbc-crusher/commit/1df48e1)).
   `RoundTripTableGuard` creates `(ID, VAL)`; four sites inserted one value.
   Firebird answers `-804`, the throw skips each probe's rollback, and the
   guard's `DROP TABLE` — destroyed *before* the sibling connection, because it
   is declared after it — then waits on that sibling's open transaction with
   Firebird's default infinite lock wait. Both of the first two stress-test runs
   died at the 570 s cap having produced nothing. `test_reconnected_handle_is_usable`,
   `test_uncommitted_row_isolation` and `test_disconnect_with_open_transaction`
   had therefore never tested what they claim, against any driver.

2. **The mock was more permissive than every real database**
   ([`a31ad4c`](https://github.com/fdcastel/odbc-crusher/commit/a31ad4c)), which
   is why (1) survived. `mock_data.cpp` validated an INSERT's value count only
   when the statement named a column list; the column-less form padded the row
   with NULL and returned success. The e2e suite runs against the mock, so a
   probe carrying a malformed statement passed CI and failed only against a
   server. **A mock that is kinder than reality hides probe bugs rather than
   driver bugs, which is the opposite of its job.**

3. **A verdict that moved between two identical runs** (`H19`,
   [`68630c6`](https://github.com/fdcastel/odbc-crusher/commit/68630c6)).
   `test_rapid_cursor_lifecycle` graded a leak on `last_10 > first_10 * 10` with
   no absolute floor; on a macOS runner the baseline was 25 µs *for ten cycles*,
   so a scheduling hiccup produced a `WARNING` where the previous run produced
   `INFO`. The project's own `TwoRunsDifferOnlyInTimings` caught it and turned
   master red. Not a flaky test — a non-deterministic verdict.

4. **Two `CRITICAL` conversion cells never run on Firebird** (from the rc2
   triage, `RC10`). `test_bindparam_int_to_wvarchar_roundtrip` and its `bigint`
   twin hardcode `NVARCHAR(20)`, which Firebird does not have — it spells it
   `NCHAR VARYING`. Both skip. `src/tests/unicode_tests.cpp:601` tests the same
   engine over the same connection and gets its table, because it uses
   `RoundTripTableGuard::create_first_working` with a fallback list. **Not
   fixed here** — one-line change, see the recommendations.

There is also a diagnosability problem behind all of this: crusher writes no
per-probe progress and block-buffers stdout, so a run killed at a wall-clock cap
discards everything unflushed. Finding (1) required enabling the Windows
driver-manager trace and reading the last ODBC call with no matching EXIT. A
wedged run and a run that produced nothing are indistinguishable from the
artifact.

---

## Verdict

**Crusher is effective at what it is shaped to find, and its shape is narrower
than the bug population it is aimed at.**

What it is genuinely good at, on this evidence:

- **Silent data corruption with a success return.** The #292 detection is
  exemplary — fourteen probes, the exact row, the exact truncation. This is the
  hardest class of bug to find by any other means, and crusher's round-trip
  probes are built for precisely it.
- **Crashes.** A category-level crash guard turns an access violation into a
  finding with a category name, and #297 was fixed on its report.
- **Regression measurement.** 75.6 % → 86.8 %, 20 probes moved, **zero
  regressions**, on runs that differ only in the driver. That number is worth
  having and no unit test produces it.

Where it is weak:

- **Coverage is the binding constraint, not analysis quality.** Fourteen of
  seventeen fixes were missed, and eleven of those were missed because no probe
  touches the API at all. Nothing about the reporting, classification or
  diagnostics would have helped.
- **Several probes assert less than their name promises.** The column-wise array
  probe binds the exact shape that triggers #299 and then checks the one column
  the bug spares. `test_ird_after_prepare` reads a count and stops.
  `test_cursor_scrollable_attr` sets an attribute and never reads it back. A
  probe that cannot fail on the bug it is named for is worse than no probe,
  because it reports a pass.
- **Its own suite had four defects, one of which cost two full CI runs**, and
  the mock could not have caught the worst of them.

The honest one-line answer to "is it finding the problems we fixed": it found
**three of seventeen** — but the three include the one that had already prompted
a fix by name, and the fourteen misses are a coverage map, which is actionable.

---

## Recommendations, in value order

1. **Build a block-cursor category.** Bind an array of columns, set
   `SQL_ATTR_ROW_ARRAY_SIZE`, fetch, and check the rows-fetched counter, the
   row-status array and a bind offset on both binding orientations, before and
   after `SQLPrepare`. That single category covers #301, #306, #307 and #304,
   and it is where BI tools live. Today `test_rowset_size` sets the attribute
   and never fetches.
2. **Make the array probes read back the key column.** `verify_rows_persisted`
   selects the value column ordered by the PK and never returns the PK. Adding
   it turns `test_column_wise_array_binding` into a detector for #299 rather
   than a probe that passes over corrupted data — and removes the
   `ORDER BY`-over-equal-keys trap the original bug report tripped on.
3. **Add a "NULL then value on the same parameter" probe.** One probe, covering
   #300 outright and the third defect of #292.
4. **Interrogate descriptor records, not just their count.** `SQL_DESC_TYPE`,
   `SQL_DESC_CONCISE_TYPE`, `SQL_DESC_NAME`, `SQL_DESC_NULLABLE` after
   `SQLPrepare`, cross-checked against `SQLDescribeCol` — which is exactly how
   #316 was found by hand.
5. **Bind `SQL_C_GUID` as a parameter,** not only as a fetch target.
6. **Attempt one deliberately failing connect** (bad password, missing database)
   and assert the diagnostic record is well-formed — SQLSTATE present, message
   length agreeing with `strlen`. That is #298, and it costs one probe.
7. **Cross-check `SQLGetInfo` claims against behaviour** wherever the pairing
   exists. `test_scalar_function_claim_vs_execute` already does this and is one
   of the most productive probes in the suite; `SQL_PARAM_ARRAY_ROW_COUNTS`
   versus what `SQLRowCount` actually returns is the same trick, and it is #311.
8. **Give crusher a per-probe watchdog and unbuffered progress.** One wedged
   call currently costs the whole run *and* the report.
9. **Route `run_int_to_string_roundtrip` through `create_first_working`** with
   `{"NVARCHAR(20)", "NCHAR VARYING(20)", "VARCHAR(20)"}` — two `CRITICAL` cells
   are unmeasured on Firebird today.

---

## Per-driver triage reports

The two full triages, produced by `/triage-driver` and classifying every
FAIL/ERROR as `BUG_IN_DRIVER` / `BUG_IN_CRUSHER` / `DRIVER_LIMITATION` /
`INCONCLUSIVE`, are in `tmp/triage/firebird-official/` and
`tmp/triage/firebird-patched/`.
