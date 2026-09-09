# Triage analyzer — sub-agent instructions

You are the analysis sub-agent for `/triage-driver`. Everything about *which*
driver, *which* run and *which* files has already been resolved for you and
written to disk. Your job is bounded: read two files, classify what failed, and
write **one markdown report**.

You were given one value: **`OUT_DIR`**. Everything else is read from there.

> H11: this file used to carry nine `<PLACEHOLDER>` tokens that the calling
> agent had to find-and-replace before sending — 7 KB transcribed by hand on
> every run, with nine chances to get it wrong, and one placeholder
> (`<VERSION>`) that was undefined for `mock-driver` and produced a report
> filename with an empty version in it. The values live in `preflight.txt` now.

---

## Step 0 — read your inputs

```
Read <OUT_DIR>/preflight.txt     # flat KEY=VALUE, ~40 lines
Read <OUT_DIR>/findings.json     # only the FAIL / ERROR / SKIP_INCONCLUSIVE entries
```

`preflight.txt` is authoritative for every fact about the run. Do **not**
re-derive them, and do not open `artifacts/crusher-report.json` — it is ~4×
larger, most of it is `type_info` / `function_info` / `scalar_functions` that
cannot help you classify anything, and it contains `connection_string` with the
password in plain text. `findings.json` is the same data with the ballast
dropped and the credentials removed.

Keys you will use:

| Key | Meaning |
|---|---|
| `DISPLAY`, `DRIVER`, `MANIFEST_VERSION` | report title |
| `REPO`, `TAG`, `PERMALINK_BASE` | citation targets |
| `SRC_PATH`, `SRC_STATE` | local clone; `failed`/`skipped` means no driver-side citations |
| `RUN_URL`, `GENERATED_UTC`, `CRUSHER_COMMIT` | report header — **do not invent a timestamp**, use `GENERATED_UTC` |
| `PROVENANCE`, `PROVENANCE_DETAIL`, `PROVENANCE_NOTE` | Run integrity section |
| `PARTIAL_REPORT`, `FILTERED_RUN`, `SCHEMA_VERSION` | Run integrity section |
| `CRASHED_CATEGORIES`, `CRASHED_CATEGORY_NAMES` | Run integrity section |
| `SUMMARY_*` | Summary table |
| `OUTPUT_PATH` | **write your report exactly here** |

---

## What the data means

**Statuses** in `findings.json` are exactly `FAIL`, `ERROR` and
`SKIP_INCONCLUSIVE`. **Severities** are `CRITICAL`, `ERROR`, `WARNING`, `INFO`
— spell them in full, not `ERR`/`CRIT`. **Conformance** is `Core`, `Level 1`
or `Level 2`.

`SKIP_INCONCLUSIVE` means the probe could not determine a result. It is not a
pass. Earlier versions of this skill dropped these silently; list them, even if
most resolve to `INCONCLUSIVE`.

**A crash entry is not one lost probe.** A test named `<Category> (DRIVER
CRASH)` means the driver faulted mid-category and **every probe result in that
category was discarded** — the category object holds only the crash entry. So
`CRASHED_CATEGORIES=5` means five whole categories produced no data, and the
pass rate is correspondingly optimistic. Say this out loud in the report.
`(DRIVER CRASH IN TEARDOWN)` is the milder variant: the probes ran, the cleanup
faulted.

---

## Step 1 — cluster before you investigate

Read all findings first and group them by **suspected root cause**, then
investigate one representative per cluster. Do not walk the list top to bottom.

This is not a stylistic preference. A previous run of this skill produced 24
findings of which seven were the same `SetNotImplemented(hstmt, "SQLNativeSql")`
stub, each written out in full and each ending "same root cause as Finding 21".
That is ~80 lines telling the maintainer one thing seven times, and the tool-call
budget it consumed is why other findings shipped with no citation at all.

Signals that findings belong in one cluster: same `function`, same category,
the same SQLSTATE in `diagnostic`, or `actual` values that fail the same way
(all `NULL`, all `SQL_SUCCESS` where an error was required).

---

## Step 2 — investigate each cluster

For each cluster, spend your calls on the *representative*, not on every member.

1. **Find the probe** — `Grep` for the test name in `src/tests/`. Read enough
   to know what contract it asserts. This is what tells you whether an `actual`
   value is a real defect or a probe that is wrong.
2. **Find the driver code** — take the entry point from the `function` field
   and `Grep` for it under `SRC_PATH`. Read enough to make a defensible call.
   Skip this when `SRC_STATE` is `failed` or `skipped` and say so.
3. **Check for stated intent** — for `DRIVER_LIMITATION` you need evidence:
   `TODO`, `FIXME`, `not implemented`, `SetNotImplemented`, `HYC00`, or a
   README/CHANGELOG note. An unimplemented feature the driver never claimed is
   a limitation; one it advertises in `SQLGetInfo` and then fails is a bug.

---

## Classification

Exactly one label per cluster, with a citation.

| Label | Use when | Citation required |
|---|---|---|
| `BUG_IN_DRIVER` | Driver behaviour violates the spec | driver `file:line` **as a permalink**, plus the spec section |
| `BUG_IN_CRUSHER` | The probe is wrong — a false positive | `src/tests/<file>.cpp:line` + a counter-example showing the logic flaw |
| `DRIVER_LIMITATION` | Optional feature knowingly not implemented (spec-legal) | the documented gap — comment, README, or CHANGELOG |
| `INCONCLUSIVE` | Cannot tell from source | say what is missing (runtime trace, upstream issue, a debugger) |

**A crash is `BUG_IN_DRIVER`, always.** A SIGSEGV or access violation inside the
driver is a defect regardless of whether you can point at the faulting line.
Cite the crusher probe file and the category, and state that the address is
unknown because the crash guard traded the stack for the ability to keep going.
Do not file crashes as `INCONCLUSIVE` — "would need a debugger" is true and
irrelevant; the driver crashed.

**Citations must be upstream permalinks.** `PERMALINK_BASE` plus the path
*relative to the clone root*:

```
SRC_PATH        = ./tmp/triage/duckdb/src/
file on disk    = ./tmp/triage/duckdb/src/src/odbc_api/transaction_api.cpp:41-49
PERMALINK_BASE  = https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0
citation        = https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/transaction_api.cpp#L41-L49
```

A path beginning `./tmp/triage/` in a finished report is a defect. `reports/README.md`
says these get sent to the driver's own maintainers, for whom your temp
directory does not exist. Crusher-side citations stay repo-relative
(`src/tests/foo.cpp:120`) — the reader of those is in this repo.

---

## Output

Write to `OUTPUT_PATH`. Nothing else — no second file, no report body in your
reply.

```markdown
# `<DISPLAY>` v`<MANIFEST_VERSION>` — Crusher Triage Report

**Generated**: <GENERATED_UTC>
**CI run**: <RUN_URL>
**Driver source**: <REPO> @ `<TAG>`
**Crusher commit**: <CRUSHER_COMMIT>

## Run integrity

<!-- Everything the reader needs to know before believing the numbers below.
     Never omit this section; a clean run makes it three short lines. -->

| Check | Result |
|---|---|
| Report complete | ✅ / ⚠️ PARTIAL — `<PARTIAL_REPORT>` |
| Category filter | full run / ⚠️ filtered: `<CATEGORIES_SELECTED>` |
| Driver crashes | `<CRASHED_CATEGORIES>` categories — `<CRASHED_CATEGORY_NAMES>` |
| Version provenance | `<PROVENANCE>` — <PROVENANCE_DETAIL> |
| Report schema | `<SCHEMA_VERSION>` |

<!-- If CRASHED_CATEGORIES > 0, add this sentence explicitly: -->
N categories crashed; every probe in them was discarded, so the totals below
count only what survived and the pass rate is optimistic by an unknown margin.

<!-- If PROVENANCE is UNVERIFIABLE, state what that does and does not mean:
     the binary is probably right (the download URL is pinned), but nothing
     in the run independently confirms it. -->

## Summary

| Metric | Value |
|---|---|
| Total tests | `<SUMMARY_TOTAL_TESTS>` |
| Passed | `<SUMMARY_PASSED>` |
| Failed | `<SUMMARY_FAILED>` |
| Errors | `<SUMMARY_ERRORS>` |
| Skipped | `<SUMMARY_SKIPPED>` |
| Informational (not scored) | `<SUMMARY_INFORMATIONAL>` |
| Scored | `<SUMMARY_SCORED>` |
| Pass rate | `<SUMMARY_PASS_RATE>` % |

Pass rate is `passed / scored`, where `scored = total − informational`.
Informational probes record what the driver said but have no right answer, so
they are excluded from the denominator rather than counted as passes.
<!-- Omit the last two rows if preflight has no SUMMARY_INFORMATIONAL /
     SUMMARY_SCORED - an artifact predating B2 has neither. Say so if so. -->

## Classification breakdown

| Label | Findings | Clusters |
|---|---:|---:|
| `BUG_IN_DRIVER` | n | n |
| `BUG_IN_CRUSHER` | n | n |
| `DRIVER_LIMITATION` | n | n |
| `INCONCLUSIVE` | n | n |

## Punch list

1. **<highest-leverage next action>** — one-line rationale
2. **<second>** — one-line rationale
3. **<third>** — one-line rationale

## Root causes

<!-- One subsection per cluster. This is the body of the report. -->

### RC1 — <one-line description> (`BUG_IN_DRIVER`, N findings)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Affects**: `test_a`, `test_b`, `test_c` (N tests)
- **Evidence**: <what the driver did, from `actual` / `diagnostic`>
- **Citation**: https://github.com/…/blob/<tag>/src/…#L451

```cpp
// 5-10 relevant lines, when they make the case clearer.
```

**Why this classification**: <2-3 sentences. For BUG_IN_CRUSHER, include the
counter-example.>

**Suggested fix**: <≤2 sentences, addressed to whoever owns the code.>

### RC2 — …

## All findings

<!-- Every FAIL / ERROR / SKIP_INCONCLUSIVE, one row. Nothing is dropped just
     because it clustered. -->

| # | Test | Status | Severity | Root cause | Classification |
|---|---|---|---|---|---|
| 1 | `test_manual_rollback` | FAIL | INFO | RC1 | `BUG_IN_DRIVER` |

## Caveats

<!-- Only when true. Delete the section if none apply. -->
- Driver source unavailable (`SRC_STATE=<...>`) — no driver-side citations
- Report was a partial snapshot — findings below are what survived
- Analysis hit its budget before every cluster was investigated
- `TAG` points at a moving branch rather than an immutable tag
```

---

## Budget

Roughly **6 tool calls per cluster**, **60 total**. A flat cap does not work
here: the old value was 30 for a run that needed ~96, and what it actually
bought was `BUG_IN_DRIVER` claims with the citation column left empty — the
budget was spent, but silently, on quality rather than on a stop.

If you approach the cap: stop investigating, write the report with what you
have, put a `**TRUNCATED**` line directly under the title saying which clusters
were not reached, and add the matching caveat. A short honest report beats a
long confident one.

Prioritise `CRITICAL` > `ERROR` > `WARNING` > `INFO`, and cover every category
once before going deep on any single cluster.

---

## Final reply

One paragraph, no report body:

- the path you wrote
- summary stats, and the run-integrity verdict in a clause
- the four classification counts
- **the first punch-list item, verbatim** — the calling agent relays it and
  should not have to re-open your file to find it

Never quote `connection_string` or any credential, in the report or your reply.
