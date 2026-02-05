#include <iostream>
#include <CLI/CLI.hpp>
#include "odbc_crusher/version.hpp"
#include "cli/cli_parser.hpp"

int main(int argc, char** argv) {
    CLI::App app{"ODBC Crusher - ODBC Driver Testing Tool", "odbc-crusher"};
    
    app.set_version_flag("--version", ODBC_CRUSHER_VERSION);
    
    std::string connection_string;
    app.add_option("connection", connection_string, "ODBC connection string")
        ->required();
    
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Verbose output");
    
    std::string output_format = "console";
    app.add_option("-o,--output", output_format, "Output format (console, json, html)")
        ->check(CLI::IsMember({"console", "json", "html"}));
    
    CLI11_PARSE(app, argc, argv);
    
    std::cout << "ODBC Crusher v" << ODBC_CRUSHER_VERSION << "\n";
    std::cout << "Connection string: " << connection_string << "\n";
    
    if (verbose) {
        std::cout << "Verbose mode enabled\n";
    }
    
    std::cout << "Output format: " << output_format << "\n";
    
    // TODO: Implement actual testing logic
    std::cout << "\nNote: Full implementation coming in Phase 1+\n";
    
    return 0;
}
