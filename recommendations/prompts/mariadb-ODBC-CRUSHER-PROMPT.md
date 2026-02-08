# ODBC Crusher Analysis Prompt – mariadb

**Driver version**: `3.1.15`
**Source code**: `tmp/external/mariadb-connector-odbc` (checked out at tag `3.1.15`)
**Reports**:
- `tmp/external/mariadb-connector-odbc/crusher-report.txt`
- `tmp/external/mariadb-connector-odbc/crusher-report.json`

## Instructions

Do a critical analysis of what `odbc-crusher` says about the `mariadb` driver and review its source code (at exactly this version) to see what the driver actually implements.

### GOAL

For each **failed** or **skipped** test, investigate its result against the real `mariadb` sources:

1. **If the odbc-crusher report is CORRECT** (the driver really has the deficiency):
   Write a recommendation in `recommendations/mariadb_ODBC_RECOMMENDATIONS.md` explaining what the `mariadb` developers should do to fix it.

2. **If the odbc-crusher report is WRONG** (the driver is fine, odbc-crusher misjudged it):
   Record it as a bug in `PROJECT_PLAN.md` for future fix.
