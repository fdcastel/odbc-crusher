# Instructions for AI Agents Working on ODBC Crusher

## 🤖 Welcome, Agent!

You are working on **ODBC Crusher**, a CLI tool for testing and debugging ODBC drivers. This document contains critical instructions for maintaining consistency and quality.

## 📋 MANDATORY: Update PROJECT_PLAN.md

**RULE #1**: Every time you make significant changes to this project, you MUST update [PROJECT_PLAN.md](PROJECT_PLAN.md).

### What Requires a Plan Update?

- ✅ Adding new test modules or test categories
- ✅ Completing a phase or milestone
- ✅ Adding new dependencies
- ✅ Changing project structure
- ✅ Implementing new features
- ✅ Updating architecture decisions
- ✅ Identifying new limitations or future ideas

### What Doesn't Require a Plan Update?

- ❌ Fixing typos or minor bugs
- ❌ Refactoring without functional changes
- ❌ Adding comments or documentation
- ❌ Updating dependencies to patch versions

### How to Update the Plan

1. **Read the current plan first**: Always read [PROJECT_PLAN.md](PROJECT_PLAN.md) before making changes
2. **Update relevant sections**:
   - Mark completed items in the current phase
   - Update "Current Status" section
   - Add new phases or modify existing ones if needed
   - Update "Last Updated" date at the top
3. **Be specific**: Include what was added, changed, or removed
4. **Keep it current**: The plan should always reflect the actual state of the project

### Example Plan Update

```markdown
### Phase 1: Foundation ✅ (Completed on Feb 3, 2026)
**Goal**: Basic project structure and connection testing

- [x] Initialize uv project structure
- [x] Create CLI entry point with argument parsing
- [x] Implement basic connection test
...

## Current Status

**Phase**: Phase 2 - Core ODBC Functions  
**Version**: 0.2.0-dev  
**Last Milestone**: Basic connection testing working  
**Next Milestone**: Handle management tests
```

## 🛠️ Project Standards

### Python Package Management

**CRITICAL**: This project uses `uv` for everything. Do NOT use `pip` or `uv pip`.

**Correct**:
```bash
uv add pyodbc
uv add --dev pytest
uv run python -m odbc_crusher.cli "DSN=test"
uv run pytest
```

**WRONG** ❌:
```bash
pip install pyodbc
uv pip install pyodbc
python -m pip install pyodbc
```

### Code Style

- **Formatter**: Use `black` (will be added as dev dependency)
- **Linter**: Use `ruff` (will be added as dev dependency)
- **Type Hints**: Use type hints for all functions
- **Docstrings**: All public functions must have docstrings

### Testing

- **Framework**: pytest
- **Location**: Tests for the tool itself go in `tests/` directory
- **Coverage**: Aim for >80% coverage
- **Naming**: Test files start with `test_`, test functions start with `test_`

### Git Commit Messages

Use conventional commits format:
```
feat: add SQLTables metadata test
fix: handle connection timeout correctly
docs: update installation instructions
test: add unit tests for reporter module
```

## 🏗️ Architecture Guidelines

### Adding New ODBC Tests

1. **Location**: Create test modules in `src/odbc_crusher/tests/`
2. **Inherit from base**: All test classes should inherit from `ODBCTest` base class
3. **Naming**: Test classes should be descriptive (e.g., `HandleManagementTests`, `MetadataTests`)
4. **Structure**: Each test should:
   - Have a clear name describing what it tests
   - Return a standardized result dictionary
   - Include expected vs actual values
   - Provide diagnostic information
   - Suggest fixes when possible

### Test Result Format

Every test must return results in this format:

```python
{
    "test_name": "test_sql_connect",
    "function": "SQLConnect",
    "status": "PASS",  # PASS, FAIL, SKIP, ERROR
    "expected": "Connection established successfully",
    "actual": "Connection established successfully",
    "diagnostic": None,  # or string with diagnostic info
    "severity": "INFO",  # CRITICAL, ERROR, WARNING, INFO
    "duration_ms": 45.2
}
```

### Adding New Dependencies

1. **Add with uv**: `uv add package-name` or `uv add --dev package-name`
2. **Update PROJECT_PLAN.md**: Add to "Python Dependencies" section
3. **Document why**: Add a comment explaining why the dependency is needed

## 📚 ODBC Knowledge Resources

When implementing ODBC tests, refer to:

1. **Primary Reference**: [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
2. **Detailed Guide**: [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
3. **pyodbc Docs**: [pyodbc Wiki](https://github.com/mkleehammer/pyodbc/wiki)

### Key ODBC Concepts

- **Handles**: Environment, Connection, Statement, Descriptor
- **Handle Hierarchy**: Environment → Connection → Statement
- **Return Codes**: SQL_SUCCESS, SQL_SUCCESS_WITH_INFO, SQL_ERROR, SQL_INVALID_HANDLE, SQL_NO_DATA
- **Diagnostic Records**: Use `SQLGetDiagRec` to get error details

## 🚀 Development Workflow

### Starting Work

1. Read [PROJECT_PLAN.md](PROJECT_PLAN.md) to understand current phase
2. Check "Next Milestone" to see what's prioritized
3. Review existing code structure before adding new files

### During Development

1. Make incremental changes
2. Test as you go (`uv run pytest`)
3. Update docstrings and comments
4. Follow the established patterns in existing code

### Completing Work

1. Run full test suite: `uv run pytest`
2. Update [PROJECT_PLAN.md](PROJECT_PLAN.md) (see above)
3. Update [README.md](README.md) if user-facing changes
4. Check that all new files are in the correct locations

## 🐛 Debugging Tips

### Testing ODBC Connections

- **Test DSN**: You can create a test DSN using Windows ODBC Data Source Administrator
- **Connection String Format**: `"DSN=name;UID=user;PWD=pass"` or `"DRIVER={SQL Server};SERVER=localhost;..."`
- **Common Errors**:
  - `[IM002] [Microsoft][ODBC Driver Manager] Data source name not found` - DSN doesn't exist
  - `[28000] Login failed` - Authentication issue
  - `[08001] Unable to connect` - Network or server issue

### pyodbc Quirks

- **Return values**: Most functions return None or raise exceptions
- **Cursor objects**: Statements are represented as cursor objects
- **Autocommit**: Default is autocommit=False, need to commit transactions

## 📝 Documentation

### What to Document

- **README.md**: Installation, quick start, basic usage
- **GETTING_STARTED.md**: Detailed setup guide for new users
- **ADDING_TESTS.md**: Guide for contributors adding new tests
- **Docstrings**: All public APIs

### Documentation Style

- Use clear, concise language
- Provide code examples
- Include expected output
- Explain error messages and how to fix them

## ✅ Quality Checklist

Before considering work complete:

- [ ] Code follows project structure
- [ ] All functions have type hints
- [ ] All public functions have docstrings
- [ ] Tests pass (`uv run pytest`)
- [ ] PROJECT_PLAN.md is updated
- [ ] README.md is updated (if needed)
- [ ] No hardcoded values (use configuration)
- [ ] Error handling is comprehensive
- [ ] User-facing messages are clear and helpful

## 🎯 Project Philosophy

**Remember**: This tool is for ODBC driver **developers**, not end users. It should:

1. **Be thorough**: Test edge cases and error conditions
2. **Be clear**: Reports should explain exactly what went wrong
3. **Be helpful**: Suggest fixes and provide context
4. **Be reliable**: The tool itself should never crash
5. **Be incremental**: Start simple, add complexity over time

## 🔄 Continuous Improvement

This project grows over time. When you notice:

- Repetitive code → Create a helper function
- Complex logic → Add comments and break into smaller functions
- Missing tests → Add them to the next phase in PROJECT_PLAN.md
- Unclear documentation → Improve it immediately

---

**Thank you for maintaining ODBC Crusher!** Your adherence to these guidelines ensures the project remains organized, extensible, and valuable for ODBC driver developers.

---

*Last Updated*: February 3, 2026
