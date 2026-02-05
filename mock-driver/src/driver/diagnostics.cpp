#include "diagnostics.hpp"

namespace mock_odbc {

DiagnosticRecord make_diagnostic(const std::string& sqlstate,
                                  SQLINTEGER native_error,
                                  const std::string& message) {
    DiagnosticRecord rec;
    rec.sqlstate = sqlstate;
    rec.native_error = native_error;
    rec.message = message;
    rec.class_origin = "ISO 9075";
    rec.subclass_origin = "ODBC 3.0";
    rec.connection_name = "";
    rec.server_name = "MockDB";
    return rec;
}

} // namespace mock_odbc
