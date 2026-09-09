# ODBC Crusher — Improvement Plan V2: closing the coverage gap

**Created**: 2026-09-09
**Companion to**: [IMPROVEMENT_PLAN.md](./IMPROVEMENT_PLAN.md) — that plan is the general work queue and stays authoritative for everything outside this one subject. **H17**, **H18** and **H19** there are the immediate history of this document.
**Baseline commit**: `a223e36`
**Scope**: everything the Firebird before/after pair exposed about **odbc-crusher itself** — probes that do not exist, probes that cannot fail on the bug they are named for, fixtures that make probes measure nothing, and the survivability problems that cost two entire CI runs.

---

## Why this plan exists

On 2026-09-09 the manifest gained its first *pair*: `firebird-official`
(3.0.1.21) and `firebird-patched` (3.5.1-rc2), the same driver at two builds,
run under conditions that differ only in the install step. Seventeen
behavioural fixes separate them. **Crusher detected three.**

That is the whole reason for this document. The three it caught it caught
superbly — fourteen probes on one silent-corruption bug, with the exact row at
which truncation starts. The fourteen it missed were missed almost entirely for
one reason: **no probe touches the API at all**. Nothing about the reporting,
the classification or the diagnostics would have helped. Coverage is the binding
constraint.

The measurement is in
[`reports/FIREBIRD-3.0.1.21-vs-3.5.1-rc2-CRUSHER-EFFECTIVENESS.md`](../reports/FIREBIRD-3.0.1.21-vs-3.5.1-rc2-CRUSHER-EFFECTIVENESS.md),
and the two triages it draws on are beside it.

### The thing that makes this plan different from guesswork

We now have a **known-answer harness**. For every task below, the fix it targets
is a merged pull request with a described symptom, and the two builds bracket
it. So each task carries a **predicted verdict pair** — what the new or repaired
probe should report against 3.0.1.21 and against 3.5.1-rc2.

> **A probe that passes against both builds has not been shown to detect
> anything.** That is this plan's acceptance test, and it is stronger than "the
> code compiles and the suite is green", which is all the existing e2e
> requirement gives.

A surprise is data, not a failure: if a new probe passes on 3.0.1.21 as well,
either the probe does not reach the defect, or the defect post-dates
`dee624f` — the baseline predates the v3.5 refactor, so a handful of these bugs
may have been introduced after it. **Record which, in the row's Details.** Do
not quietly relax the probe until it goes green.

---

## How to keep this plan updated

Same discipline as the companion plan; it is repeated here so this document
stands alone.

1. Set **Status** to `✅ DONE` only once the fix is implemented **and** its
   predicted verdict pair has been observed on a real run of the pair.
2. Fill the **Commit** column with the short hash.
3. Rewrite the **Details** cell to say what was *actually* done and what the
   pair actually reported — not what was planned. If the observed verdicts
   differ from the prediction, say so and say why.
4. New problems get new rows. Never widen an existing task to absorb them.

**Status values** — use exactly these:

| Symbol | Meaning |
|---|---|
| ✅ DONE | Implemented, and the predicted verdict pair was observed on a real run. |
| 🔧 IN PROGRESS | Partially implemented. Note in Details what is left. |
| ❌ OPEN | Not yet addressed. |
| ⏯️ DEFERRED | On hold until another task finishes. Name the blocking task ID. |

**Rules that keep this plan honest:**

- **Never mark a task DONE on a green suite alone.** Green against the mock
  proves the probe compiles and runs. The pair proves it detects.
- **Never delete a row.** If a task turns out to be wrong, keep it and rewrite
  Details to explain the reversal.
- **A probe that cannot fail is worse than no probe**, because it reports a
  pass. When strengthening an existing probe, first confirm it fails against
  3.0.1.21; if it does not, the strengthening did not land.
- **Update the companion plan's `README.md` counts** whenever probe counts or
  the driver matrix change (H1–H3 there).
- Commit subject carries the row ID, as in the companion plan.

---

### If you only do six things

Ordered by fixes-unlocked per hour, not by severity.

| # | Task | Why first |
|---|---|---|
| 1 | **Q1** — make `verify_rows_persisted` return the key column | Two lines. Turns `test_column_wise_array_binding` from a probe that passes over corrupted data into a detector for #299 — and removes the `ORDER BY`-over-equal-keys trap the original bug report tripped on. |
| 2 | **S1** + **S2** — per-probe watchdog and unbuffered progress | Until these exist, every future run is one wedged ODBC call away from producing nothing at all, and the report will not say where it died. This cost two complete CI runs already. |
| 3 | **P1**–**P5** — the block-cursor category | One new category covers four of the thirteen PRs (#301, #304, #306, #307) and the single largest hole in the suite. Block cursors are how every BI tool reads bulk data. |
| 4 | **R1** + **R2** — seed the Firebird fixture | Eight of the twenty-three findings in the rc2 triage were probes that skipped without touching the driver. A fixture table and two procedures make them real assertions for free. |
| 5 | **R3** — stop the CALL-escape probe asserting about procedures that do not exist | It scores 0/7 against a *correct* driver. It is currently a false-positive generator, and it is in the published report. |
| 6 | **P6** + **P7** — interrogate descriptor records | Seven sub-defects of #316 sit behind reads the suite never performs. It found the crash in that area only because a crash needs no assertion. |

---

## How these findings were verified

Every gap below was checked against the probe source, not inferred from a
report. The method, so it can be repeated:

```bash
# "Is this API exercised at all?" — the check behind most of Part P.
for t in ROWS_FETCHED_PTR ROW_STATUS_PTR ROW_BIND_OFFSET PARAM_BIND_OFFSET \
         KEYSET_SIZE SQL_DESC_CONCISE_TYPE SQL_DESC_NAME SQL_DESC_NULLABLE \
         SQL_DESC_DATETIME_INTERVAL_CODE SQL_PARAM_ARRAY_ROW_COUNTS; do
  printf '%-34s %s\n' "$t" "$(grep -rl "$t" src/ | tr '\n' ' ')"
done            # every one of these prints an empty right-hand column
```

The two runs behind the measurement:
[34367166127](https://github.com/fdcastel/odbc-crusher/actions/runs/34367166127)
(official) and
[34367182766](https://github.com/fdcastel/odbc-crusher/actions/runs/34367182766)
(patched), both on crusher `68630c6`.

---

## Part P — Coverage the pair proved missing

Fourteen fixes went undetected. Eleven of them because nothing in `src/` names
the API. **This is the bulk of the work.**

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| P1 | 🔧 IN PROGRESS | HIGH | Block cursors | **Create a block-cursor category with a real multi-row fetch** | There is no block-cursor data path anywhere in the suite. `test_rowset_size` ([`advanced_tests.cpp:204`](../src/tests/advanced_tests.cpp)) sets `SQL_ATTR_ROW_ARRAY_SIZE = 100`, reads it back, and passes — it never fetches. Nothing binds an array of columns, fetches more than one row at a time, reads a rows-fetched counter, or inspects a row-status array. New `src/tests/block_cursor_tests.cpp`, registered in `main.cpp` and `src/tests/CMakeLists.txt` per AGENTS.md. The substrate: a six-row result (`UNION ALL`, via `literal_select_variants` so it survives Firebird's FROM-clause rule), `SQL_ATTR_ROW_ARRAY_SIZE = 4`, `SQLBindCol` into arrays, two fetches (4 rows then 2) and `SQL_NO_DATA`. Assert every bound slot on both rowsets, both binding orientations. **Predicted**: PASS on both builds — this is the ground the next four tasks stand on, not a detector. If it *fails* on either, that is a finding in its own right and gets its own row. **Done**: new src/tests/block_cursor_tests.{hpp,cpp}, category "Block Cursor Tests", registered in main.cpp and CMakeLists. The substrate binds an integer array, sets ROW_ARRAY_SIZE=4 over six rows, and asserts 1-4, then 5-6, then SQL_NO_DATA. **The fixture is a table, not a UNION ALL of literals** - the first version used the union and the mock driver, which implements block fetch correctly (D11), evaluates it as a single row, so the probes reported a one-row rowset as a driver defect. RoundTripTableGuard already handles the DDL dialects; the category creates the table once and drops it in run(). PASSes on both the mock and a live Firebird 5.0.3, which is what a substrate should do. Pair run pending. | — |
| P2 | 🔧 IN PROGRESS | HIGH | Block cursors | **`SQL_ATTR_ROWS_FETCHED_PTR` / `SQL_ATTR_ROW_STATUS_PTR` must survive `SQLPrepare`** (targets #301 / PR #303) | Both tokens are absent from `src/` entirely. `OdbcDesc::setDefaultImplDesc` reset them on every prepare, so an application that set them *before* `SQLPrepare` or `SQLExecDirect` — the usual order — never had them written by `SQLFetch`. PR #303: "The reset dates from 2004/2008, so every released version is affected." Set both before the prepare, fetch two rowsets, and assert the counter (4, then 2) and the status array (`SQL_ROW_SUCCESS` / `SQL_ROW_NOROW`). Include the control that already worked — set *after* the prepare — so a failure localises. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2.** **Done, and it detects the fix.** Against a live Firebird 5.0.3 build that predates PR #303: `after rowset 1: rows_fetched=999, status=[?, ?, ?, ?] - the counter was never written`. 999 is the probe's sentinel, so the driver wrote neither the counter nor the status array, which is exactly what #301 describes - setDefaultImplDesc reset both pointers on every prepare. The probe also checks the partial rowset (rows_fetched=2), where a wrong counter does the most damage, and treats unused status slots not marked NOROW as a note rather than a failure because the specification admits both readings. Pair run pending. | — |
| P3 | 🔧 IN PROGRESS | HIGH | Block cursors | **`SQL_ATTR_ROW_BIND_OFFSET_PTR` on a column-wise rowset** (targets #306 / PR #315) | `ROW_BIND_OFFSET` appears nowhere in `src/`. `getSchemaFetchData()` took the row-wise branch whenever a bind offset was set; that branch advances by `SQL_ATTR_ROW_BIND_TYPE`, which is 0 for column-wise binding, so every row of a rowset landed on the same address while the counter and status array claimed four. Rowset 4 over six rows, `SQLBindCol(SQL_C_SLONG, values, 0, indicators)`, offset of 8 × `sizeof(SQLLEN)` bytes, then a second rowset with the offset back at zero; check every shifted slot, every unshifted slot and both indicator arrays. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2.** **Done, and it detects the fix.** Against a live Firebird 5.0.3 build that predates PR #315: `offset 16 bytes; slots [-111, -111, -111, -111, 4, -111, -111, -111] - expected 1, 2, 3, 4 in the shifted half`. Every row landed on the same shifted address, leaving only the last value, which is the row-wise branch advancing by SQL_ATTR_ROW_BIND_TYPE = 0 - precisely the mechanism #306 reports. The probe checks both halves: the shifted one must hold the rowset and the unshifted one must be untouched, so ignoring the offset entirely and collapsing onto one address are distinguishable. Pair run pending. | — |
| P4 | 🔧 IN PROGRESS | MED | Block cursors | **`SQL_ATTR_KEYSET_SIZE` is not the rowset size; an unprepared IRD answers `HY007`** (targets #307 / PR #314) | `KEYSET_SIZE` appears nowhere in `src/`. The setter stored it in the ARD's array size, so setting a keyset size silently gave you a rowset of that size and the next `SQLFetch` wrote that many rows into buffers bound for one — a buffer overrun the application never asked for. Two assertions: keyset size 7 leaves the rowset at 1 and reads back as 7; and `SQLGetDescField` on an unprepared IRD returns the header fields but answers `HY007` (not `HY091`) for `SQL_DESC_COUNT` and record fields. **Caveat to honour**: through unixODBC the driver manager answers `HY007` for an unprepared IRD before the driver is consulted, so the header-field half of this must `SKIP_UNSUPPORTED` on Linux rather than fail. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2** (Windows). **Done, and it detects the fix.** Against a live Firebird 5.0.3 build that predates PR #314: `keyset size set to 7; rowset was 1, now 7`. The mock driver had no SQL_ATTR_KEYSET_SIZE at all and declined it with HY092, so the probe skipped there and the category had no e2e coverage of this cell; the mock now stores it **in its own field** and returns it, which is both the correct behaviour and the thing the probe asserts. The IRD-before-prepare half of #314 is not here - it is a descriptor question and belongs with P6. Pair run pending. | — |
| P5 | 🔧 IN PROGRESS | MED | Statement attrs | **Round-trip every settable statement attribute through `SQLGetStmtAttr`** (targets PR #304) | `test_statement_attributes` ([`advanced_tests.cpp:314`](../src/tests/advanced_tests.cpp)) is named "various attributes" and reads five: `QUERY_TIMEOUT`, `MAX_ROWS`, `MAX_LENGTH`, `NOSCAN`, `RETRIEVE_DATA`. The eight that were unreadable are none of them — `ROWS_FETCHED_PTR`, `ROW_BIND_OFFSET_PTR`, `ROW_OPERATION_PTR`, `PARAMS_PROCESSED_PTR`, `PARAM_BIND_OFFSET_PTR`, `PARAM_OPERATION_PTR`, `PARAM_STATUS_PTR`, `CURSOR_SCROLLABLE`. The rule is simple and worth stating as the probe's expectation: **every attribute `SQLSetStmtAttr` accepts must come back from `SQLGetStmtAttr`.** Set a pointer, read it back, then set NULL and read that back. Keep `SQL_ATTR_ROW_STATUS_PTR` as the control that already passed. **Predicted: FAIL on 3.0.1.21 (`HYC00` from the `default:` branch), PASS on 3.5.1-rc2.** **Done, and it detects the fix.** Eight pointer attributes plus SQL_ATTR_ROW_STATUS_PTR as the control that already worked; the probe only judges attributes the setter accepted, so "not settable" is not counted against a driver. FAILs against a live Firebird 5.0.3 build that predates PR #304. Pair run pending. | — |
| P6 | ❌ OPEN | HIGH | Descriptors | **Interrogate IRD and IPD *records*, not just their count** (targets #316 / PR #317, items 1, 2, 4, 5) | `SQL_DESC_CONCISE_TYPE`, `SQL_DESC_NAME`, `SQL_DESC_NULLABLE` and `SQL_DESC_DATETIME_INTERVAL_CODE` are never read anywhere in `src/`. `SQLPrepare` sized the IRD and IPD but left their records at constructor defaults, so `SQLGetDescField`, `SQLGetDescRec` and `SQLCopyDesc` returned type `99`, an empty name, nullable 0 and scale 0 — **with `SQL_SUCCESS`**. After `SQLPrepare` on a multi-column select, read each IRD record and cross-check against `SQLDescribeCol` on the same statement, which read the metadata directly and was therefore right: the two must agree. Add the full-width check — `SQL_DESC_LENGTH`, `SQL_DESC_OCTET_LENGTH` and `SQL_DESC_DISPLAY_SIZE` were written as 32-bit integers into the application's `SQLULEN`/`SQLLEN`, leaving the upper half untouched, so pre-fill the target with `0xAAAA…` and assert the whole width. Same for IPD records after `SQLDescribeParam`. **Predicted: on 3.0.1.21 the category still crashes (see P14), so the honest prediction is FAIL-or-CRASH on 3.0.1.21 and PASS on 3.5.1-rc2.** | — |
| P7 | ❌ OPEN | MED | Descriptors | **`SQLColAttribute` type fields and full-width numeric attribute** (targets #316 / PR #317, items 3, 6) | `SQLColAttribute` is called in exactly one place, for `SQL_DESC_UNSIGNED` ([`metadata_tests.cpp`](../src/tests/metadata_tests.cpp)). Two defects live behind the fields it never asks for: `SQL_DESC_TYPE` answered the concise code (91, 93) for datetime columns where the specification asks for `SQL_DATETIME`, and `SQL_DESC_DATETIME_INTERVAL_CODE` was rejected as an unknown field. A third is Linux-only and structural — both entry points were exported under C++ names, so unixODBC routed every call through the ODBC 2 `SQLColAttributesW`, and the numeric attribute was a 32-bit write on Linux x64 (`SQL_DESC_CONCISE_TYPE` of a UTF8 `VARCHAR` read back as `4294967287` instead of `-9`). Pre-fill the `SQLLEN` and assert the full width, on both platforms. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2 — and the Linux half needs the linux `firebird` job, see U2.** | — |
| P8 | ❌ OPEN | HIGH | Parameters | **A value bound after a NULL on the same parameter** (targets #300 / PR #302, and #292's third defect) | There is no "NULL then value" probe. `test_bindparam_null_indicator` ([`param_binding_tests.cpp`](../src/tests/param_binding_tests.cpp)) binds one NULL, executes, checks the return code, and never reads anything back — see **Q3**. The defect is nasty and entirely silent: `SQL_C_DEFAULT` resolves to `SQL_C_CHAR` for `SQL_NUMERIC`, `SQL_DECIMAL` and `SQL_BIGINT`, so an application binding its NULLs with `SQL_C_DEFAULT` and its values with the value's own C type retyped the parameter on its first NULL and never got it back — every later row reached the server as **NULL**, with `SQL_SUCCESS` and a correct row count. `SQLFreeStmt(SQL_RESET_PARAMS)` did not help. The matrix that found it: `NUMERIC(9,3)`, `DECIMAL(18,2)`, `BIGINT`, `INTEGER`, `SMALLINT`, `DOUBLE PRECISION` × {bind NULL as `SQL_C_DEFAULT` then a value; bind a value then NULL then a value}. Read every row back. `INTEGER`/`SMALLINT`/`DOUBLE` are the controls — their `SQL_C_DEFAULT` never resolves to a character type, which is why they escaped. **Predicted: FAIL on 3.0.1.21 (6 lost cells), PASS on 3.5.1-rc2.** | — |
| P9 | ❌ OPEN | MED | Conversions | **Bind `SQL_C_GUID` as a *parameter*** (targets #295 / PR #296) | `SQL_C_GUID` appears once in the suite, in `test_guid_type` ([`datatype_tests.cpp:535`](../src/tests/datatype_tests.cpp)), on the **output** path via `SQLGetData`. The bug is entirely on the input path: `SQLBindParameter(SQL_C_GUID, SQL_GUID, …, ptr, 16, &len)` returned `SQL_SUCCESS` and the server never saw the 16 UUID bytes — a binary target got the ASCII of the first 16 characters of the canonical string, and a text target under `CHARSET=UTF8` got UTF-16 on the wire. Two dialect-guarded shapes, both from the issue: `SELECT UUID_TO_CHAR(?)` (binary target) and `SELECT UUID_TO_CHAR(CHAR_TO_UUID(?))` (text target), asserting the canonical string comes back. Use the existing dialect-variant machinery so non-Firebird engines skip cleanly rather than fail. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2.** | — |
| P10 | ❌ OPEN | MED | Diagnostics | **One deliberately failing connect, and assert the diagnostic record is well-formed** (targets PR #298) | Crusher never attempts a connect that fails. Its only `SQLDriverConnect` probe reconnects an already-connected handle ([`sqlstate_tests.cpp:459`](../src/tests/sqlstate_tests.cpp)). Every `catch (std::exception&)` in the driver did `(SQLException&)ex` — a `reinterpret_cast` — so when the caught object was not an `SQLException` the virtual dispatch read through whatever lay where the vtable pointer should be: empty `SQLSTATE`, a native code that varied every run, and a one-byte message whose reported length disagreed with its `strlen`. That last part is the generic assertion, and it needs no Firebird knowledge: **connect to a nonexistent database, then require `SQLSTATE` non-empty and well-formed (5 characters), the message length to agree with the message, and the native code to be stable across two attempts.** Skip cleanly where the environment cannot produce a failing connect. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2 — but note PR #298 observed the garbling on Linux; if Windows does not reproduce it, record that in Details rather than deleting the probe.** | — |
| P11 | ❌ OPEN | MED | Parameter arrays | **`SQLRowCount` after an array execute, cross-checked against the driver's own `SQLGetInfo` claim** (targets PR #311) | `SQLRowCount` is called **zero** times in `array_param_tests.cpp`, and `SQL_PARAM_ARRAY_ROW_COUNTS` / `SQL_PARAM_ARRAY_SELECTS` appear nowhere in `src/` — so neither the behaviour nor the claim about it is read. This is the same trick that makes `test_scalar_function_claim_vs_execute` one of the most productive probes in the suite: **ask the driver what it does, then check that it does it.** If the driver answers `SQL_PARC_BATCH`, one row count per parameter set must be reachable through `SQLMoreResults`; if `SQL_PARC_NO_BATCH`, `SQLRowCount` must return the cumulative total. Either answer is conformant; disagreeing with your own answer is not. Five-set INSERT (expect 5) and a three-set UPDATE touching 3/1/0 rows (expect 4 — a total, distinguishable from both a set count and the last set's count). **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2.** | — |
| P12 | ❌ OPEN | MED | Parameter arrays | **Reuse the statement handle after a mid-array server error** (targets #309 / PR #313) | `test_param_status_per_row_partial_failure` executes once and reads the status array — it never touches the handle afterwards, which is exactly where the damage was. `executeStatementParamArray` pointed the APD's bind-offset pointer at a local and restored it on the normal and `inputParam` exits but not on the exception exit, so after a server error the descriptor pointed into a dead stack frame and *the next execute on that handle* read a garbage offset: an access violation in the original report, a garbage row on the operation-pointer branch. Extend the existing probe (or add a sibling) so that after the failing five-set INSERT it does a single-row insert on the **same handle** and reads the row back. Also assert the failed set is marked `SQL_PARAM_ERROR` rather than left `SQL_PARAM_UNUSED`. **Predicted: FAIL or CRASH on 3.0.1.21, PASS on 3.5.1-rc2.** Guard it: a crash here takes the category down, which is what the **S1** watchdog is for. | — |
| P13 | ❌ OPEN | LOW | Discovery | **Sanity-check the `SQL_DBMS_VER` value against its own free-text tail** (targets PR #294) | `SQL_DBMS_VER` is read three times and its *value* is never asserted — it is only ever a subject for buffer-length and Unicode probes ([`buffer_validation_tests.cpp:213`](../src/tests/buffer_validation_tests.cpp), [`unicode_tests.cpp:64`](../src/tests/unicode_tests.cpp)). 3.0.1.21 answered `06.03.1683 WI-V Firebird 5.0`: the numeric prefix is the engine/ODS number, not the product version, so `atoi` gives **6** and every consumer mis-identifies a Firebird 5 server as Firebird 6. A driver-agnostic prober has no second source of truth for a product version — but it does not need one here. ODBC fixes the format as `##.##.####` followed by optional vendor text, and **when that vendor text contains a version-shaped token, its major should agree with the numeric prefix.** `06.03…` against a tail saying `Firebird 5.0` is a self-contradiction the driver states in one string. Report as `WARNING`, not `FAIL`, and skip when the tail carries no version token — the heuristic is real but not universal. **Predicted: FAIL(WARNING) on 3.0.1.21, PASS on 3.5.1-rc2** (`05.00.1683 WI-V Firebird 5.0`). | — |
| P14 | ❌ OPEN | LOW | Descriptors | **Keep the `SQLCopyDesc` empty-descriptor probe, and make its blast radius one probe** | Not a gap — the one unambiguous success. `test_copy_desc` crashed 3.0.1.21 with `0xC0000005` and the fix commit credits it by name. It is recorded here because **P6** and **P7** add probes to the same category, and on 3.0.1.21 the crash currently discards every one of them: the category reports one `ERROR` and five probes vanish. Until **S1** lands, any descriptor probe added is unobservable on the baseline. **Blocked by S1**, and the reason S1 is ranked where it is. | — |

---

## Part Q — Probes that cannot fail on the bug they are named for

These already exist and already pass. That is the problem: each reports a pass
over the defect its name promises to catch.

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| Q1 | 🔧 IN PROGRESS | HIGH | Verification | **`verify_rows_persisted` never returns the key column** — so the column-wise array probe passes over corrupted data (targets #299 / PR #308) | [`test_base.cpp:412`](../src/tests/test_base.cpp) runs `SELECT <value_col> FROM <table> ORDER BY <pk_col>` and collects only the value column. `test_column_wise_array_binding` ([`array_param_tests.cpp:212`](../src/tests/array_param_tests.cpp)) binds the **exact shape that triggers #299** — `SQL_C_SLONG` with `BufferLength = 0`, which the specification says is ignored for fixed-length C types and which applications therefore pass — and then verifies only `NAME`. Under the bug every set reads element 0, so the IDs collapse to one value while the `SQL_C_CHAR` array steps correctly (there `BufferLength` *is* the element size, which is why the string-only tests never saw it). The probe passes. Worse, `ORDER BY` over three equal keys returns rows in arbitrary order — the same trap the original bug report fell into, where a NULL appeared to move between rows. **Fix**: `RowVerification` gains `actual_keys`; the query selects both columns; probes that know their expected keys assert them. **Acceptance**: this probe must FAIL against a driver with the stride defect. Since 3.0.1.21 masks it behind the executor bug (only one row is inserted at all), the honest check is the mock with a stride-bug mode, or a note in Details that the pair cannot isolate it. **Done in code**: `RowVerification` gains `actual_keys`, `display_key()`, `keys_are()` and `keys_joined()`; the query is now `SELECT <pk>, <val> … ORDER BY <pk>` and the key is read first (column order matters on a forward-only driver). `test_column_wise_array_binding` asserts `keys_are({"100","200","300"})` with a suggestion naming the BufferLength-as-stride cause. **Locally the assertion is masked exactly as U1 predicted**: this build has #308's executor defect, so the probe fails earlier at `COUNT(*) = 1 but expected 3` and never reaches the key check. Pair run pending. | — |
| Q2 | 🔧 IN PROGRESS | MED | Statement attrs | **`test_cursor_scrollable_attr` sets an attribute and never reads it back** | [`advanced_tests.cpp:581`](../src/tests/advanced_tests.cpp) calls `SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE, SQL_SCROLLABLE)` and passes on success. The bug PR #304 fixed is that `SQLGetStmtAttr` answered `HYC00` for it — the setter stored the value and the driver used it, and the application could not read it back. The probe is one call away from the defect and does not make it. Fold the read-back in here (and comprehensively in **P5**), plus the related assertion from #304's own tests: `SQL_SCROLLABLE` must move `SQL_ATTR_CURSOR_TYPE` off forward-only. **Predicted: FAIL on 3.0.1.21, PASS on 3.5.1-rc2.** **Done in code**: the probe now reads the attribute back, fails when `SQLGetStmtAttr` refuses it or returns a different value, and reports `SQL_ATTR_CURSOR_TYPE` alongside (ungraded — a driver may choose which scrollable type it substitutes). **Verified against a live Firebird 5.0.3, and it detects #304**: `SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE, SQL_SCROLLABLE) succeeded but SQLGetStmtAttr could not read it back (SQLSTATE=HYC00)` — the exact defect PR #304 fixed, on a build that predates it. Pair run pending. | — |
| Q3 | 🔧 IN PROGRESS | MED | Parameters | **`test_bindparam_null_indicator` never reads the value back** | It binds `SQL_NULL_DATA`, executes `SELECT CAST(? AS VARCHAR(50))`, and reports the return code. It does not fetch, so it cannot tell a NULL that arrived from a NULL that did not — and it uses a `SELECT`, so nothing persists to check. A probe named for the NULL indicator should assert that the value came back NULL, and (with **P8**) that a value bound afterwards on the same parameter is not swallowed. **Done in code**: the probe fetches the value back and grades it — SQL NULL passes, anything else fails at CRITICAL, with the empty-string case called out because that is what a driver produces when it converts the indicator into a zero-length value and it is indistinguishable from a real `''` downstream. A statement that returns no row is SKIP_INCONCLUSIVE rather than a silent pass. **Verified against a live Firebird 5.0.3**: `read back as SQL NULL`. Pair run pending. | — |
| Q4 | ❌ OPEN | MED | Process | **Audit the suite for the "asserts less than its name promises" shape, and write the rule down** | Q1–Q3, **P5** and **P13** are five instances found by accident while chasing seventeen specific fixes; nobody has looked for the shape deliberately. The tell is a probe whose body ends at `SQL_SUCCEEDED(rc)` when its name or `expected` string promises something about a *value*. Sweep every probe, list the ones where the `expected` text describes an outcome the body never reads, and either strengthen or rename them. Add the rule to `AGENTS.md` beside the e2e requirement: **a probe's `expected` string is a contract — if the body cannot fail when that expectation is violated, the probe is not finished.** | — |

---

## Part R — Fixtures: probes that measured nothing

Eight of the twenty-three findings in the rc2 triage were probes that skipped
**without touching the driver**. They are not driver findings and they are not
crusher bugs; they are missing test data, and they are cheap to fix.

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| R1 | 🔧 IN PROGRESS | HIGH | Fixture | **Seed the Firebird CI database with a persistent table** | `.github/actions/firebird-windows/action.yml:78` creates `crusher.fdb` and never seeds it, and every probe that creates a table drops it again in the same probe — so by the time the catalog categories run, the database holds no user table. `SQLTables` returning zero rows is the *correct* answer to the question asked. Four probes report `SKIP_INCONCLUSIVE` as a result: `test_count_star_result_metadata`, `test_statistics_result`, `test_privileges_result`, `test_outer_join_escape`. Seed one permanent table **with an index and a foreign key**, so `SQLStatistics` and `SQLTablePrivileges` have something to describe. Do it in the composite action so both halves of the pair get it identically. Mirror it in the linux `firebird` job. **Acceptance**: those four probes report a real verdict on both builds. **Done in code**: both the Windows composite and the linux job seed CRUSHER_FIXTURE_PARENT / CRUSHER_FIXTURE - primary keys, a secondary index, a foreign key between them, and five rows - and both fail the job loudly if the seed did not take, rather than letting four probes go on skipping for a reason nobody reads. The SQL is an array joined with newlines, not a PowerShell here-string: the closing token must sit at column 0 and that ends the YAML block scalar. **Verified against a live Firebird 5.0.3**: test_statistics_result now reports 4 index rows, test_privileges_result 5 privilege rows, and test_outer_join_escape actually sends its {oj ...} join against CRUSHER_FIXTURE - all three were SKIP_INCONCLUSIVE before. Pair run pending. | — |
| R2 | 🔧 IN PROGRESS | MED | Fixture | **Give the Firebird fixture the `MOCK_INOUT` / `MOCK_FN` equivalents** | Four probes — `test_call_escape_in_parameter`, `test_call_escape_out_parameter`, `test_function_call_escape_return_value`, `test_call_escape_inout_parameter` ([`escape_sequence_tests.cpp:1088`](../src/tests/escape_sequence_tests.cpp)) — need a procedure the fixture does not have, and their own header admits "Real drivers rarely have a procedure of that exact name". These are the **only** `SQL_PARAM_OUTPUT` and `SQL_PARAM_INPUT_OUTPUT` coverage in the suite, so parameter direction is currently untested against every real driver. Create a Firebird procedure and selectable function of those names in the R1 seeding step. **Done, and wider than this row assumed.** A fixture alone could not work: ground truth from a live Firebird 5.0.3 is that SQLProcedureColumns describes a two-in/two-out procedure as (IN, IN, OUT, OUT) - inputs first, then outputs, no SQL_PARAM_INPUT_OUTPUT at all, because Firebird's procedure model has none. The probes' hard-coded (IN, OUT, INOUT) shape is not something any fixture can give it. **So the probes now ask instead of assuming**: new describe_procedure() reads SQLProcedureColumns, call_by_contract() binds every declared parameter in its declared direction and builds the {CALL} or {? = CALL} form from what it found, and each probe asserts the part of the contract that applies - reporting SKIP_UNSUPPORTED, with the shape the driver *did* declare, for a direction the engine does not have. No engine names in src/. The mock's MOCK_INOUT / MOCK_FN are renamed CRUSHER_PROC / CRUSHER_FUNC so one contract covers the mock and every real driver, and both Firebird jobs seed them. New docs/FIXTURE_CONTRACT.md states what a probe may assume (the names, and the behaviour) and what it may not (shape, directions, types). **Verified against a live Firebird 5.0.3, where all four used to skip**: in_parameter PASSes having built {CALL CRUSHER_PROC(?, ?, ?, ?)} from the declared shape; out_parameter PASSes on a real assertion, OUT_N = 84 for input 42, which is the first SQL_PARAM_OUTPUT coverage Firebird has ever had here; inout and return_value report SKIP_UNSUPPORTED naming the declared shape. The mock's Procedures=BrokenInout e2e canary still trips, so the adaptive form still catches a driver that accepts the binding and never writes back. 184/184 crusher, 431/431 mock. Pair run pending. | — |
| R3 | 🔧 IN PROGRESS | HIGH | False positive | **`test_call_escape_format_variants` asserts translation of procedures that do not exist** | The probe hard-codes the identifiers `proc` and `func` and counts *any* non-translation, **including an error return**, as a failure ([`escape_sequence_tests.cpp:1044-1085`](../src/tests/escape_sequence_tests.cpp)). Firebird has two native spellings for a procedure call — `execute procedure p` for a non-selectable one, `select * from p` for a selectable one — so `getNativeSql` must look the procedure up before it can choose, and throws `Unknown procedure 'PROC'` when it cannot. **A driver with complete and correct CALL-escape handling scores 0/7 here.** That verdict is in the published rc2 report as `0/7 CALL variants translated`, which reads as a damning finding and is not evidence of a translation defect. Fix with **R2**'s real procedure, and treat a driver-reported error as `SKIP_INCONCLUSIVE` rather than "not translated". **This is the only known false positive in the two reports; it should be corrected before either is sent anywhere.** **Done in code**: the probe asks `SQLProcedures` for a name that exists (`find_any_procedure`) and falls back to the placeholder only when the catalog is empty, saying which it used; and the three outcomes are now separated — translated, *refused* (SQLNativeSql returned an error, which for a driver that resolves while translating is not a translation defect), and *silently untranslated* (SQL_SUCCESS with the braces still in place). Only the last is a FAIL; a refusal is SKIP_INCONCLUSIVE naming the SQLSTATE. **Verified against a live Firebird 5.0.3**: `7/7 CALL variants translated using the procedure CANCEL_BLOB found via SQLProcedures` — so the published `0/7` was a false positive end to end, and the driver's CALL-escape handling is in fact complete. The effectiveness report and its artifact have been corrected; both triage reports had already classified it as BUG_IN_CRUSHER. Pair run pending for the record. | — |
| R4 | 🔧 IN PROGRESS | MED | Portability | **Two `CRITICAL` conversion cells never run on Firebird** | `test_bindparam_int_to_wvarchar_roundtrip` and its `bigint` twin hard-code `NVARCHAR(20)` ([`param_binding_tests.cpp:763`, `:833`](../src/tests/param_binding_tests.cpp)). Firebird spells the type `NCHAR VARYING` / `NATIONAL CHARACTER VARYING`; there is no `NVARCHAR`, so the parser reads it as an identifier and stops at the `(`. Both cells skip, on both builds — the only coverage of numeric C → `SQL_WVARCHAR`, both rated `CRITICAL`. The counter-example is in the tree: [`unicode_tests.cpp:601`](../src/tests/unicode_tests.cpp) tests the same engine over the same connection and gets its table, because it uses `RoundTripTableGuard::create_first_working(conn_, table, {"NVARCHAR(64)", "VARCHAR(64)"})`, documented at [`test_base.hpp:792`](../src/tests/test_base.hpp) as exactly this shape. Route `run_int_to_string_roundtrip` through it with `{"NVARCHAR(20)", "NCHAR VARYING(20)", "VARCHAR(20)"}` and keep the skip only when every variant fails. **Done in code**: both round-trip templates take `const std::vector<std::string>& column_ddl_variants`, a new `create_roundtrip_table_first_working` routes through `RoundTripTableGuard::create_first_working` (the guard is movable, so it goes into the map by move; `try_emplace` cannot be used because the helper has to build it), and the skip message now lists every spelling tried instead of naming one that no longer exists. All 12 call sites converted; the WVARCHAR pair gets `{NVARCHAR(20), NCHAR VARYING(20), NATIONAL CHARACTER VARYING(20), VARCHAR(20)}`. **Verified against a live Firebird 5.0.3**: both cells now execute and report `All 10 rows round-tripped correctly` where they previously skipped. Pair run pending. | — |

---

## Part S — Survivability, determinism and diagnosability

The pair's first two runs produced **nothing at all**. Fixing the probe that
wedged them (**H18**) does not fix the property that let one probe cost two
runs.

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| S1 | ❌ OPEN | HIGH | Survivability | **Per-probe watchdog: one wedged ODBC call must not cost the run** | Both stress-test runs of the pair were killed at the 570 s cap with `crusher exit code: 124`, on both driver builds, because a single `SQLExecDirect("DROP TABLE …")` never returned. The tool's stated philosophy is *"reliable — never crash — handle all ODBC errors gracefully"*; a call that never returns is the one failure mode it has no answer for, and the category crash guard does not cover it because nothing unwinds. Give `run_test` a wall-clock bound: run the probe body on a worker, and on expiry record `ERROR` with `HYT00`-shaped diagnostics naming the probe, then continue. **This is genuinely hard to do safely** — abandoning a thread inside a driver call leaks a handle and may leave the connection unusable — so the honest first increment may be to bound the *category* rather than the probe, and to mark the connection poisoned and skip the remainder rather than pretend to recover. Whatever is chosen, the report must say which probe did not return. **Also raises the ceiling on P12**, which deliberately provokes a crash-prone path. | — |
| S2 | ✅ DONE | HIGH | Diagnosability | **Per-probe progress on stderr, unbuffered** | `main.cpp:344` writes `Phase 2: Running ODBC tests...` and then nothing until the run ends. stdout is block-buffered on a pipe, so a `SIGKILL` at a wall-clock cap discards every probe result already produced. From the artifact, **a wedged run and a run that produced nothing are indistinguishable** — the text report of both killed runs ended at the same line. Finding **H18** required enabling the Windows driver-manager trace and reading the last ODBC call with no matching EXIT, which is not a reasonable diagnostic path for a tool whose whole job is diagnosing. Emit `→ <category> / <probe>` to stderr before each probe and flush; stderr is already the progress channel (G1). Cheap, and it makes **S1** merely nice-to-have rather than essential. **Done**: `note_probe_start` in `test_base.hpp`, called from `run_test` before the body, emitting `  -> <category> / <probe>` and flushing. The start, not the completion — the interesting probe is the one that never completes. `run-crusher` now sends crusher's stderr to its own `crusher-progress.txt` instead of folding it into the report with `2>&1`, uploads it, and echoes its tail; that also keeps 200 progress lines out of the human-readable report on every healthy run. `triage.ps1` reads it on the missing-JSON path and sets `LAST_PROBE_STARTED` / `PROBES_STARTED`, so the abort reason now names the probe that did not return instead of saying the run "did not finish". **Verified** locally: progress on stderr, `0` progress lines on stdout (G1's rule, which the e2e harness pins by capturing the two separately). | pending |
| S3 | ✅ DONE | MED | Reporting | **A crashed category must name the probes it discarded** | On 3.0.1.21 the Descriptor Tests category died and its five probes vanished from the report entirely — the two runs' totals differ by four for this reason, and *both* triage agents independently flagged that the baseline pass rate is "optimistic by an unknown margin". The margin need not be unknown: the category knows its probe list before it runs. Emit the discarded probes as `ERROR` with a diagnostic naming the crash that took them, so the denominator stays honest and a reader can see *which* coverage was lost. Ties to **B2**'s definition of the scored denominator in the companion plan. **Done**: `TestBase` keeps `completed_` (a copy of every verdict as it is reached) and `last_started_`; `run_test` maintains both. On `guard.crashed`, `run_test_category` takes `completed_results()` instead of the empty vector, and the crash entry now names the probe that did not return and how many completed before it. Chose this over having each category declare its probe list up front: probe names are produced by running them, so a declaration would be a second list to keep in sync. **Testing**: `tests/unit/test_crash_survivors.cpp`, 5 cases — nothing salvaged before a run, every completed verdict kept (status included, not just the name), `last_started_` set *before* the body so it names the probe in flight, a throwing probe counted as a survivor rather than a casualty, and two categories not inheriting each other's results. The main.cpp wiring is deliberately not unit-tested: faulting the process on purpose is not something to ship in the mock, and `firebird-official` supplies a real access violation every run. | pending |
| S4 | ❌ OPEN | LOW | Reporting | **Record the run's own environment in the report** | The two reports are only comparable because the pair was run under identical conditions, and nothing in either report says so — the connection string is deliberately excluded (it carries the password), and the server version is only visible as `SQL_DBMS_VER`, which **P13** shows a driver may get wrong. Add the driver library path, the driver manager version and the platform to the JSON. Without it, a future reader cannot tell a real regression from a changed fixture. | — |

---

## Part T — Make the pair the acceptance test

The pair is currently a one-off measurement. It should be a standing harness,
or the next seventeen fixes will be as invisible as these fourteen.

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| T1 | ❌ OPEN | HIGH | Process | **Every task in Part P and Q records its observed verdict pair before it is DONE** | Stated at the top of this plan; repeated as a task because it is the thing most likely to be skipped under time pressure. Green against the mock proves a probe compiles and runs; only the pair proves it *detects*. The predicted pair is written in each row already — the work is to observe it and write the observation back. | — |
| T2 | ✅ DONE | MED | Tooling | **Commit the report-diff tool** | The per-probe diff behind this plan was an ad-hoc PowerShell script in a scratch directory: load both `crusher-report.json` files, index by `test_name`, and emit FIXED / REGRESSED / STILL-FAILING / ONLY-IN-ONE. That last bucket is what surfaced the discarded descriptor probes. It belongs in `tools/` beside the other maintenance scripts, taking two report paths, so "did this change detect anything?" is one command. **Done**: `tools/compare_reports.py OLD.json NEW.json`, five buckets — FIXED, REGRESSED, STILL FAILING, ONLY IN ONE REPORT, OTHER STATUS CHANGES — plus both pass rates and a one-line tally. `--require-no-regressions` exits 1 on any PASS→FAIL so it can gate CI. **Verified** by reproducing the H17 measurement exactly from the stored artifacts: fixed 20, regressed 0, still-failing 13, only-in-one 6, the last being the crashed Descriptor Tests category and the five probes it discarded. Output is ASCII-only, because a Windows console in cp1252 renders an em dash as a replacement character. | pending |
| T3 | ❌ OPEN | MED | CI | **Run the pair on demand as one dispatch, not two** | Today the pair needs two `workflow_dispatch` invocations and two `triage.ps1` runs, and nothing enforces that both used the same crusher binary — the `run-crusher` composite resolves "latest successful master CI run" independently for each, so a merge between them silently breaks the comparison. Add a `driver=firebird-pair` selector that runs both jobs in one workflow run, and have `fetch-stress-test.ps1` learn the pairing so both reports and their diff come out of one command. | — |
| T4 | ❌ OPEN | LOW | Fleet | **Generalise the pair to a manifest concept** | `firebird-official` / `firebird-patched` are two independent manifest entries that happen to share a connection string; nothing in `.github/drivers.json` says they are a pair, and nothing would notice if one drifted. A `pair_with` key, checked by `triage.ps1`, would let the skill refuse to compare two reports whose connection strings or crusher commits differ — the two conditions the whole method rests on. | — |

---

## Part U — What this plan does *not* promise

The brief was "it should catch all problems on the next triage". Most of that is
achievable; these parts are not, and saying so now is cheaper than discovering
it later.

| ID | Status | Sev | Area | Task | Details / Evidence | Commit |
|---|---|---|---|---|---|---|
| U1 | ❌ OPEN | — | Honesty | **Three fixes cannot be isolated by this pair, whatever probes are written** | (a) **#299's stride defect** is masked on 3.0.1.21 by #308's executor defect — only one parameter set runs at all, so there is no stride to get wrong. **Q1** is still right, but the pair cannot demonstrate it; the mock needs a stride-bug mode, or the demonstration waits for a build with one fixed and not the other. (b) **#279** (null-indicator offset) sits inside the same masked region. (c) **#294** is heuristic-only — see **P13**; a driver-agnostic prober has no independent source for a product version, and the self-contradiction check will not fire on a driver whose free text carries no version token. | — |
| U2 | ❌ OPEN | MED | Fleet | **Half of the platform surface is not covered by the pair** | The pair runs on Windows because the official 3.0.x line ships only Inno Setup installers — there is no Linux `.so` for 3.0.1.21. So the Linux-only defects among the seventeen are structurally invisible to it: **#288**'s `ConvertingString` heap overflow on the widechar path, and **#316 item 6** (`SQLColAttribute` exported under C++ names on Linux, making every call route through the ODBC 2 entry point). The nearest published Linux binary, `linux_libs.zip` at `v3-0-1-release`, is **build 18**, not 21 — close, but a different binary, and pinning it would break the "same source as the binary" guarantee that `install.sha256` and the `dee624f` pin exist to provide. Options, in preference order: build 3.0.1.21 from `dee624f` in CI and pin the resulting `.so` by digest; or add a `firebird-official-linux` entry pinned to build 18 with the discrepancy stated in its provenance note. Either way, **do not silently compare a build-18 Linux binary against build-21 source.** | — |
| U3 | ❌ OPEN | LOW | Honesty | **"Catches all problems" is a claim about a known seventeen** | Every task here was derived from fixes that already exist. Passing them all means the suite would have caught *these* seventeen — it says nothing about the next seventeen. The durable version of this work is **Q4**'s rule and **T1**'s discipline, not the individual probes; the probes are the evidence that the rules were applied once. | — |

---

## Part V — Execution order

Dependencies and observability, not severity. Each phase leaves the tree green
and is independently shippable.

### Phase 0 — See what is happening *(no probe changes)*

| Order | Task | Note |
|---|---|---|
| 1 | **S2** | Per-probe progress on stderr. Everything after this is easier to debug, and it is a few lines. |
| 2 | **T2** | Commit the report-diff tool, so every later phase can answer "did that detect anything?" in one command. |
| 3 | **S3** | A crashed category names its discarded probes — needed before **P6**/**P7**, whose probes land in the category that crashes on the baseline. |

### Phase 1 — Cheap detections and one correction

| Order | Task | Note |
|---|---|---|
| 4 | **R3** | Stop the false positive first. It is in a published report. |
| 5 | **Q1** | Two lines in `verify_rows_persisted`; unblocks the #299 assertion and removes the equal-keys ordering trap. |
| 6 | **R4** | One helper swap; two `CRITICAL` cells start running on Firebird. |
| 7 | **Q2**, **Q3** | Read back what the probe just set. Both are small and both are real detections. |

### Phase 2 — Fixtures, so probes stop measuring nothing

| Order | Task | Note |
|---|---|---|
| 8 | **R1**, **R2** | Seed the table, the procedure and the function in the composite action. Eight findings become real assertions. Do both halves of the pair *and* the linux job, or the next comparison is not like-for-like. |

### Phase 3 — The block-cursor category

| Order | Task | Note |
|---|---|---|
| 9 | **P1** | The substrate. Expect PASS on both; that is fine, it is the ground for what follows. |
| 10 | **P2**, **P3**, **P5** | Three detections on the substrate. Observe the verdict pair for each (**T1**). |
| 11 | **P4** | Last of the four, and the one with a driver-manager caveat to encode. |

### Phase 4 — Parameters and conversions

| Order | Task | Note |
|---|---|---|
| 12 | **P8** | The `SQL_C_DEFAULT` NULL matrix. Highest-value single probe in this plan after the block cursors. |
| 13 | **P11** | `SQLRowCount` and the claim/behaviour cross-check. |
| 14 | **P9** | `SQL_C_GUID` as a parameter. |
| 15 | **S1**, then **P12** | The handle-reuse probe deliberately provokes the path that crashed; land the watchdog first or accept that a failure costs the category. |

### Phase 5 — Descriptors

| Order | Task | Note |
|---|---|---|
| 16 | **P6**, **P7**, **P14** | Deliberately last among the probe work: on the baseline this category crashes, so until **S1** and **S3** these probes are unobservable there. |

### Phase 6 — Make it stick

| Order | Task | Note |
|---|---|---|
| 17 | **T3**, **T4** | One dispatch, one diff, and a manifest that knows the two entries are a pair. |
| 18 | **S4**, **Q4** | Environment in the report; the "asserts less than its name promises" sweep and the rule in `AGENTS.md`. |
| 19 | **U2** | The Linux arm. Largest remaining coverage question after everything above. |

### Sequencing at a glance

```
Phase 0  S2 → T2 → S3                      instruments
Phase 1  R3 → Q1 → R4 → Q2,Q3              cheap detections + the false positive
Phase 2  R1,R2                             fixtures
Phase 3  P1 → P2,P3,P5 → P4                block cursors        (4 fixes)
Phase 4  P8 → P11 → P9 → S1 → P12          parameters           (4 fixes)
Phase 5  P6,P7,P14                         descriptors          (7 sub-defects)
Phase 6  T3,T4 → S4,Q4 → U2                make it stick
```

---

## Appendix — Reproducing the measurement

```powershell
# Both halves of the pair, from one crusher build.
gh workflow run stress-test.yml --ref master -f driver=firebird-official
gh workflow run stress-test.yml --ref master -f driver=firebird-patched

# Triage each (writes preflight.txt, findings.json and the report).
pwsh -File .claude/skills/triage-driver/triage.ps1 -Driver firebird-official -RunId <id>
pwsh -File .claude/skills/triage-driver/triage.ps1 -Driver firebird-patched  -RunId <id>
```

Then diff the two `crusher-report.json` files per probe (**T2** commits this).
The buckets that matter:

- **FIXED** — FAIL/ERROR on official, PASS on patched. This is the detection count.
- **REGRESSED** — the reverse. Was zero on 2026-09-09.
- **STILL FAILING** — real bugs the fixes did not address; thirteen at the time of writing.
- **ONLY IN ONE REPORT** — probes a crashed category discarded. This bucket is
  why **S3** exists; it is invisible in the summary counts.
