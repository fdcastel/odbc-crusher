# Triage reports

Reports in this directory are **published** ones: a triage that was worth
keeping, usually because it was sent to the driver's own maintainers as a
hard-linkable reference.

They are not where `/triage-driver` writes. That skill writes to
`tmp/triage/<driver>/`, which is gitignored, because most triages are working
output — run against a snapshot of a driver on a particular day, useful for
an afternoon, and misleading a month later when the driver has moved.

**Promoting one:** copy it here, name it
`<DRIVER>-v<VERSION>-ODBC-CRUSHER-REPORT.md`, and keep the driver version in
the filename. A report with no version in its name cannot be checked against
anything later.

IMPROVEMENT_PLAN.md **H9**: the two locations existed with nothing saying
which was which, so a reader finding one committed report at the repo root
had no way to tell whether it was the skill's output or something deliberate.
It was deliberate.

## The Firebird pair

Three files here belong together and were produced in one sitting (**H17**,
**H18**, **H19**):

- `FIREBIRD-OFFICIAL-v3.0.1.21-ODBC-CRUSHER-REPORT.md` — the official release.
- `FIREBIRD-PATCHED-v3.5.1-rc2-ODBC-CRUSHER-REPORT.md` — upstream `master`
  plus thirteen open PRs.
- `FIREBIRD-3.0.1.21-vs-3.5.1-rc2-CRUSHER-EFFECTIVENESS.md` — what the
  difference between the two says about **crusher**, not about Firebird.

The first two are ordinary triages and follow the naming rule above. The third
is a different kind of document and deliberately breaks it: it is not a triage
of a driver, it is a measurement of this tool against seventeen known fixes, so
it carries both versions in its name instead of one. Keep them together; the
comparison is worthless without the two reports it is drawn from, and the two
reports are only comparable because the pair was run under identical conditions.
