"""Command-line interface for ODBC Crusher."""

import sys
from typing import Optional

import click
from rich.console import Console
from rich.panel import Panel
from rich.table import Table

from .connection import check_connection
from .test_runner import TestRunner
from .reporter import Reporter

console = Console()


@click.command()
@click.argument("connection_string", type=str)
@click.option(
    "--output",
    "-o",
    type=click.Choice(["text", "json"], case_sensitive=False),
    default="text",
    help="Output format for the test report",
)
@click.option(
    "--verbose",
    "-v",
    is_flag=True,
    help="Enable verbose output",
)
@click.version_option(version="0.3.0", prog_name="odbc-crusher")
def main(connection_string: str, output: str, verbose: bool) -> None:
    """
    ODBC Crusher - Test and debug ODBC drivers.
    
    CONNECTION_STRING: ODBC connection string (e.g., "DSN=mydb;UID=user;PWD=pass")
    
    Examples:
    
        odbc-crusher "DSN=PostgreSQL"
        
        odbc-crusher "DRIVER={SQL Server};SERVER=localhost;DATABASE=test;UID=sa;PWD=pass"
        
        odbc-crusher "DSN=mydb" --output json
    """
    console.print(
        Panel.fit(
            "[bold cyan]ODBC Crusher[/bold cyan] - ODBC Driver Testing Tool",
            subtitle=f"v0.3.0",
        )
    )
    
    if verbose:
        console.print(f"[dim]Connection string: {connection_string}[/dim]\n")
    
    # Phase 1: Test connection
    console.print("[bold]Phase 1:[/bold] Testing connection...\n")
    
    conn_result = check_connection(connection_string, verbose=verbose)
    
    if not conn_result["success"]:
        console.print(f"[bold red]✗ Connection failed:[/bold red] {conn_result['error']}")
        console.print(f"\n[yellow]Diagnostic:[/yellow] {conn_result['diagnostic']}")
        sys.exit(1)
    
    console.print(f"[bold green]✓ Connection successful[/bold green]")
    if verbose and conn_result.get("info"):
        for key, value in conn_result["info"].items():
            console.print(f"  [dim]{key}:[/dim] {value}")
    
    console.print()
    
    # Phase 2: Run tests
    console.print("[bold]Phase 2:[/bold] Running ODBC tests...\n")
    
    runner = TestRunner(connection_string)
    results = runner.run_all_tests()
    
    # Generate report
    reporter = Reporter(results, output_format=output)
    
    if output == "json":
        console.print(reporter.generate_json())
    else:
        reporter.display_text_report()
    
    # Exit with appropriate code
    if reporter.has_failures():
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
