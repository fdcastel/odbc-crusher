# duckdb-odbc 1.5.2.0 — SIGSEGV during ODBC discovery

**Status: written, not yet executed.** See [Validation](#validation) before
sending this anywhere.

## What is being reported

`duckdb-odbc` 1.5.2.0 on Linux crashes with SIGSEGV during the *discovery*
sequence — the `SQLGetInfo` / `SQLGetTypeInfo` / `SQLGetFunctions` calls an
ODBC client makes immediately after connecting, before issuing any query.

This matters more than an ordinary probe failure. Discovery is what a driver
manager and essentially every BI tool, ORM and reporting client does on
connect, so a fault here fails ordinary applications before they run a single
statement.

Observed in odbc-crusher CI run
[34323148605](https://github.com/fdcastel/odbc-crusher/actions/runs/34323148605):

```
WARNING: Driver crashed during discovery phase: Segmentation fault (SIGSEGV)
Continuing with limited information...
```

That is all odbc-crusher can say. Its crash guard deliberately trades the
stack for the ability to finish the run, so there is no faulting address and
no backtrace — which is exactly what a bug report needs. Hence this program.

## Why a standalone reproducer

`repro.c` contains **no odbc-crusher code**. It is plain C against the driver
manager, so a DuckDB maintainer can build and run it without this repository.

It makes the same calls odbc-crusher's discovery makes, in the same order, one
at a time — announcing each on stderr *before* the call and flushing
immediately:

```
  -> SQL_DBMS_VER
     rc=0 len=0 value=""
  -> SQL_CATALOG_NAME
```

**The last `->` line is the call that crashed.** The four phases are:

| Phase | Calls |
|---|---|
| 1 | `SQLGetInfo` for 16 string values (`SQL_DRIVER_NAME` … `SQL_IDENTIFIER_QUOTE_CHAR`) |
| 2 | `SQLGetInfo` for 36 integer values, including the 23-entry `SQL_CONVERT_*` matrix |
| 3 | `SQLGetTypeInfo(SQL_ALL_TYPES)` and a `SQLFetch` / `SQLGetData` loop over columns 1–6 |
| 4 | `SQLGetFunctions(SQL_API_ODBC3_ALL_FUNCTIONS)` into a correctly sized 250-element array |

## Build and run

```sh
cc -Wall -Wextra -O0 -g repro.c -lodbc -o repro
./repro                                            # :memory: default
./repro "Driver={DuckDB Driver};Database=/tmp/x.duckdb;"
```

A crash shows up as a **signal**, not an exit code — shell reports 139 for
SIGSEGV. Exit `0` means every discovery call returned and the bug did not
reproduce in that configuration.

For a backtrace:

```sh
gdb -batch -ex run -ex bt --args ./repro
```

## Validation

`.github/workflows/repro-duckdb-discovery.yml` builds and runs this under
`gdb` against the version pinned in `.github/drivers.json`, on
`ubuntu-latest`, and uploads the logs. Manual dispatch only:

```sh
gh workflow run repro-duckdb-discovery.yml
```

With no argument it runs **both** connection strings, because they take
different paths through the driver: with no `Database=` the driver opens
`:memory:` and then declines to cache the instance
([`connect.cpp:187`](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/connect/connect.cpp#L187)),
so each connection gets its own database object. Whether the crash depends on
that is itself worth knowing, and is not yet established.

**Nothing below has been observed yet.** Fill these in from the workflow's
artifact before opening an issue, and delete any that turn out to be wrong:

- [ ] Which phase and which call crashes
- [ ] Backtrace, with the frame inside `libduckdb_odbc.so`
- [ ] Whether it reproduces with `Database=<file>` as well as `:memory:`
- [ ] Whether it reproduces without the unixODBC driver manager

Until those are filled in, the only claims this repository can actually
support are the two at the top: crusher saw a SIGSEGV somewhere in discovery,
and it could not say where.

## Environment

| | |
|---|---|
| Driver | `duckdb-odbc` 1.5.2.0, `libduckdb_odbc.so` from the [official release zip](https://github.com/duckdb/duckdb-odbc/releases/download/v1.5.2.0/duckdb_odbc-linux-amd64.zip) |
| Driver manager | unixODBC (`ubuntu-latest`) |
| Observed by | odbc-crusher, run 34323148605 |
| Full triage | [`reports/DUCKDB-v1.5.2.0-ODBC-CRUSHER-REPORT.md`](../../reports/DUCKDB-v1.5.2.0-ODBC-CRUSHER-REPORT.md), RC1 |
