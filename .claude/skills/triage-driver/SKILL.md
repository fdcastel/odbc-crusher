---
name: triage-driver
description: Run odbc-crusher against a single registered ODBC driver in CI, clone its source at the matching tag, and produce a triage report classifying every FAIL/ERROR as BUG_IN_DRIVER / BUG_IN_CRUSHER / DRIVER_LIMITATION / INCONCLUSIVE. Use whenever the user asks to triage a driver, debug a stress-test failure against the driver source, or compare a crusher report to upstream code.
argument-hint: "[driver-name]"
---

# `/triage-driver <driver>` — orchestration

You have been invoked with one argument (`$ARGUMENTS`): the driver
name. The complete list of valid driver names is whatever appears in
`.github/drivers.json` (currently: `postgresql`, `mariadb`, `mysql`,
`duckdb`, `clickhouse`, `mock-driver`).

Your job is to drive seven steps in order. **Do not skip steps; do not
reorder them.** If anything in steps 1–5 fails, abort with a clear
message — do not produce a partial triage report.

All paths below are relative to the repository root (the directory
that holds `.github/drivers.json` and `mock-driver/`). Run all `gh`
commands and `git clone`s from there. The skill does not assume any
specific absolute path.

---

## Step 1 — Read the manifest

```bash
jq -e ".drivers[\"$ARGUMENTS\"]" .github/drivers.json
```

If `jq` exits non-zero, the driver isn't in the manifest. Tell the user
the available driver names (`jq -r '.drivers | keys[]' .github/drivers.json`)
and stop.

Capture these values into shell variables for reuse below:

- `DRIVER=$ARGUMENTS`
- `VERSION` ← `.drivers[$ARGUMENTS].version`
- `REPO`    ← `.drivers[$ARGUMENTS].source.repo`
- `TAG`     ← `.drivers[$ARGUMENTS].source.tag`
- `DISPLAY` ← `.drivers[$ARGUMENTS].display_name`

**Mock-driver special case.** If `$ARGUMENTS` is `mock-driver`, `REPO`
will be `"self"` and `TAG` will be `"workspace"` — skip the clone in
step 3 and use `mock-driver/` as the source directory in step 6.

---

## Step 2 — Dispatch the CI workflow

```bash
gh workflow run stress-test.yml -f driver=$DRIVER
```

Workflow dispatch is asynchronous and `gh workflow run` does not return
the new run ID. Wait briefly, then capture the most-recent stress-test
run's ID — that's the one we just triggered:

```bash
sleep 8
RUN_ID=$(gh run list --workflow stress-test.yml --branch master --limit 1 \
  --json databaseId -q '.[0].databaseId')
echo "Triggered run: $RUN_ID"
```

If `gh workflow run` fails (auth, no permissions, branch protection),
propagate stderr to the user and stop.

---

## Step 3 — In parallel, clone the source

While CI is running, clone the driver source locally so the analysis
agent has somewhere to grep. Skip the clone for `mock-driver`.

```bash
mkdir -p ./tmp/triage/$DRIVER
git clone --depth 50 --branch "$TAG" "$REPO" \
  ./tmp/triage/$DRIVER/src
```

If the clone fails (tag missing upstream, repo gone), do **not** abort —
record the failure in a variable so the analysis agent can note it in
the report header, and proceed. The triage can still flag findings
against the *crusher* code even without driver source.

---

## Step 4 — Wait for CI + download artifact

Run `gh run watch $RUN_ID` (foreground) or use the Monitor tool to wait
asynchronously. When the run completes:

```bash
mkdir -p ./tmp/triage/$DRIVER/artifacts
gh run download "$RUN_ID" -n report-$DRIVER \
  -D ./tmp/triage/$DRIVER/artifacts/
ls ./tmp/triage/$DRIVER/artifacts/
```

Expected files: `crusher-report.txt`, `crusher-report.json`,
`actual_version.txt`. If any are missing, abort and explain — the
workflow may have failed before the artifact-upload step.

---

## Step 5 — Verify version provenance

The whole triage is meaningless if the binary version doesn't match the
source tag. Before writing the report, cross-check three values:

- `MANIFEST_VERSION` from step 1 (`$VERSION`)
- `RUNTIME_VERSION` from `actual_version.txt`
- `JSON_REPORTED_VERSION` from `.driver_info.driver_version` in
  `crusher-report.json`

```bash
MANIFEST=$(jq -er ".drivers[\"$DRIVER\"].version" .github/drivers.json)
RUNTIME=$(cat ./tmp/triage/$DRIVER/artifacts/actual_version.txt)
REPORTED=$(jq -er '.driver_info.driver_version' \
  ./tmp/triage/$DRIVER/artifacts/crusher-report.json)
echo "manifest=$MANIFEST  runtime=$RUNTIME  reported=$REPORTED"
```

**For `mock-driver`**, `$MANIFEST` is `"workspace"` and `$RUNTIME` is
the master CI run's commit SHA — they won't match by string equality.
Skip this check for `mock-driver`.

For all other drivers: if `$MANIFEST` doesn't appear as a substring of
either `$RUNTIME` or `$REPORTED`, **abort with a VERSION_MISMATCH
error**. Do not write the report. The user's manifest is wrong, or the
workflow's pin failed silently — either way, fix that first.

(Substring match, not exact — `1.4.4.0` should match `1.4.4` even if
the driver only reports three segments.)

**Known driver quirks** that pass anyway because of the substring rule:
- DuckDB v1.4.4.0 returns `"03.51.0000"` (its ODBC compliance level)
  from `SQLGetInfo(SQL_DRIVER_VER)`, NOT the driver's own version. The
  match still passes because `actual_version.txt` carries `1.4.4.0`
  verbatim. Note this in the analysis report's "Version provenance"
  section so the reader doesn't waste time chasing the disparity.

---

## Step 6 — Spawn the analysis sub-agent

The heavy lifting (per-finding source-grep + classification + writing
the markdown report) happens in a sub-agent. Use the Agent tool with:

- `subagent_type`: `"general-purpose"` (it needs Write to produce the
  output file)
- `description`: e.g. `"Triage duckdb v1.4.4.0"` — substitute real
  values
- `prompt`: read `.claude/skills/triage-driver/analyze.md` and
  substitute these placeholders before sending. The placeholder syntax
  in `analyze.md` is plain `<NAME>` text — find/replace each one:

| Placeholder in `analyze.md` | Substitute with |
|---|---|
| `<DRIVER>` | `$DRIVER` (the argument value) |
| `<VERSION>` | `$MANIFEST` |
| `<DISPLAY>` | `$DISPLAY` |
| `<REPO>` | `$REPO` |
| `<TAG>` | `$TAG` |
| `<RUN_URL>` | `https://github.com/<owner>/<repo>/actions/runs/$RUN_ID` (use `gh repo view --json nameWithOwner -q '.nameWithOwner'` for `<owner>/<repo>`) |
| `<REPORT_PATH>` | `./tmp/triage/$DRIVER/artifacts/crusher-report.json` |
| `<SRC_PATH>` | `./tmp/triage/$DRIVER/src/` (or `./mock-driver/` for the mock-driver special case) |
| `<OUTPUT_PATH>` | `./tmp/triage/$DRIVER/$DRIVER_UPPER-v$VERSION-ODBC-CRUSHER-REPORT.md` where `DRIVER_UPPER=$(echo $DRIVER | tr '[:lower:]' '[:upper:]')` |
| `<SOURCE_CLONE_FAILED>` | `"true"` or `"false"` based on step 3 |

Pass the substituted prompt verbatim to the sub-agent. Do **not** add
extra preamble — the sub-agent prompt is self-contained.

---

## Step 7 — Confirm the report and brief the user

When the sub-agent returns, verify the output file exists:

```bash
test -f "$OUTPUT_PATH" && wc -l "$OUTPUT_PATH"
```

Then in **one short message** to the user, report:

- Path of the written report (clickable markdown link)
- Top-level summary: total / passed / failed / skipped from the JSON's
  `summary` block
- Classification breakdown: how many of each label
  (BUG_IN_DRIVER / BUG_IN_CRUSHER / DRIVER_LIMITATION / INCONCLUSIVE)
- The first item from the punch list

Keep this brief — the report file has the full content. Two or three
sentences plus the classification counts is enough.

---

## Failure modes — handle these explicitly

| When | Do |
|---|---|
| Driver argument missing (`$ARGUMENTS` empty) | List available driver names from manifest, ask user to pick one. |
| Driver not in manifest | Same — show available names, stop. |
| `gh workflow run` fails | Propagate stderr; common causes: not authenticated (`gh auth status`), no push permissions on the repo, branch protection. |
| CI run takes too long (>15 min) | Abort the wait; tell the user the run ID and workflow URL so they can check manually. |
| Artifact missing | Workflow likely failed before upload; show the user the run URL and the failed step name (`gh run view $RUN_ID --log-failed | head -40`). |
| Source clone fails | Continue, but tell the analysis sub-agent via `<SOURCE_CLONE_FAILED>=true` and let it caveat the report. |
| Version mismatch (step 5) | Abort. Print the three values clearly. Suggest: update `.github/drivers.json` so `version` matches what was actually installed, then re-run. |
| Sub-agent errors / times out | Save its partial output if any; mark the report file with a `**TRUNCATED**` header. |

---

## Notes for invocation

- This skill assumes `gh` is authenticated (`gh auth status` should
  succeed). If not, prompt the user to run `gh auth login` first.
- The CI run takes ~2–5 minutes for a single-driver dispatch.
- The clone is `--depth 50` — enough for `git log`/`git blame` context
  but fast. Use a deeper clone (`git fetch --unshallow` inside
  `./tmp/triage/$DRIVER/src`) if the analysis needs more history.
- All paths under `./tmp/triage/` are gitignored — clones and artifacts
  never leak into commits.
