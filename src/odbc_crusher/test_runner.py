"""Test runner to execute all ODBC tests."""

from typing import List, Dict, Any

from .tests import ConnectionTests, TestResult


class TestRunner:
    """Runs all registered ODBC tests."""
    
    def __init__(self, connection_string: str):
        self.connection_string = connection_string
        self.test_classes = [
            ConnectionTests,
            # Future test classes will be added here
        ]
    
    def run_all_tests(self) -> List[TestResult]:
        """
        Execute all registered test classes.
        
        Returns:
            List of all test results
        """
        all_results = []
        
        for test_class in self.test_classes:
            test_instance = test_class(self.connection_string)
            results = test_instance.run()
            all_results.extend(results)
        
        return all_results
    
    def run_specific_tests(self, test_names: List[str]) -> List[TestResult]:
        """
        Run specific test classes by name.
        
        Args:
            test_names: List of test class names to run
            
        Returns:
            List of test results from specified tests
        """
        all_results = []
        
        for test_class in self.test_classes:
            if test_class.__name__ in test_names:
                test_instance = test_class(self.connection_string)
                results = test_instance.run()
                all_results.extend(results)
        
        return all_results
