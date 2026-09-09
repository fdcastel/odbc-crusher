---
name: triage-driver
description: Run odbc-crusher against a single registered ODBC driver in CI, clone its source at the matching tag, and produce a triage report classifying every FAIL/ERROR as BUG_IN_DRIVER / BUG_IN_CRUSHER / DRIVER_LIMITATION / INCONCLUSIVE. Use whenever the user asks to triage a driver, debug a stress-test failure against the driver source, or compare a crusher report to upstream code.
argument-hint: "<driver-name> [run-id]"
---

# `/triage-driver <driver> [run-id]`

Three steps: run `triage.ps1`, hand its output to an analysis sub-agent, brief
the user. The script owns everything mechanical — manifest, CI run, clone,
download, and the integrity gates. You own the two judgement calls: whether to
proceed when it declines, and what to tell the user at the end.

Valid driver names are the keys of `.github/drivers.json`. The script lists them
for you if the argument is wrong, so do not memorise them and do not guess.

---

## Step 1 — run the preflight script

```powershell
pwsh -File .claude/skills/triage-driver/triage.ps1 -Driver <driver>
```

Add `-RunId <id>` if the user named an existing run, or wants to re-analyse
without spending CI time. Other switches, used only when the situation calls for
them: `-NoDispatch` (reuse the newest completed run), `-AllowPartial` (see exit
6), `-SkipClone`, `-Offline` (analyse what is already on disk, no network),
`-OutDir`.

A fresh dispatch waits for CI — typically 2–5 minutes for one driver. Run it in
the background or raise the tool timeout; the default 120 s will cut it off.

The script prints a `----- BEGIN PREFLIGHT -----` block and writes the same
content to `<OUT_DIR>/preflight.txt`. **Read the block; do not re-derive
anything from it.** It already contains the run URL, the resolved paths, the
version verdict and the summary counts.

### Exit codes

| Code | Meaning | What to do |
|---|---|---|
| 0 | Ready | Go to step 2. |
| 2 | Unknown driver, or not run from the repo | The script printed the valid names — relay them and ask which one. |
| 3 | Dispatch or CI failure | Relay `ABORT_REASON`. Usually `gh auth status`, no push permission, or no runner. |
| 4 | Report missing or unusable | Relay `NO_JSON_REASON` verbatim — it says *why*, from the text report. Do not substitute a guess. |
| 5 | Version mismatch | Stop. The binary is not the source. Relay `PROVENANCE_DETAIL` and suggest fixing `.github/drivers.json` or the CI pin, then re-running. |
| 6 | Report is a partial snapshot | Tell the user the run was killed before finishing and offer two options: re-run the stress test, or re-run with `-AllowPartial` to triage what survived under a banner. **Do not pass `-AllowPartial` on your own** — a partial report is a smaller truth, and whether that is good enough is the user's call. |

On any non-zero exit: report it and stop. Do not produce a partial triage report.

---

## Step 2 — spawn the analysis sub-agent

**First: if `FINDINGS_COUNT=0`, skip this step.** There is nothing to classify —
tell the user the driver passed cleanly, give them the summary counts and the
run URL, and stop. Spawning an agent to write "no findings" costs minutes and
produces a document nobody needs. (The mock driver hits this on every green
run.) Still relay any non-clean run-integrity fact from step 3; a run with zero
findings *and* crashed categories is not a clean run.

Otherwise, use the Agent tool, `subagent_type: "general-purpose"` (it needs Write).

- `description`: e.g. `"Triage duckdb v1.5.2.0"`
- `prompt`:

```
Read .claude/skills/triage-driver/analyze.md and follow it exactly.

OUT_DIR = <the OUT_DIR value from the preflight block>
```

That is the whole prompt. `analyze.md` is self-contained and reads every other
value it needs from `<OUT_DIR>/preflight.txt`. Do not paste the preflight block
into the prompt, do not summarise the findings for it, and do not add
instructions of your own — you have not read the findings, and a hint from you
would bias a classification you are not in a position to make.

> H11: this step used to hand the sub-agent a copy of `analyze.md` with nine
> `<PLACEHOLDER>` tokens find-and-replaced by hand. One of them (`<VERSION>`)
> referenced a variable that was never set for `mock-driver`, so that driver's
> reports were written to a path with an empty version in the filename.

---

## Step 3 — brief the user

The sub-agent's reply carries everything you need, including the first punch-list
item. Verify the file exists, then write **one short message**:

- the report path, as a clickable markdown link
- total / passed / failed / errors / skipped, and the pass rate
- the four classification counts
- the first punch-list item
- **the run-integrity verdict, if it is not clean** — say plainly when
  categories crashed (their probes were discarded, so the pass rate is
  optimistic), when the run was filtered, and when `PROVENANCE` is
  `UNVERIFIABLE`. These change how much the numbers are worth and the user
  cannot see them unless you say so.

Two or three sentences plus the counts. The report file is the deliverable; the
message is the receipt.

---

## Notes

- Needs `gh` authenticated (`gh auth status`) and `git`. **Not** `jq` — the
  script uses PowerShell's `ConvertFrom-Json`, because `jq` is absent on the
  main development machine and the old bash version misread its "command not
  found" as "that driver is not in the manifest".
- Everything under `tmp/triage/` is gitignored. A report worth keeping gets
  promoted to `reports/` — see `reports/README.md` (H9).
- Clones are `--depth 50` and are reused across runs when already at the right
  tag. They accumulate; `tmp/triage/` was 97 MB at the time this was written, so
  delete driver subdirectories you are done with.
- To triage every driver at once, `fetch-stress-test.ps1` loops this same script
  over the whole manifest.
