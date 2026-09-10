# Is odbc-crusher finding the bugs we fix?

**A controlled before/after against one driver, two builds — measured twice.**

This report was first written on 2026-09-09 against crusher `68630c6`, and
concluded that of seventeen behavioural fixes separating the two builds, crusher
detected **three**. [`docs/IMPROVEMENT_PLAN_V2.md`](../docs/IMPROVEMENT_PLAN_V2.md)
is the work that followed. This is the re-measurement.

The first version's numbers are kept alongside the new ones throughout rather
than overwritten. A tool's effectiveness claim is only worth as much as the
measurement behind it, and a measurement you cannot see move is not one.

| | |
|---|---|
| Baseline | Firebird ODBC Driver **3.0.1.21** (official release) — source `FirebirdSQL/firebird-odbc-driver` @ [`dee624f`](https://github.com/FirebirdSQL/firebird-odbc-driver/commit/dee624fac13182f569532a5785e32eb5d27bed98) |
| Candidate | Firebird ODBC Driver **3.5.1-rc2** — upstream `master` @ `35fae3e` plus thirteen open pull requests |
| Crusher | `18ccae7`, resolved **once** for both halves (`T3`) and recorded in each report (`S4`) |
| Server | Firebird 5.0.3 via PSFirebird, `windows-2022`, `CHARSET=UTF8` |
| Run | [firebird-pair 34409542504](https://github.com/fdcastel/odbc-crusher/actions/runs/34409542504) — both halves, one dispatch |
| Provenance | `OK` on both, from the installed DLL's VERSIONINFO — `3.0.1.21` and `3.5.1.0` |
| Comparability | `PAIR_CONN_STRING_MATCH=true`, `PAIR_COMPARABLE=true` — the two `environment` blocks are identical |

The two runs differ in the driver and nothing else, and since **T3**/**T4** that
is now *checked* rather than merely intended: the manifest declares the pair,
`triage.ps1` refuses to proceed if the two connection strings differ by one
character, and both halves take their crusher binary from a single resolution.
The first measurement was two separate dispatches and nothing verified either
condition.

---

## Scoreboard

| | official 3.0.1.21 | patched 3.5.1-rc2 |
|---|---|---|
| Probes scored | 208 | 210 |
| Passed | 156 | 193 |
| Failed | 45 | 12 |
| Errors | 2 | 0 |
| **Pass rate** | **75.0 %** | **91.9 %** |
| Crashed categories | **2 — Descriptor Tests, Array Parameter Tests** | 0 |
| Findings | 47 | 12 |

**33 probes moved FAIL → PASS. None regressed.**

| | first measurement | this one |
|---|---|---|
| Probes moved | 20 | **33** |
| Pass-rate delta | 75.6 % → 86.8 % | **75.0 % → 91.9 %** |
| Distinct fixes detected | **3 of 17** | **12 of 17** |
| Crashed categories on the baseline | 1 | **2** |

The baseline's pass rate is still *optimistic*, and by more than before: two
categories now crash it rather than one. The second is new — not a regression in
the driver, but a probe that did not exist when the first measurement was taken.

---

## Part 1 — What crusher catches now

Thirty-three probes moved, but they are **twelve driver bugs**, not thirty-three.
Counting findings instead of causes is the single easiest way to overstate a
tool like this, and the sixteen numeric-conversion probes below are one bug.

| Fix | Then | Now | Evidence |
|---|---|---|---|
| **#292** numeric → text parameter | ✅ 14 probes | ✅ **16** | `R4` gave the two `wvarchar` cells a dialect fallback; they had been skipping on Firebird because they hardcode `NVARCHAR(20)`, which Firebird spells `NCHAR VARYING` |
| **#308** executor chosen at prepare time | ✅ 6 probes | ✅ 6 | unchanged |
| **#297** `SQLCopyDesc` access violation | ✅ category crash | ✅ + survivors | `S3`: the crash entry now names `test_copy_desc` and keeps the six probes that finished before it. Previously all of them vanished |
| **#311** cumulative `SQLRowCount` after an array execute | ❌ | ✅ **and it crashes** | `P11`. Predicted to fail; it *segfaults* 3.0.1.21 — a second crasher nobody had seen. `S3`'s entry names it with nine probes preserved |
| **#303 / #301** `SQLPrepare` discards `ROWS_FETCHED_PTR` / `ROW_STATUS_PTR` | ❌ | ✅ | `P2`. `after rowset 1: rows_fetched=999, status=[?, ?, ?, ?] — the counter was never written` |
| **#315 / #306** column-wise rowsets ignore `ROW_BIND_OFFSET_PTR` | ❌ | ✅ | `P3`. `offset 16 bytes; slots [-111, -111, -111, -111, 4, -111, -111, -111]` |
| **#314 / #307** `SQL_ATTR_KEYSET_SIZE` overwrites the rowset size | ❌ | ✅ | `P4`. `keyset size set to 7; rowset was 1, now 7` |
| **#304** settable attributes unreadable via `SQLGetStmtAttr` | ❌ | ✅ ×2 | `P5`: `8 of 8 pointer attributes accepted … unreadable: ROWS_FETCHED_PTR (HYC00), ROW_BIND_OFFSET_PTR (HYC00), …`. `Q2` covers `SQL_ATTR_CURSOR_SCROLLABLE`, which was being set and never read back — the bug itself |
| **#317 / #316** implementation descriptors and `SQLColAttribute` | ❌ | ✅ ×3 | `P6`/`P7`. `IRD concise=SQL_C_DEFAULT(99) - an unfilled record, name=''` while `SQLDescribeCol` on the same statement answers `SQL_INTEGER`/`ID`; and `SQL_DESC_LENGTH=9187201948296675328` — `0x7F7F7F7F00000000`, the guard pattern left in the upper half of a `SQLLEN` the driver wrote 32 bits of |
| **#302 / #300** value after a NULL on the same parameter | ❌ | ✅ | `P8`, and more precisely than predicted: `the value bound after a NULL came back as NULL for: NUMERIC(9,3), DECIMAL(18,2), BIGINT` — exactly the three whose `SQL_C_DEFAULT` resolves to `SQL_C_CHAR`, and not the three that resolve to a fixed-width C type |
| **#296 / #295** `SQL_C_GUID` parameter binding | ❌ | ✅ | `P9`. Round-tripped `A0EEBC99-9C0B-4EF8-…` as `41304545-4243-3939-…`, which is the ASCII of `A0EEBC99-9C0B-4E`: the driver stored the *text* and read the bytes back as binary |
| **#294** `SQL_DBMS_VER` is the engine version, not the product | ❌ | ✅ | `P13`. `06.03.1683 WI-V Firebird 5.0` against `05.00.1683 WI-V Firebird 5.0` — `atoi` on the prefix returns **6** for a Firebird **5** server |

**Three became twelve.** Nine of the nine new detections came from probes
written against a described symptom and then *checked against the pair*, which
is the discipline (`T1`) that made the difference rather than the probes
themselves.

---

## Part 2 — What crusher still cannot catch

Five of the seventeen. None of them is now a coverage gap in the sense the first
report meant — every one has a probe. What is left is harder and more
interesting.

| Fix | Status | Why |
|---|---|---|
| **#298** garbled diagnostic on a failed connect | ❌ **probe reaches the wrong path** | `P10` exists and asserts the right contract. All four runs — both platforms, both builds — return a textbook record: `08004`, native `-902`, 48 reported and 48 written, stable across attempts. #298 is `SQLException &e = (SQLException&)ex` inside `catch (std::exception&)`, a `reinterpret_cast` that is a **no-op when the caught object really is an `SQLException`** — and a database that does not exist makes the driver raise exactly that. Reaching the defect needs a throw of some *other* `std::exception` on a diagnostic-producing path. The probe is kept: it has a mock configuration that fails it, and it asserts a contract drivers do violate |
| **#299** element-0 stride | ⚠️ **demonstrable, but not by this pair** | `Q1` gave `verify_rows_persisted` the key column and `test_column_wise_array_binding` an assertion on it. 3.0.1.21 masks the stride behind #308's executor defect — only one set runs at all, so there is no stride to get wrong. The mock's `ColumnWiseStride=BufferLength` reproduces it: `expected IDs 100, 200, 300 but read back 100, 100, 100`, with `SQL_SUCCESS` and a correct processed count |
| **#313 / #309** dangling bind-offset after a mid-array throw | ⚠️ **written, unreachable on the baseline** | `P12` runs last in its category and `P11` crashes the driver before it, so it is absent from the baseline report. Registering it last was right and is not enough |
| **#310** `SQL_ATTR_PARAM_OPERATION_PTR` | ⚠️ **capable, not independently shown** | unchanged from the first measurement: the probe asserts `SQL_PARAM_UNUSED` correctly, but on 3.0.1.21 it fails for #308's reason |
| **#279** null-indicator offset | — | sits inside the same masked region as #299 |

Three of the five are the same phenomenon: **one defect hiding another.** The
baseline is a build with many bugs, and the earlier one in a code path decides
what the later one gets to demonstrate. No amount of probe-writing fixes that;
it needs a build with one fixed and the other not, or a mock lever — which is
what `Q1` now has and `P12` does not.

---

## Part 3 — What crusher found that the PRs do not fix

Twelve probes fail on **both** builds. These are live bugs in rc2 and are the
return on the exercise that has nothing to do with the fixes under test. The
list is unchanged from the first measurement except where noted.

| Cluster | Evidence |
|---|---|
| **`{fn …}` translation is incomplete** | `{fn LENGTH}`, `{fn YEAR}`, `{fn MONTH}`, `{fn DAYOFWEEK}`, `{fn DATABASE}` are advertised in `SQLGetInfo` and fail with `-104 Token unknown` when executed. `test_scalar_function_claim_vs_execute` cross-checks the driver's own claim against execution: `STRING:6/7 NUMERIC:4/4 TIMEDATE:3/5 SYSTEM:1/2` |
| **`SQL_DIAG_ROW_COUNT` is a 32-bit write into a 64-bit slot** | `-4294967296` = `0xFFFFFFFF00000000`: low half written, high half left as the caller's `-1`. OC-2, still open |
| **`SQL_ATTR_ASYNC_ENABLE` accepted then ignored** | set returns success, get reports OFF. OC-4, still open |
| **Missing spec-mandated error checks** | `SQLCloseCursor` succeeds with no cursor open (`24000`); `SQLSetConnectAttr` accepts attribute `99999` (`HY092`) |
| **Every engine exception is `HY000`** | a syntax error should be `42000` |
| **`PARAMSET_SIZE = 1` writes neither output** | `processed=0; status=65535`. The array executor runs only for paramset > 1 |
| **Truncated `SQLGetInfo` reports the written length, not the length available** | OC-5, still open |
| **Widechar round-trips collapse at the first non-ASCII codepoint** *(Linux only, new)* | `expected [U+0063 U+0061 U+0066 U+00e9 U+0020 U+20ac U+0020 U+6f22 U+5b57] got [U+0063]`. Present on **both** builds — see Part 5 |

> The `{CALL …}` row from the first version of this report is gone rather than
> struck through, because it was corrected there and the correction has been
> published for a full measurement cycle. For the record: it was a **crusher**
> bug, not a driver one — the probe hardcoded identifiers that exist in no
> database and counted an error return as "not translated". Given a real
> procedure the driver translates 7/7 (`R3`).

---

## Part 4 — What this exercise found in crusher itself

The first measurement found four defects in the tool. The re-measurement found
eight more, and they are of a different kind: the first four were probes that
were wrong, these are things the tool could not *survive* or could not *report*.

| | Finding |
|---|---|
| `S1` | **A wedged probe cost two entire CI runs and needed a driver-manager trace to locate.** A watchdog now names the stuck probe on stderr every 60 s. It reports and deliberately does **not** recover: abandoning a thread inside a driver call leaks the handle it holds and corrupts every result after it. First real use was the Linux arm, where it named `test_cancel_idle` within a minute |
| `S3` | **A crashed category discarded every probe that had already passed.** The report now keeps them and names the probe that did not return |
| `S4` | **The published reports carried `PWD=masterkey` in clear.** The row that asked for this said the connection string was "deliberately excluded"; it was not. Secrets are masked, the rest stays legible, and the schema number moved because that is a meaning change |
| `S5` | **The JSON report of a wedged run was far less complete than the text one** — ten categories against one, on the same driver. `run-crusher` invoked crusher twice and the second run met a server the first had wedged. One run now writes both, which also halves the stress-test wall clock for every driver |
| `S6` | **The snapshot rate limiter discarded exactly the runs worth keeping.** Nine categories completing inside a second were all skipped, then the driver wedged for 570 s with no `report_end()` to flush them. A category carrying an `ERROR` now bypasses the limiter |
| `S7` | **One crashed category cost the other thirteen.** The run carried on with the same connection handle. On Windows that works; on Linux the next ODBC call on that handle never returns. Measured against a mock that reproduces it: **204 passed / 1 error** with the fix, **90 passed / 63 errors** without |
| `P15` | **A garbled diagnostic makes unixODBC read out of bounds** — `__sprintf_chk` → `SQLDriverConnect` in `libodbc.so.2`, caught by ASan. Not our bug, and the sharpest available answer to "why does an unreadable SQLSTATE matter": it corrupts the driver manager's memory before the application is handed anything |
| `P17` | **A SIGSEGV the crash guard does not catch**, in `test_array_row_count_matches_its_claim` on Linux — though it catches the same probe's fault on Windows and `test_copy_desc`'s SIGSEGV on Linux. Open |

And one rule, which is the part most likely to outlast the probes:

> **A probe that passes against both builds has not been shown to detect
> anything.** (`Q4`, now step 8 of `AGENTS.md`.)

A sweep for the shape it describes found six probes that reported a pass over
the very bug they were named for — including `test_cursor_scrollable_attr`,
which set an attribute and never read it back, which *is* #304.

---

## Part 5 — The Linux arm

The first measurement ran on Windows only, because the official 3.0.x line
publishes Inno Setup installers and no Linux `.so` for 3.0.1.21 was ever
released. Two of the seventeen fixes are Linux-only and were therefore
structurally invisible. `U2` built the arm: CI compiles 3.0.1.21 from its pinned
commit and pairs it against the published 3.5.1-rc2 Linux tarball.

**#316 item 6 is confirmed outright, and not by a probe.**

| symbol | 3.0.1.21 | 3.5.1-rc2 |
|---|---|---|
| `SQLColAttribute` | **MANGLED** (C++ name) | exported |
| `SQLColAttributeW` | **MANGLED** | exported |
| `SQLColAttributes` (ODBC **2**) | exported | exported |

unixODBC cannot resolve the ODBC 3 symbol, silently falls back to the ODBC 2
one, and answers every call with the wrong function with no error anywhere. **No
probe can see that** — the fallback is silent by design and the results are only
subtly wrong. The export table is the only place the truth is written, so both
Linux jobs now log `nm -D`.

**#288 is not fixed in 3.5.1-rc2, so the pair cannot bracket it.** Crusher
*does* catch its symptom — three Unicode probes fail with precise evidence — but
they fail identically on both halves. The patched tree's own CI says why: *"ASAN
is temporarily disabled — re-enable once the Linux widechar conversion paths in
MainUnicode.cpp are rewritten (#287 Tier 1b, draft PR #291)."* A draft PR,
unmerged at that tag. Listing #288 among the seventeen was an error in the
first report, carried into `U2`'s row and corrected there.

The arm also found what only a second platform could: `test_copy_desc` SIGSEGVs
on Linux as well as access-violating on Windows, **and the connection does not
survive it there** — which is `S7`, and which the entire Windows pair had no way
to expose.

---

## Verdict

**Crusher is now effective across most of the bug population it is aimed at, and
the remaining gap has changed character.**

The first measurement's gap was coverage: fourteen of seventeen fixes had *no
probe that touched the API at all*. That is closed. Of the five not detected
today, four have probes that are correct and blocked by something else — a
defect masking a defect, or a fix that was never made — and one (`#298`) has a
probe that reaches a neighbouring code path.

Three things are worth stating plainly against any temptation to read 12/17 as a
grade:

**The seventeen are a known seventeen.** Every probe in Part 1 was written from
a described symptom in a merged pull request. Passing them means crusher would
have caught *these*; it says nothing about the next seventeen. The durable
output of this work is `Q4`'s rule and `T1`'s discipline — the probes are the
evidence those were applied once.

**Two of the twelve were made sharper by the pair than by the prediction.**
`P11` was predicted to fail and instead crashes the driver. `P8` was predicted
to fail and instead split its six types cleanly along `SQL_C_DEFAULT`
resolution. Neither would have been written from the prediction alone, which is
the argument for observing before claiming.

**The tool's own survivability was the larger finding.** Eight of the entries in
Part 4 are about crusher, not Firebird, and they were found by pointing it at a
real driver on a platform it had not been run against. A conformance prober that
loses its report when the driver misbehaves is least useful exactly when it
matters most — and before this exercise, a wedged run and a run that produced
nothing were indistinguishable from the artifact.

---

## Recommendations, in value order

1. **Report `P15` upstream to unixODBC.** It affects every application using
   unixODBC with any driver that mis-describes a diagnostic record, not just
   this one. Outward-facing, so it needs a decision rather than a commit.
2. **Report `P16` / `P17` to the Firebird ODBC project.** A connection that does
   not survive a fault, and an array-execute segfault that defeats a crash
   guard. Both are Linux-only and both are reproducible from the pinned commit.
3. **Close `S8`.** A run killed without reporting still loses everything the
   snapshot limiter skipped. Measure the limiter's real cost first — its stated
   justification predates `S5` halving CI's crusher invocations.
4. **Give `P12` a mock lever**, the way `Q1` got one. It is the last probe in
   this plan asserting something no configuration anywhere can falsify.
5. **Re-measure when #291 lands.** #288 will then be bracketed by the Linux
   pair, and the three Unicode probes that currently fail on both halves become
   a thirteenth detection.

---

## Per-driver triage reports

- [`FIREBIRD-OFFICIAL-v3.0.1.21-ODBC-CRUSHER-REPORT.md`](./FIREBIRD-OFFICIAL-v3.0.1.21-ODBC-CRUSHER-REPORT.md)
- [`FIREBIRD-PATCHED-v3.5.1-rc2-ODBC-CRUSHER-REPORT.md`](./FIREBIRD-PATCHED-v3.5.1-rc2-ODBC-CRUSHER-REPORT.md)
- [`docs/IMPROVEMENT_PLAN_V2.md`](../docs/IMPROVEMENT_PLAN_V2.md) — every row carries its observed verdict pair, or a stated reason it cannot have one
