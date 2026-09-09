# The fixture contract

**What odbc-crusher expects to find in a database, and why the probes cannot
just create it themselves.**

Related: [IMPROVEMENT_PLAN_V2.md](./IMPROVEMENT_PLAN_V2.md) **R1**, **R2**.

---

## The rule this exists to keep

**No engine names in `src/`.** A probe never writes `CREATE PROCEDURE`, never
branches on which driver it is talking to, and never assumes a parameter shape.
It asks the driver — `SQLTables`, `SQLProcedures`, `SQLProcedureColumns` — and
binds whatever comes back.

That leaves one thing a probe genuinely cannot do for itself: **create a stored
procedure**. There is no dialect-free spelling of it, and unlike a table there
is no fallback list that covers the field. So the objects below are created by
each driver's CI job in that engine's own SQL, and the probes discover them.

This is the same split the repository already has: `.github/` knows that
PostgreSQL needs `ALTER USER` and that MySQL needs a `crusher` account, while
`src/` knows none of it.

## What a probe may assume

Only this:

- the objects below exist, **by name**;
- the *behaviour* stated below holds.

Not their parameter shape, not their parameter directions, not their types.
Those are read at run time. Firebird makes the point: ask its driver about
`CRUSHER_PROC` and `SQLProcedureColumns` answers `(IN, IN, OUT, OUT)` — inputs
first, then outputs, and `SQL_PARAM_INPUT_OUTPUT` never appears, because
Firebird's procedure model has no INOUT. The mock driver answers
`(IN, OUT, INOUT)`. Both are correct; the probes adapt, and report
`SKIP_UNSUPPORTED` for a direction the engine does not declare rather than
failing a driver for not having it.

---

## The objects

### Tables — `CRUSHER_FIXTURE_PARENT` and `CRUSHER_FIXTURE`

| | |
|---|---|
| Why | Without a permanent table, `SQLTables` returns nothing and the catalog probes have nothing to inspect. Every probe that creates a table drops it again in the same probe, so the database is empty by the time the catalog categories run. Four probes reported `SKIP_INCONCLUSIVE` as their *result*. |
| Shape | `CRUSHER_FIXTURE_PARENT(ID PK, LABEL)`; `CRUSHER_FIXTURE(ID PK, PARENT_ID → parent, NAME)`, plus a **secondary index on `NAME`**. |
| Why that shape | The primary key and the secondary index give `SQLStatistics` more than a trivial index list; the foreign key gives it a second kind of entry; two tables give the `{oj …}` outer-join escape something to join; rows in both keep `COUNT(*)` non-zero. |
| Rows | At least two in the parent, three in the child, one of them with a NULL `PARENT_ID`. |

Probes served: `test_count_star_result_metadata`, `test_statistics_result`,
`test_privileges_result`, `test_outer_join_escape`.

### Procedure — `CRUSHER_PROC`

| | |
|---|---|
| Why | The only `SQL_PARAM_OUTPUT` / `SQL_PARAM_INPUT_OUTPUT` coverage in the suite. |
| Requires | At least one integer input and at least one integer output. Where the engine has character parameters, one of each of those too. Where the engine has a true INOUT, use it for the character parameter — then the INOUT probe becomes conclusive instead of skipping. |
| Behaviour | The first integer output is **twice** the first integer input. The first character output is the first character input with `-out` appended. |
| Why that behaviour | So a probe can assert an answer rather than "something was written". `42 → 84` fails loudly if a driver matches arguments to slots wrongly, where a copied-through value would not. |

Probes served: `test_call_escape_in_parameter`,
`test_call_escape_out_parameter`, `test_call_escape_inout_parameter`.

### Callable returning a value — `CRUSHER_FUNC`

| | |
|---|---|
| Why | `{?=CALL fn(?, ?)}` numbers the return value as parameter **1**, so the first argument is parameter 2. A driver that binds the first argument as parameter 1 computes a wrong answer with no diagnostic at all. |
| Requires | Two integer arguments. Where the engine's ODBC driver describes a `SQL_RETURN_VALUE` parameter, use a construct that produces one. |
| Behaviour | Returns `a * 10 + b`. |
| Why that behaviour | With `a=4, b=7` the answer is `47`, and the off-by-one reading — the return-value slot taken as `a` — gives `0*10 + 4 = 4`. Both are nameable, so the probe reports which mistake was made rather than "wrong". |

Probe served: `test_function_call_escape_return_value`.

---

## Worked example — Firebird

From [`.github/actions/firebird-windows/action.yml`](../.github/actions/firebird-windows/action.yml)
and the linux `firebird` job, which must stay in step with each other.

```sql
CREATE TABLE CRUSHER_FIXTURE_PARENT (
    ID    INTEGER NOT NULL PRIMARY KEY,
    LABEL VARCHAR(50));
CREATE TABLE CRUSHER_FIXTURE (
    ID        INTEGER NOT NULL PRIMARY KEY,
    PARENT_ID INTEGER,
    NAME      VARCHAR(50),
    CONSTRAINT FK_CRUSHER_FIXTURE_PARENT
        FOREIGN KEY (PARENT_ID) REFERENCES CRUSHER_FIXTURE_PARENT (ID));
CREATE INDEX IDX_CRUSHER_FIXTURE_NAME ON CRUSHER_FIXTURE (NAME);

SET TERM ^ ;
CREATE PROCEDURE CRUSHER_PROC (IN_N INTEGER, IN_S VARCHAR(64))
RETURNS (OUT_N INTEGER, OUT_S VARCHAR(64)) AS
BEGIN OUT_N = IN_N * 2; OUT_S = IN_S || '-out'; END^

CREATE PROCEDURE CRUSHER_FUNC (A INTEGER, B INTEGER)
RETURNS (RESULT INTEGER) AS
BEGIN RESULT = A * 10 + B; END^
SET TERM ; ^
```

Two Firebird-specific notes, both established by testing rather than assumed:

- **No `SUSPEND`.** With it the procedure becomes *selectable* and returns a
  result set; without it, it is executable and its outputs arrive as output
  parameters, which is what the ODBC direction probes are about.
- **`CRUSHER_FUNC` is a procedure, not a `CREATE FUNCTION`.** Firebird 5 has
  real scalar functions, but its ODBC driver does not list them through
  `SQLProcedures` at all — a probe cannot discover what the catalog will not
  admit exists. As a procedure it is discoverable, and the driver describes it
  as `(IN A, IN B, OUT RESULT)` with no `SQL_RETURN_VALUE`, so the return-value
  probe reports `SKIP_UNSUPPORTED` and names that shape. That is the honest
  outcome for this driver, and it is more informative than the "not visible via
  SQLProcedures" it used to print.

## Adding a driver to the fleet

Create the objects above in your engine's SQL, in the job's database-setup
step, and check the seed took — a fixture that silently failed to load turns
into probes that skip for a reason nobody reads, which is exactly the state
**R1** was opened to fix. Nothing in `src/` needs to change.

If your engine cannot express one of these, leave it out: the probes that need
it will report `SKIP_UNSUPPORTED` and say what the driver declared instead.
That is a real answer about the engine, and it is the reason the probes ask
rather than assume.
