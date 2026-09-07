# Port SQLComponents / ODBCQueryTool Test Ideas — Plan

**Sources surveyed**: `tmp/SQLComponents/UnitTest/` (14 CppUnitTest files, ~6.6k LoC), `tmp/SQLComponents/TestSQL/` (12 console-test files, ~5.2k LoC), `tmp/ODBCQueryTool/TestFiles/` (SQL scripts and example data — no portable C++ tests).

**Last updated**: 2026-04-25

---

## 1. Strategy summary

**What we are porting.** *Ideas*, not Firebird-specific SQL. The harvested concepts become probes against arbitrary ODBC drivers — discoverable schemas, optional CREATE TABLE with `IF NOT EXISTS`/fallbacks, `SQLGetTypeInfo`-driven type selection. No port carries a hard dependency on Firebird, MSSQL, or Oracle.

**Where ports land — the recipe.**

```
For each idea harvested:

1. Does it exercise a driver behavior currently thin/missing in src/tests/?
   YES → Zone A: add method to the relevant existing category class.
   NO  → Drop. (Utility-class tests — BCD math, formatting, variant ops — are out.)

2. Can the existing mock driver exhibit both the green path and the targeted
   failure mode the probe detects?
   YES → Add zone-A only. Done.
   NO  → Extend the mock with a knob (new FailOn= entry, SilentCorruption=
         variant, behavior flag), then add a zone-C canary in tests/e2e/
         that flips the knob and asserts the probe FAILs.

3. Does the probe need new shared infra (a new TestBase helper, a new
   round-trip table shape)? Refactor the helper first, probe lands clean.

Zone B (tests/unit/) is intentionally NOT used here — IMPROVEMENT_PLAN.md
§5.1/§5.3 retired the per-category GTest wrapper pattern; pure-C++
SQLComponents utility classes (SQLVariant, bcd, SQLDate) we don't own.
```

**Probe / canary pairing.** Each zone-A probe that detects a new failure shape ships with a zone-C canary so a future refactor can't silently downgrade FAIL → SKIP_INCONCLUSIVE without the e2e suite tripping. This is the same pattern the §1.4 silent-corruption canaries already use.

---

## 2. Master port list

Twelve ideas, ranked by value × proximity-to-existing-coverage. Each row links to its detail section.

| # | Idea | Source | Target category | Mock delta | Priority | Status |
|---|------|--------|-----------------|------------|---------:|--------|
| 1 | [SQL_NUMERIC_STRUCT byte-for-byte round-trip](#detail-1) | TestSQL/TestNumeric.cpp | `numeric_struct_tests` | none | High | ✅ RESOLVED |
| 2 | [Empty-string-vs-NULL distinction across C types](#detail-2) | TestSQL/TestNULL.cpp | `datatype_edge_tests` | small | High | ✅ RESOLVED |
| 3 | [`{?=CALL proc(?, ?)}` IN/OUT/INOUT parameter directions](#detail-3) | TestSQL/TestCalling.cpp + UnitTest/TestCalling.cpp | `escape_sequence_tests` (+ new helper) | medium | High | ✅ RESOLVED |
| 4 | [`SQLNativeSql` translation surface](#detail-4) | UnitTest/InfoDiscoveryTest.cpp + UnitTest/BasicDatabase.cpp | `escape_sequence_tests` | small | High | ✅ RESOLVED |
| 5 | [Cursor open/close hammer loop with phase timing](#detail-5) | TestSQL/TestClosingCursor.cpp | `cursor_stress_tests` | none | Medium | ✅ RESOLVED |
| 6 | [Array-param `SQL_PARAM_STATUS_PER_ROW` + driver-fallback path](#detail-6) | UnitTest/BulkOperationTest.cpp | `array_param_tests` | medium | High | ✅ RESOLVED |
| 7 | [NVARCHAR/WCHAR non-ASCII round-trip integrity](#detail-7) | UnitTest/TestNvarchar.cpp + TestSQL/TestNvarchar.cpp | `unicode_tests` | small | Medium | ✅ RESOLVED |
| 8 | [`SQLProcedures` / `SQLProcedureColumns` discovery](#detail-8) | UnitTest/InfoDiscoveryTest.cpp | `metadata_tests` | small | Medium | ✅ RESOLVED |
| 9 | [Transaction rollback while cursor still open](#detail-9) | UnitTest/TransactionTest.cpp (`TransactionRollback_FAIL`) | `transaction_tests` | small | Medium | ✅ RESOLVED |
| 10 | [Connection-pool reuse / stale-connection recovery](#detail-10) | UnitTest/Connections.cpp | `connection_tests` | none | Low | 🔧 IN PROGRESS — audit done (10.A ✅), implementation deferred (10.B ⏯️). |
| 11 | [DECIMAL aggregation precision (master/detail sums)](#detail-11) | TestSQL/TestSelections.cpp | `numeric_struct_tests` (informational cell) | none | Low | ✅ RESOLVED |
| 12 | [`{fn …}` scalar-function escape execution matrix](#detail-12) | UnitTest/FilterTest.cpp (`FilterFunction`) | `escape_sequence_tests` | none | Medium | ✅ RESOLVED |

**Explicitly skipped (no driver-behavior value).** AssignmentTest, BasicSheet, CalculateTest, CompareTest, Temporal, TestBCD (Icd/bcd math), TestOperators, TestFormatting, TestOracleOID (Oracle-specific), VariantFormatting — all pure C++ utility-class tests for SQLComponents library types we don't own. Zone B (`tests/unit/`) is not the right home for them; they would replicate the wrapper-class pattern §5.1 deleted.

---

## 3. Cross-cutting infrastructure tasks

These unblock multiple ports. Do them first.

| ID | Task | Status |
|----|------|--------|
| INFRA-1 | Audit `src/tests/test_base.hpp` for a probe-with-cleanup helper. The deferred §6.5 pattern (top-level `auto elapsed = [&]{...}` with try/catch + table cleanup before each early return) is what most round-trip ports want. Either extend `run_test` with an optional `on_exit` callback, or introduce an RAII `RoundTripTableGuard` so a CREATE TABLE in the probe is unwound automatically on any exit path. **Pick one approach and document it in `src/tests/README` if one exists, otherwise inline-comment the helper.** Required by ports #1, #2, #6, #7. | ✅ RESOLVED — Shipped as `RoundTripTableGuard` in test_base.hpp/.cpp (commit `38ff8fc`). RAII guard with INTEGER/INT id-column fallbacks, autocommit save/restore, rollback-after-failed-DDL, drop+retry once. Existing param_binding/transaction/array_param helpers intentionally not migrated (per §6.5). All 27 unit tests still pass; CI + CodeQL green. |
| INFRA-2 | Add a `mock-driver` connection-string knob `SilentCorruption=` cookbook entry for each new variant introduced by ports #2, #4, #6, #7. The pattern already exists (`DropInserts`, `MangleVarchar`, `TruncateNumeric`); extend it for new failure shapes rather than inventing parallel parameters. Update `mock-driver/README.md` whenever a new variant lands. | ✅ RESOLVED — All required knobs shipped. `SilentCorruption=` extended with `NullAsEmpty` and `MangleUnicode`. Top-level knobs added for cases where "data corruption" wasn't the right framing: `NativeSqlPassThrough`, `Procedures=BrokenInout`, `ArrayBindRowFailsAt`, `SupportsArrayBind`. README columns updated each time. |
| INFRA-3 | Add a stored-procedure surface to the mock driver: at least one mock proc with one IN, one OUT, one INOUT parameter, callable via `{?=CALL …}` escape. Required by port #3. Track behind feature flag `Procedures=Default` (default off so existing scenarios are unaffected). | ✅ RESOLVED — Shipped in commit `71a485c`. Procedure available in every preset (no opt-in flag — the existing test surface needed it for Port 8 too). `MockProcedure` extended with typed `params: vector<MockProcParam>`, `MockProcedureResult` with `output_values`, full SQLExecute writeback path (handles SLONG/SBIGINT/SSHORT/DOUBLE/CHAR/WCHAR/NULL→indicator). `SQLProcedureColumns` enumerates per-parameter rows with proper direction codes; `SQLProcedures` reports correct NUM_OUTPUT_PARAMS. Default `Procedures=Default` is the green path; `Procedures=BrokenInout` is the canary knob. |
| INFRA-4 | Confirm mock-driver `SQLNativeSql` actually translates the standard escapes (`{fn …}`, `{d …}`, `{t …}`, `{ts …}`, `{oj …}`) to native SQL. If it currently returns input verbatim, port #4 has nothing to detect against the green path. Implement minimal translation for the documented escape set; preserve the pass-through path under a new knob `SQLNativeSqlPassThrough=true` so port #4's canary has something to trip. | ✅ RESOLVED — Mock already implemented `translate_escape_sequences` for all six escape categories (commit `2f74ccb` confirms via two new ConnectionTest cases). Knob shipped as `NativeSqlPassThrough=true`/`false` (separate from `SilentCorruption=` because it doesn't tamper with stored data — it skips translation). |

---

## 4. Per-port detail

### <a id="detail-1"></a>Port 1 — SQL_NUMERIC_STRUCT byte-for-byte round-trip

**Idea source.** `TestSQL/TestNumeric.cpp` constructs a `SQL_NUMERIC_STRUCT` by hand (sign byte, precision, scale, `val[16]` mantissa little-endian), binds it as input, retrieves it back, and `memcmp`s the result against the input. The point: drivers that re-encode through `double` lose the last digits; drivers that mis-handle the sign byte produce silent negation; drivers that misread `val[]` byte order corrupt mantissa.

**Current odbc-crusher coverage** (per `src/tests/numeric_struct_tests`). Has positive/negative/zero, precision/scale handling, extremes. **Does not have**: byte-equality round-trip after a write — only reads.

**Why it's worth the port.** This is the most precise way to detect drivers that lose information silently in NUMERIC handling. The existing `TruncateNumeric` canary covers fractional truncation; this probe extends detection to mantissa-byte and sign-byte corruption.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 1.A | Add `NumericStructTests::test_numeric_struct_roundtrip_byte_equality` — discover or CREATE a `(ID INTEGER, VAL DECIMAL(p, s))` table, bind a `SQL_NUMERIC_STRUCT` with known mantissa/sign/scale, INSERT, SELECT back as `SQL_C_NUMERIC`, `memcmp` and FAIL on mismatch. Use `INFRA-1` cleanup helper. | ✅ RESOLVED — Shipped in commit `f4c0517`. INSERT-literal path (parameter binding for SQL_C_NUMERIC is too driver-variable for a portable probe). Probe is **scale-relative**: trusts `got.scale` and recomputes `expected_mantissa = round(literal × 10^got.scale)` before `memcmp` on val[]. Strict equality on sign byte. |
| 1.B | Add three precision/scale variants: `(10, 2)`, `(19, 0)`, `(38, 10)` — drivers fail at different boundaries. Report the first failing variant in `actual` so users see *where* the driver breaks. | ✅ RESOLVED — Three variants shipped: `(10,2)/12345.67`, `(19,0)/1234567890`, `(38,10)/123.4567890`. Mantissas chosen to fit in 32 bits so doubles represent them exactly (no FP-rounding false positives). First failing variant reported with hex dumps of got vs expected val[]. |
| 1.C | Confirm existing `SilentCorruption=TruncateNumeric` mock canary trips this probe; if not, file a follow-up to extend the mock. | ✅ RESOLVED — Verified locally: under `TruncateNumeric` the probe FAILs with mantissa `12345` (got) vs `12346` (expected). Extended `SilentCorruptionTruncateNumericTripsFractionalRoundTrip` e2e canary to assert this — no new mock knob needed. |

---

### <a id="detail-2"></a>Port 2 — Empty-string-vs-NULL distinction across C types

**Idea source.** `TestSQL/TestNULL.cpp` writes `''` and `NULL` to a VARCHAR column in separate rows, reads them back, and asserts the indicator distinguishes them (`SQL_NULL_DATA` vs `0`). Also bind-side: `SetParameter(NULL_indicator)` vs `SetParameter("")`.

**Current odbc-crusher coverage.** `param_binding_tests` covers NULL indicators in binding; `datatype_edge_tests` covers NULL per C type. **Does not have**: round-trip *contrast* between empty value and NULL value in the same probe — drivers that conflate the two pass both individually.

**Why it's worth the port.** Drivers that map empty `VARCHAR` to NULL (Oracle historically) are a well-known correctness footgun. The contrast probe surfaces it.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 2.A | Add `DataTypeEdgeCaseTests::test_null_vs_empty_distinction_varchar` — INSERT two rows (`''`, NULL), SELECT both, assert indicators differ (`0` vs `SQL_NULL_DATA`). FAIL with explicit suggestion if Oracle-style NULL conflation is detected. | ✅ RESOLVED — Shipped in commit `864c200`. Uses `RoundTripTableGuard` for cleanup; reports both indicators in `actual` for diagnostics. |
| 2.B | Replicate as `test_null_vs_zero_distinction_integer` — INSERT `0` and NULL into INTEGER, fetch as `SQL_C_LONG`, assert the indicator on the NULL row is `SQL_NULL_DATA` and not the value `0`. Catches drivers that zero the buffer instead of setting the indicator. | ✅ RESOLVED — Sentinel value `0xDEADBEEF` pre-fetch lets us distinguish "buffer zeroed" from "buffer untouched + indicator". Both buffer content and indicator reported in `actual`. |
| 2.C | Replicate as `test_null_in_numeric_struct` — INSERT NULL into DECIMAL, fetch as `SQL_C_NUMERIC`, assert indicator is `SQL_NULL_DATA` and the struct content is *not* required to be zeroed (per spec). | ✅ RESOLVED — Configures ARD descriptor (precision=10, scale=2) before SQL_C_NUMERIC fetch; SKIP_UNSUPPORTED if driver rejects SQL_C_NUMERIC entirely. Only the indicator is checked, not struct contents (per spec). |
| 2.D | Mock-driver knob: extend `SilentCorruption=` with `NullAsEmpty` (NULL VARCHAR fetches return `''` with indicator `0`). Required for canary. | ✅ RESOLVED — Added to `SilentCorruptionMode` enum + parser + statement_api SQLGetData NULL branch. Touches `SQL_C_CHAR`/`SQL_C_WCHAR`/`SQL_C_DEFAULT` only — integer/numeric NULL fetches deliberately untouched so the canary can assert isolation. README + 3 mock-driver internal tests added (parser + char-collapse + int-untouched). |
| 2.E | Add e2e canary `SilentCorruption=NullAsEmpty` → expect 2.A FAIL. | ✅ RESOLVED — `CrusherE2EFixture.SilentCorruptionNullAsEmptyTripsNullVsEmptyContrast` asserts varchar probe FAILs **and** integer/numeric probes stay PASS (isolation). 9/9 e2e + 73/73 mock-driver tests green; CI + CodeQL pass. |

---

### <a id="detail-3"></a>Port 3 — `{?=CALL proc(?, ?)}` IN/OUT/INOUT parameter directions

**Idea source.** `UnitTest/TestCalling.cpp` and `TestSQL/TestCalling.cpp` exercise `SQLPrepare("{?=CALL multinout(?, ?, ?)}")` with `SQLBindParameter` direction `SQL_PARAM_OUTPUT` / `SQL_PARAM_INPUT_OUTPUT` / `SQL_PARAM_INPUT`, plus an unnamed `?=` for the return value. Verifies values are populated post-`SQLExecute`.

**Current odbc-crusher coverage.** None evident — `escape_sequence_tests` covers `{fn …}`/`{d …}`/`{t …}`/`{ts …}`/`{oj …}` but not the procedure-call escape, and `param_binding_tests` covers IN-only.

**Why it's worth the port.** `{?=CALL …}` is Core conformance for any driver claiming SP support. INOUT is a common bug — drivers that accept the syntax but only honour IN are widespread.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 3.A | INFRA-3: ship the mock proc surface (one IN, one OUT, one INOUT, optional return). Gate behind `Procedures=Default`. | ✅ RESOLVED — See INFRA-3. |
| 3.B | Add `EscapeSequenceTests::test_call_escape_in_parameter` — `{CALL proc(?)}` with one IN; assert no error and `SQLRowCount` semantics. | ✅ RESOLVED — Shipped in commit `9854ec5`. Implementation binds all three MOCK_INOUT params (the mock validates input_param_count) but only asserts execute success. SKIP_INCONCLUSIVE if MOCK_INOUT not visible via SQLProcedures, with suggestion to register one. |
| 3.C | Add `EscapeSequenceTests::test_call_escape_out_parameter` — `{CALL proc(?)}` with one OUT; assert post-execute the bound buffer holds the expected mock value. | ✅ RESOLVED — Asserts `out_int == n*2` AND `out_int != 0xDEADBEEF` (sentinel). Severity ERR (Core conformance). |
| 3.D | Add `EscapeSequenceTests::test_call_escape_inout_parameter` — round-trip a value through INOUT; assert mutation. **This is the high-value probe** — many drivers fail here. | ✅ RESOLVED — Round-trips `"hello"` through INOUT VARCHAR, expects `"HELLO"` per `s := UPPER(s)` contract. Distinguishes "didn't write back" (still `"hello"`) from "wrote a different value". |
| 3.E | Add `EscapeSequenceTests::test_call_escape_return_value` — `{?=CALL fn(?)}`; assert position-1 bound buffer holds the function return. | ⏯️ DEFERRED — `MOCK_INOUT` is currently a procedure, not a function with a return value. Adding a separate mock function for `?=CALL` would duplicate INFRA-3 plumbing for marginal additional driver-conformance signal. Re-evaluate when a real driver exposes a function that round-trips poorly. |
| 3.F | Mock canary: `Procedures=BrokenInout` (mock returns input value unchanged on INOUT) → expect 3.D FAIL. | ✅ RESOLVED — `Procedures=BrokenInout` knob: MOCK_INOUT callback returns empty `output_values` so the SQLExecute writeback is a no-op. E2E canary `CrusherE2EFixture.ProceduresBrokenInoutTripsOutAndInoutProbes` asserts both 3.C and 3.D FAIL **and** 3.B stays PASS (IN path unaffected). |

---

### <a id="detail-4"></a>Port 4 — `SQLNativeSql` translation surface

**Idea source.** `UnitTest/InfoDiscoveryTest.cpp` (`TranslateSQLtoNative`) and `UnitTest/BasicDatabase.cpp` (`ODBCEscapeSequences`) feed escape sequences to `SQLNativeSql` and inspect the returned native SQL string.

**Current odbc-crusher coverage.** `escape_sequence_tests` exercises escapes via `SQLExecDirect` (does the query *run*?). It does **not** independently test `SQLNativeSql` translation — drivers can implement execution one way and translation differently, with both paths broken in subtle ways.

**Why it's worth the port.** Some applications (query rewriters, ORMs) call `SQLNativeSql` directly and never execute. A driver that ignores the call (returns input verbatim) breaks them silently.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 4.A | INFRA-4: confirm/implement mock-driver `SQLNativeSql` real translation for `{fn UCASE …}`, `{d 'YYYY-MM-DD'}`, `{t 'HH:MM:SS'}`, `{ts '…'}`, `{oj … LEFT OUTER JOIN …}`. | ✅ RESOLVED — Mock already had `translate_escape_sequences`; verified end-to-end with `SQLNativeSql_TranslatesScalarFunctionEscape`. |
| 4.B | Add `EscapeSequenceTests::test_native_sql_translation_function` — call `SQLNativeSql` on `SELECT {fn UCASE('abc')}`, assert the returned string differs from input AND contains either `UPPER(` or the driver's native uppercase function. Informational on translation choice; FAIL only on pass-through. | ✅ RESOLVED — `test_native_sql_scalar_functions` already existed (returned-brace check); tightened to **also** flag pure pass-through (`output == input`). The two checks together catch both partial and complete pass-through. |
| 4.C | Add `EscapeSequenceTests::test_native_sql_translation_datetime` — feed `{d '2026-01-01'}`, `{t '12:00:00'}`, `{ts '2026-01-01 12:00:00'}`; assert non-pass-through. | ✅ RESOLVED — `test_native_sql_datetime_literals` already existed and asserts no `{` in any of the three translations. |
| 4.D | Add `EscapeSequenceTests::test_native_sql_translation_outer_join` — `{oj T1 LEFT OUTER JOIN T2 ON …}`; assert the literal `{oj` is removed from the output. | ✅ RESOLVED — Shipped in commit `2f74ccb` as `test_native_sql_outer_join_escape`. Asserts both no `{oj`/`{OJ` substring and `output != input`. |
| 4.E | Mock canary: `SQLNativeSqlPassThrough=true` → expect 4.B FAIL. | ✅ RESOLVED — `CrusherE2EFixture.NativeSqlPassThroughTripsTranslationProbes` asserts all four `test_native_sql_*` cells FAIL **and** `test_outer_join_escape` (execution-side, not translation-side) stays PASS — proving the deficiency is localised to `SQLNativeSql`, not to `SQLExecDirect`. |

---

### <a id="detail-5"></a>Port 5 — Cursor open/close hammer loop with phase timing

**Idea source.** `TestSQL/TestClosingCursor.cpp` runs the same `SELECT` 1500 times and times open vs close phases independently.

**Current odbc-crusher coverage.** `cursor_stress_tests` exists but per the inventory its full method set is "TBD." Confirm before extending.

**Why it's worth the port.** Slow `SQLCloseCursor` is a real driver footgun — some drivers re-fetch remaining rows on close. Phase-separated timing surfaces it; aggregate timing hides it.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 5.A | Inventory existing methods in `cursor_stress_tests.cpp` and decide: extend or add. | ✅ RESOLVED — Existing methods (`test_rapid_cursor_lifecycle` whole-iteration timing + `test_concurrent_statements` MARS check). New probes are pure additions — extend, don't replace. |
| 5.B | Add `CursorStressTests::test_open_close_hammer_loop` — N=1000 iterations of `SQLAllocHandle(STMT) → SQLExecDirect("SELECT 1") → SQLCloseCursor → SQLFreeHandle`. Track open-phase μs and close-phase μs separately. Report both in `actual`. FAIL if close-phase mean exceeds 10× open-phase mean (configurable threshold). | ✅ RESOLVED — Shipped in commit `1752fc6` with N=500 (kept short to fit CI runtime; mock takes ~10ms). Reports `open_mean=33µs close_mean=6µs ratio=0.18` against the mock. |
| 5.C | Add `CursorStressTests::test_handle_reuse_no_leak` — same statement handle, 1000× `SQLExecDirect → SQLCloseCursor`, assert no `SQLAllocHandle` is needed and no diagnostic queue accumulates. | ✅ RESOLVED — N=500 reuse cycles. After every iteration walks the diag queue (records 1..32) and tracks max. FAILs if any iteration leaves > 1 record behind (spec: SQLExecDirect clears the queue at entry). Reports `max_diag_records_observed=0` against the mock. |

No mock delta needed — the existing mock should sail through this; the probe is meant to surface real-driver pathologies.

---

### <a id="detail-6"></a>Port 6 — Array-param `SQL_PARAM_STATUS_PER_ROW` + driver-fallback path

**Idea source.** `UnitTest/BulkOperationTest.cpp`. Tests cover: `SetParameterArraySize`, parameter array binding with NULLs, batch chunking, per-row status retrieval, and explicit fallback when the driver doesn't accept `SQL_ATTR_PARAMSET_SIZE > 1`.

**Current odbc-crusher coverage.** `array_param_tests` exists; per the inventory it covers "array binding for bulk operations, parameterized array operations." Verify whether it covers per-row error status and the fallback path.

**Why it's worth the port.** The two highest-value scenarios from BulkOperationTest aren't basic array binding — they're (a) drivers that report success at the batch level but failure at a single row via `SQL_PARAM_STATUS_PTR`, and (b) drivers that reject `SQL_ATTR_PARAMSET_SIZE > 1` and force the application to fall back. Both are conformance edge cases.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 6.A | Inventory existing methods in `array_param_tests.cpp`. | ✅ RESOLVED — 8 existing probes cover column-wise / row-wise binding, status array (all-success), processed count, NULL values, operation array (SQL_PARAM_IGNORE), paramset_size=1, partial error via IGNORE. |
| 6.B | Add `ArrayParamTests::test_param_status_per_row_partial_failure` — bind a 5-row batch where row 3 violates a constraint, set `SQL_ATTR_PARAM_STATUS_PTR`, execute, assert per-row status array shows `SQL_PARAM_SUCCESS` for rows 1/2/4/5 and `SQL_PARAM_ERROR` for row 3. | ✅ RESOLVED — Shipped in commit `c039ee7`. Reports `succ/err/other` counts in `actual`. Probe stays PASS in green path (all-success); under canary, validates the per-row contract (succ > 0 when err > 0). |
| 6.C | Add `ArrayParamTests::test_paramset_size_unsupported_returns_error` — set `SQL_ATTR_PARAMSET_SIZE=10` on a driver that may not support it; if it returns `SQL_ERROR` or `SQL_SUCCESS_WITH_INFO` with state `HYC00`, classify as KNOWN-LIMITATION (not FAIL). Informational cell. | ✅ RESOLVED — Final form (`952b22c`) walks the diagnostic queue (DM may inject IM006 ahead of the driver's HYC00) and treats empty-SQLSTATE rejection as SKIP_INCONCLUSIVE rather than FAIL. Three classifications: PASS (driver supports), SKIP_UNSUPPORTED (HYC00), SKIP_INCONCLUSIVE (DM intercepted with no diagnostic). |
| 6.D | Mock knobs: `ArrayBindRowFailsAt=N` (mock returns SQL_PARAM_ERROR for the Nth row in any batch), `SupportsArrayBind=No` (mock returns HYC00 on `SQL_ATTR_PARAMSET_SIZE > 1`). | ✅ RESOLVED — Both knobs shipped. `ArrayBindRowFailsAt=N` synthesizes a SQLSTATE 23000 error for the Nth (1-indexed) row inside the array execute loop; surrounding rows execute normally. `SupportsArrayBind=false` returns SQL_ERROR + HYC00 from `SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE)` when size > 1. |
| 6.E | E2E canaries: `ArrayBindRowFailsAt=3` → expect 6.B PASS with row 3 reported. `SupportsArrayBind=No` → expect 6.C SKIP_UNSUPPORTED (not FAIL). | ✅ RESOLVED — `ArrayBindRowFailsAtProducesMixedStatus` asserts `actual` contains "OK, OK, ERR". `SupportsArrayBindFalseDoesNotFailProbe` asserts NOT FAIL (any of PASS/SKIP_UNSUPPORTED/SKIP_INCONCLUSIVE acceptable, since unixODBC's DM layer can intercept the SetStmtAttr error before it reaches the driver — that's a manager bug, not a probe bug). |

---

### <a id="detail-7"></a>Port 7 — NVARCHAR/WCHAR non-ASCII round-trip integrity

**Idea source.** `UnitTest/TestNvarchar.cpp` writes accented characters and the Euro symbol through `SetParameter(value, wide=true)`, reads back, and verifies bytes survive. The Latin-1-only path is the failure mode.

**Current odbc-crusher coverage.** `unicode_tests` covers WCHAR input, catalog patterns, GetData WCHAR, truncation. **Doesn't appear to cover** round-trip *integrity* for non-ASCII payloads — i.e., binding non-ASCII WCHARs and asserting byte equality on read.

**Why it's worth the port.** Drivers that re-encode through the system codepage (Latin-1, CP1252) silently mangle codepoints outside the codepage. WCHAR-in / WCHAR-out byte equality catches it.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 7.A | Add `UnicodeTests::test_wchar_roundtrip_non_ascii` — discover or CREATE an NVARCHAR(64) column, INSERT a WCHAR string containing accented Latin (`é`, `ñ`), the Euro symbol (`€`, U+20AC), CJK (`漢字`), and an emoji (surrogate pair, e.g. U+1F600). SELECT back as `SQL_C_WCHAR`, byte-compare. FAIL with codepoint diff in `actual` on mismatch. | ✅ RESOLVED — Shipped in commit `5ccaac6`. NVARCHAR(64) → VARCHAR(64) DDL fallback for engines where VARCHAR is already Unicode-capable. Surrogate-pair coverage moved into a sibling probe (7.B); main probe stays in BMP. Codepoint hex dump on mismatch. |
| 7.B | Add `UnicodeTests::test_wchar_surrogate_pair_preserved` — explicitly round-trip a U+1F600-class codepoint and assert the surrogate pair survives (some drivers split or normalize). | ✅ RESOLVED — Single-codepoint U+1F600 probe; asserts `out[0]==0xD83D, out[1]==0xDE00`. Severity WARNING (some drivers legitimately don't support supplementary plane). |
| 7.C | Mock knob: extend `SilentCorruption=` with `MangleUnicode` (mock fetches strip non-ASCII bytes). | ✅ RESOLVED — Added `MangleUnicode` mode to `SilentCorruptionMode`. Replaces every byte > 0x7F with `?` on char/wchar fetch (UTF-8 leading bytes get smashed, conversion to UTF-16 produces `U+003F`). Touches char/wchar only — date/time parsing untouched. **Mock-driver fix bundled**: `read_param_value` previously fell to the narrow-string path for `SQL_C_WCHAR` (truncating at first 0x00 byte), now decodes UTF-16 → UTF-8 via `sqlw_to_string`. Without this fix the round-trip probes would flag a mock-driver bug rather than a DUT bug. |
| 7.D | E2E canary: `SilentCorruption=MangleUnicode` → expect 7.A FAIL with diff on Euro/CJK rows. | ✅ RESOLVED — `CrusherE2EFixture.SilentCorruptionMangleUnicodeTripsWcharRoundTrip` asserts both 7.A and 7.B FAIL under MangleUnicode. 10/10 e2e + 76/76 mock + 27/27 unit pass; CI + CodeQL green. |

---

### <a id="detail-8"></a>Port 8 — `SQLProcedures` / `SQLProcedureColumns` discovery

**Idea source.** `UnitTest/InfoDiscoveryTest.cpp` (`ProcedureDiscovery`, `ParametersDiscovery`).

**Current odbc-crusher coverage.** `metadata_tests` covers SQLTables/SQLColumns/SQLPrimaryKeys/SQLForeignKeys/SQLStatistics/SQLSpecialColumns/Privileges. **Does not appear to cover** SQLProcedures and SQLProcedureColumns.

**Why it's worth the port.** Driver developers who tested catalog functions on tables often forget the procedure variants — both functions are Core conformance.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 8.A | Add `MetadataTests::test_sqlprocedures_smoke` — call `SQLProcedures(NULL, NULL, NULL)`, walk the result set, assert column shape (`PROCEDURE_CAT`, `PROCEDURE_SCHEM`, `PROCEDURE_NAME`, reserved-1..3, `REMARKS`, `PROCEDURE_TYPE`). FAIL on missing columns; SKIP_UNSUPPORTED on `IM001`. | ✅ RESOLVED — Shipped in commit `71a485c`. Asserts ≥8 columns; reports `ncols`, `rows`, and first PROCEDURE_NAME for diagnostics. Empty list is valid (informational, not FAIL). |
| 8.B | Add `MetadataTests::test_sqlprocedurecolumns_smoke` — for the first procedure name returned by 8.A (or for the mock's INFRA-3 proc), call `SQLProcedureColumns`, assert the parameter rows include the expected `COLUMN_TYPE` codes (`SQL_PARAM_INPUT`, `SQL_PARAM_OUTPUT`, `SQL_PARAM_INPUT_OUTPUT`, `SQL_RESULT_COL`, `SQL_RETURN_VALUE`). | ✅ RESOLVED — Tallies COLUMN_TYPE codes by direction. FAILs on any code outside the documented set. Reports `IN/OUT/INOUT/RESULT/RETURN` counts plus a sample of the first three rows in `actual`. |
| 8.C | Mock delta — ensure `SQLProcedures` and `SQLProcedureColumns` enumerate the INFRA-3 mock procedure surface. | ✅ RESOLVED — `SQLProcedureColumns` rewritten to iterate `MockProcedure::params`; reports 3 rows for MOCK_INOUT (IN INTEGER, OUT INTEGER, INOUT VARCHAR). `SQLProcedures.NUM_OUTPUT_PARAMS` derived from `params` for procedures using the new metadata, falls back to legacy `input_param_count` for INSERT_N_ROWS. |

---

### <a id="detail-9"></a>Port 9 — Transaction rollback while cursor still open

**Idea source.** `UnitTest/TransactionTest.cpp` `TransactionRollback_FAIL` — closes the database while a transaction is open and asserts the failure surfaces meaningfully.

**Current odbc-crusher coverage.** `transaction_tests` covers commit/rollback/isolation/autocommit/state. **Does not appear to cover** the cross-state interaction: open cursor + uncommitted writes + rollback.

**Why it's worth the port.** Drivers that handle the happy path correctly often leak state when the rollback path fires while a cursor is mid-fetch. Returning to a clean state machine after the rollback is what this probe verifies.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 9.A | Add `TransactionTests::test_rollback_with_open_cursor` — start txn, INSERT one row, open a cursor on the same table, fetch first row, call `SQLEndTran(SQL_ROLLBACK)`, assert: (i) rollback succeeds or returns a documented error, (ii) the cursor is closed (subsequent `SQLFetch` returns the appropriate state), (iii) the inserted row is gone, (iv) the connection is in a usable state for the next statement. | ✅ RESOLVED — Shipped in commit `2f7edb2`. All four invariants reported in compact form: `fetch_rc=0 rollback_rc=0 post_rollback_fetch_rc=-1 post_rollback_count=0 post_select_rc=0`. autocommit save/restore on every exit path; CREATE TABLE committed before INSERT-then-rollback so the table doesn't disappear with the rollback. |
| 9.B | Add `TransactionTests::test_disconnect_with_open_transaction` — start txn, INSERT, attempt `SQLDisconnect` without commit/rollback. Spec says implicit rollback; assert the row is gone on reconnect. | ⏯️ DEFERRED — Verifying "row is gone on reconnect" requires a SECOND ODBC connection handle, which the existing `TestBase` infrastructure doesn't expose (probes get a single `OdbcConnection&`). Re-evaluate when a TestBase extension to allocate a sibling connection becomes useful for other ports. The high-value cross-state shape (rollback × cursor) is captured by 9.A; 9.B is informational. |

No mock delta needed for the green path. A canary requires a `RollbackLeavesCursorOpen=true` knob — defer until 9.A is observed to drift.

---

### <a id="detail-10"></a>Port 10 — Connection-pool reuse / stale-connection recovery

**Idea source.** `UnitTest/Connections.cpp` (`PoolTesting`, `CleanUpPool`, `ReuseConnection`).

**Current odbc-crusher coverage.** `connection_tests` lists "pooling" but unclear how deep. Worth confirming before adding.

**Why it's only Low priority.** The SQLComponents tests are mostly about the pooling object's lifecycle (a SQLComponents abstraction), not about how the *driver* interacts with `SQL_ATTR_CONNECTION_POOLING`. The driver-relevant slice is small.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 10.A | Inventory `connection_tests.cpp` pool methods. If they cover only happy-path enable/disable, add 10.B; if they already cover dead-connection recovery, drop this port. | ✅ RESOLVED — Audit conclusion: the existing `test_connection_pooling` is even thinner than "happy-path enable/disable" — it only **queries** `SQL_ATTR_CONNECTION_POOLING` mode without ever setting/exercising it. So a real driver-conformance gap exists. **However**, the per-port priority assessment in the plan stands: the gap's value is small (most pooling complexity lives in the application's pooling abstraction, not in driver behavior), and the implementation cost of 10.B (mock knob + probe infrastructure) is non-trivial. 10.B remains ⏯️ DEFERRED — a focused future session with explicit user demand should open it. |
| 10.B | Add `ConnectionTests::test_pooled_connection_after_remote_close` — enable pooling, take a connection, simulate the server side terminating the underlying transport (mock knob `KillConnectionAfterIdle=ms`), put the connection back in the pool, take a new connection, assert the driver detects the dead state and either reconnects or returns a documented error rather than handing out a broken handle. | ⏯️ DEFERRED — Confirmed by 10.A audit. Implementation cost (mock connection-lifecycle plumbing for kill/idle, plus probe infrastructure to allocate/release/re-acquire pooled connections) outweighs the driver-conformance signal at this stage. Open when (a) a real driver under test exhibits a pool-recovery bug worth diagnosing, or (b) a TestBase extension to allocate sibling connections becomes useful for other ports (would also unlock port 9.B). |

Defer 10.B until 10.A confirms a real gap.

---

### <a id="detail-11"></a>Port 11 — DECIMAL aggregation precision (master/detail sums)

**Idea source.** `TestSQL/TestSelections.cpp` — sums `detail.amount` across rows and asserts the per-master total equals the sum.

**Current odbc-crusher coverage.** Numeric round-trip is covered; sum-loop precision is not.

**Why it's worth a small port.** The test exposes drivers that lose 1¢ per ~1000 rows when the DECIMAL is round-tripped through `double`. Catches a real footgun, but only worth a single informational cell.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 11.A | Add `NumericStructTests::test_decimal_sum_loop_precision` — INSERT 1000 rows with `DECIMAL(10, 2)` values that have a known exact sum (e.g., 1000 × 0.01 = 10.00 exactly). SELECT all rows, accumulate via `SQL_C_NUMERIC` (NOT `SQL_C_DOUBLE`), compare against expected. FAIL on inexact sum, with the absolute error in `actual`. Informational on PASS. | ✅ RESOLVED — Shipped in commit `cba0862`. 100 rows of 0.01 (kept short to keep total runtime under 15ms); accumulation normalises for driver-reported scale. Existing TruncateNumeric e2e canary extended with a sibling assertion (sum drops from 100 to 0 under trunc). |

No mock delta — the existing `TruncateNumeric` canary should already trip it; verify and close.

---

### <a id="detail-12"></a>Port 12 — `{fn …}` scalar-function escape execution matrix

**Idea source.** `UnitTest/FilterTest.cpp` `FilterFunction` — generates `{fn UCASE(?)}`, `{fn CONCAT(?, ?)}`, `{fn SUBSTRING(?, ?, ?)}`, `{fn LTRIM(?)}`/`{fn RTRIM(?)}`, `{fn ABS(?)}`, `{fn SQRT(?)}`, `{fn EXTRACT(? FROM ?)}`, `{fn TIMESTAMPADD(?, ?, ?)}`. SQLComponents tests only generate the syntax; we want to *execute* it.

**Current odbc-crusher coverage.** `escape_sequence_tests` covers escapes; the inventory says "ODBC scalar/function escapes" but doesn't enumerate which scalar functions. The driver-discovery `scalar_functions` discovery (already pulled by §2.4) reports the *bitmask* of what the driver claims to support; this port closes the loop by *running* one query per claimed function.

**Why it's worth the port.** Drivers that announce `SQL_FN_STR_UCASE` in the bitmask and then return `42000` when the query runs are common. The "claim vs execute" cross-check is the exact value-add.

**Tasks.**

| ID | Task | Status |
|----|------|--------|
| 12.A | Add `EscapeSequenceTests::test_scalar_function_claim_vs_execute` — for each scalar function the driver claims via `SQLGetInfo(SQL_STRING_FUNCTIONS / SQL_NUMERIC_FUNCTIONS / SQL_TIMEDATE_FUNCTIONS)`, run a one-shot `SELECT {fn …}` with literal arguments, assert success. Report the claim-vs-execute matrix in `actual`. FAIL on any claimed-but-broken function. | ✅ RESOLVED — Shipped in commit `4f7a8fd`. Single consolidated probe covering 7 string + 5 numeric + 5 datetime + 3 system functions (20 total). Compact diagnostic format: "STRING:7/7 NUMERIC:5/5 TIMEDATE:5/5 SYSTEM:2/2 (broken: SUBSTRING)". SKIP_INCONCLUSIVE if the driver implements zero of the bitmasks. |

No mock delta — the existing mock should announce a small set and execute them.

---

## 5. Execution order recommendation

1. **INFRA-1** (cleanup helper) — unblocks ports 1, 2, 6, 7.
2. **Ports 1, 2, 11** — pure numeric / NULL / round-trip ports; share helper, share mock surface.
3. **INFRA-2** + **port 7** — Unicode round-trip; small mock extension; high signal-to-noise.
4. **INFRA-4** + **port 4** — `SQLNativeSql` translation; isolated mock surface.
5. **INFRA-3** + **ports 3, 8** — procedure surface + procedure escape + procedure catalog; share infra.
6. **Ports 6, 12** — array-param status & scalar-function execute matrix; mostly independent.
7. **Port 5** — cursor hammer; standalone, no infra.
8. **Port 9** — transaction × cursor; standalone.
9. **Port 10** — pool/recovery; defer until 10.A audit confirms gap.

---

## 6. Out of scope (skipped tests, recorded for completeness)

| Source file | Reason for skip |
|-------------|-----------------|
| AssignmentTest.cpp | `SQLVariant` operator/cast tests — no driver involvement. |
| BasicSheet.cpp | Spreadsheet I/O, not ODBC. |
| CalculateTest.cpp | `SQLVariant` arithmetic — pure C++. |
| CompareTest.cpp | `SQLVariant` comparison — pure C++. |
| Temporal.cpp | `SQLDate`/`SQLTime`/`SQLInterval` utility methods — pure C++. |
| TestBCD.cpp | `bcd` arbitrary-precision math — only the `SQL_NUMERIC_STRUCT` ↔ `bcd` round-trip portion is relevant, and port 1 covers it at the ODBC layer directly. |
| TestOperators.cpp | `SQLVariant` operator overloads — pure C++. |
| TestFormatting.cpp | Date/time/GUID string formatting — application layer. |
| TestOracleOID.cpp | Oracle-specific Object IDs — non-portable. |
| VariantFormatting.cpp | Locale-aware formatting — application layer. |
| Icd.cpp (3,107 LoC) | Integer-coded decimal arithmetic — pure C++. |
| TestSQL.cpp / TestDataSet.cpp | Test harness / ORM layer — neither tests ODBC behavior. |
| BasicDatabase.cpp `TestDBInfo` subtests | Per-RDBMS reporting of metadata — not driver conformance, more like documentation. |
| ODBCQueryTool/TestFiles/* | SQL scripts and example data; no portable C++ tests. |

---

## 7. Status legend

- ✅ RESOLVED — Fix implemented and tested
- 🔧 IN PROGRESS — Partially implemented or underway
- ❌ OPEN — Not yet addressed
- ⏯️ DEFERRED — Delayed or put on hold until another task is finished
