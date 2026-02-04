"""Test runner to execute all ODBC tests."""

from typing import List, Dict, Any, Optional

from .tests import (
    DriverCapabilityTests,
    ConnectionTests,
    HandleTests,
    StatementTests,
    MetadataTests,
    AdvancedTests,
    DataTypeTests,
    TestResult,
)
from .driver_info import DriverInfo


class TestRunner:
    """Runs all registered ODBC tests."""
    
    def __init__(self, connection_string: str):
        self.connection_string = connection_string
        self.driver_info: Optional[Dict[str, Any]] = None
        self.test_classes = [
            DriverCapabilityTests,  # Run FIRST - detects driver capabilities
            ConnectionTests,
            HandleTests,
            StatementTests,
            MetadataTests,
            AdvancedTests,
            DataTypeTests,
        ]
    
    def collect_driver_info(self) -> Dict[str, Any]:
        """Collect driver information before running tests."""
        if not self.driver_info:
            collector = DriverInfo(self.connection_string)
            self.driver_info = collector.collect_all()
        return self.driver_info
    
    def run_all_tests(self) -> List[TestResult]:
        """
        Execute all registered test classes.
        
        Returns:
            List of all test results
        """
        # Collect driver info first
        driver_info = self.collect_driver_info()
        
        all_results = []
        
        for test_class in self.test_classes:
            test_instance = test_class(self.connection_string)
            # Pass driver info to test instance
            if hasattr(test_instance, 'set_driver_info'):
                test_instance.set_driver_info(driver_info)
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
