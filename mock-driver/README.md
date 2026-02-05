# Mock ODBC Driver

A configurable Mock ODBC Driver for testing ODBC applications without requiring a real database.

## Features

- **Full ODBC 3.x Implementation**: All core ODBC functions implemented
- **Configurable Behavior**: Control driver behavior via connection string parameters
- **Mock Data**: Built-in mock catalog with realistic data
- **Error Injection**: Simulate failures at specific functions
- **Zero Dependencies**: No database server required
- **Cross-Platform**: Windows, Linux, macOS

## Quick Start

### Connection String

```
Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;
```

### Configuration Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| `Mode` | Success, Failure, Random | Overall behavior mode |
| `Catalog` | Default, Empty, Large | Mock schema preset |
| `ResultSetSize` | Number | Rows to return |
| `FailOn` | Function names | Inject failures |
| `ErrorCode` | SQLSTATE | Error code to return |
| `Latency` | e.g., 10ms | Simulated delay |

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Installation

### Windows
Register the driver in ODBC Administrator or use the registry template.

### Linux/macOS
Add to `/etc/odbcinst.ini`:
```ini
[Mock ODBC Driver]
Description = Mock ODBC Driver for Testing
Driver = /usr/local/lib/libmockodbc.so
```

## Mock Catalog

Default tables:
- `USERS` - User accounts
- `ORDERS` - Order records
- `PRODUCTS` - Product catalog
- `ORDER_ITEMS` - Order line items

## License

MIT License - See LICENSE file
