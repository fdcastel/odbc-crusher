GOAL: Write a python CLI tool to test odbc drivers.

It must receive only one input: the ODBC connection string for a database 

Using this connection string, it will run a series of tests. Each test will call odbc driver functions to check its output against the expected result.

It should identify what functions are ok/wrong/missing/not implemented.

It should output a comprehensive report indicating what was wrong what was expected and (ideally) how to fix 

The project will grow with the time. The objective is to be a debug tool for ODBC drivers developers. LEts develop increntally. Easy initial steps. The grow.

Write a initial plan in this directory. keep the plan always updated. Write instructions for future agents working on this project to always keep the plan updated.

References:
- A comprehensive documentation about ODBC: https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference
- A list of all ODBC functions: https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference

Initial python guidance: 
use uv for package management, build system and tests. Do not use PIP nor `uv PIP` for anything.
