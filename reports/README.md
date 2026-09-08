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
