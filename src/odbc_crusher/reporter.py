"""Report generation for ODBC test results."""

import json
from typing import List, Dict, Any

from rich.console import Console
from rich.table import Table
from rich.panel import Panel

from .tests import TestResult, TestStatus, Severity


class Reporter:
    """Generates reports from ODBC test results."""
    
    def __init__(self, results: List[TestResult], output_format: str = "text"):
        self.results = results
        self.output_format = output_format
        self.console = Console()
    
    def has_failures(self) -> bool:
        """Check if any tests failed or had errors."""
        return any(
            r.status in [TestStatus.FAIL, TestStatus.ERROR]
            for r in self.results
        )
    
    def get_summary(self) -> Dict[str, int]:
        """Get summary counts of test results."""
        summary = {
            "total": len(self.results),
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "errors": 0,
        }
        
        for result in self.results:
            if result.status == TestStatus.PASS:
                summary["passed"] += 1
            elif result.status == TestStatus.FAIL:
                summary["failed"] += 1
            elif result.status == TestStatus.SKIP:
                summary["skipped"] += 1
            elif result.status == TestStatus.ERROR:
                summary["errors"] += 1
        
        return summary
    
    def display_text_report(self):
        """Display a formatted text report using Rich."""
        # Summary
        summary = self.get_summary()
        
        summary_text = (
            f"[bold]Total:[/bold] {summary['total']} | "
            f"[bold green]Passed:[/bold green] {summary['passed']} | "
            f"[bold red]Failed:[/bold red] {summary['failed']} | "
            f"[bold yellow]Errors:[/bold yellow] {summary['errors']} | "
            f"[bold dim]Skipped:[/bold dim] {summary['skipped']}"
        )
        
        self.console.print(Panel(summary_text, title="Test Summary", border_style="blue"))
        self.console.print()
        
        # Detailed results
        if self.results:
            self._display_detailed_results()
    
    def _display_detailed_results(self):
        """Display detailed test results in a table."""
        table = Table(title="Test Results", show_header=True, header_style="bold cyan")
        
        table.add_column("Status", style="bold", width=8)
        table.add_column("Test Name", style="cyan", width=30)
        table.add_column("Function", style="magenta", width=20)
        table.add_column("Severity", width=10)
        table.add_column("Duration (ms)", justify="right", width=12)
        
        for result in self.results:
            status_icon = self._get_status_icon(result.status)
            severity_color = self._get_severity_color(result.severity)
            
            table.add_row(
                status_icon,
                result.test_name,
                result.function,
                f"[{severity_color}]{result.severity.value}[/{severity_color}]",
                f"{result.duration_ms:.2f}",
            )
        
        self.console.print(table)
        self.console.print()
        
        # Show failures and errors in detail
        failures = [r for r in self.results if r.status in [TestStatus.FAIL, TestStatus.ERROR]]
        
        if failures:
            self.console.print("[bold red]Failed Tests Details:[/bold red]\n")
            
            for result in failures:
                self._display_failure_detail(result)
    
    def _display_failure_detail(self, result: TestResult):
        """Display detailed information about a failed test."""
        status_color = "red" if result.status == TestStatus.FAIL else "yellow"
        
        detail = f"[bold {status_color}]{result.status.value}:[/bold {status_color}] {result.test_name}\n"
        detail += f"[dim]Function:[/dim] {result.function}\n"
        detail += f"[dim]Expected:[/dim] {result.expected}\n"
        detail += f"[bold]Actual:[/bold] {result.actual}\n"
        
        if result.diagnostic:
            detail += f"[yellow]💡 Diagnostic:[/yellow] {result.diagnostic}\n"
        
        self.console.print(Panel(detail, border_style=status_color))
        self.console.print()
    
    def _get_status_icon(self, status: TestStatus) -> str:
        """Get colored icon for test status."""
        icons = {
            TestStatus.PASS: "[bold green]✓ PASS[/bold green]",
            TestStatus.FAIL: "[bold red]✗ FAIL[/bold red]",
            TestStatus.SKIP: "[bold dim]○ SKIP[/bold dim]",
            TestStatus.ERROR: "[bold yellow]⚠ ERROR[/bold yellow]",
        }
        return icons.get(status, "?")
    
    def _get_severity_color(self, severity: Severity) -> str:
        """Get color for severity level."""
        colors = {
            Severity.CRITICAL: "red",
            Severity.ERROR: "red",
            Severity.WARNING: "yellow",
            Severity.INFO: "blue",
        }
        return colors.get(severity, "white")
    
    def generate_json(self) -> str:
        """
        Generate JSON report.
        
        Returns:
            JSON string of test results
        """
        summary = self.get_summary()
        
        report = {
            "summary": summary,
            "results": [r.to_dict() for r in self.results],
        }
        
        return json.dumps(report, indent=2)
    
    def save_report(self, filepath: str):
        """
        Save report to file.
        
        Args:
            filepath: Path to save the report
        """
        if self.output_format == "json":
            content = self.generate_json()
        else:
            # For text format, we'd need to capture console output
            # This is a simplified version
            content = f"ODBC Crusher Test Report\n{'='*50}\n\n"
            summary = self.get_summary()
            content += f"Summary: {summary}\n\n"
            
            for result in self.results:
                content += f"{result.status.value}: {result.test_name}\n"
                content += f"  Function: {result.function}\n"
                content += f"  Expected: {result.expected}\n"
                content += f"  Actual: {result.actual}\n"
                if result.diagnostic:
                    content += f"  Diagnostic: {result.diagnostic}\n"
                content += "\n"
        
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
