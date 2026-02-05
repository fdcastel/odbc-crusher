#include "function_info.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <iomanip>

namespace odbc_crusher::discovery {

FunctionInfo::FunctionInfo(core::OdbcConnection& conn)
    : conn_(conn) {
    function_bitmap_.fill(0);
}

void FunctionInfo::collect() {
    functions_.clear();
    
    // Get all ODBC 3.x functions via bitmap
    SQLRETURN ret = SQLGetFunctions(conn_.get_handle(), SQL_API_ODBC3_ALL_FUNCTIONS, function_bitmap_.data());
    core::check_odbc_result(ret, SQL_HANDLE_DBC, conn_.get_handle(), "SQLGetFunctions");
    
    // Important ODBC functions to check
    std::vector<SQLUSMALLINT> important_functions = {
        // Connection functions
        SQL_API_SQLCONNECT,
        SQL_API_SQLDRIVERCONNECT,
        SQL_API_SQLDISCONNECT,
        SQL_API_SQLBROWSECONNECT,
        
        // Statement functions
        SQL_API_SQLEXECDIRECT,
        SQL_API_SQLPREPARE,
        SQL_API_SQLEXECUTE,
        SQL_API_SQLFETCH,
        SQL_API_SQLFETCHSCROLL,
        SQL_API_SQLMORERESULTS,
        SQL_API_SQLCLOSECURSOR,
        SQL_API_SQLCANCEL,
        
        // Catalog functions
        SQL_API_SQLTABLES,
        SQL_API_SQLCOLUMNS,
        SQL_API_SQLPRIMARYKEYS,
        SQL_API_SQLFOREIGNKEYS,
        SQL_API_SQLSTATISTICS,
        SQL_API_SQLSPECIALCOLUMNS,
        SQL_API_SQLPROCEDURES,
        SQL_API_SQLPROCEDURECOLUMNS,
        SQL_API_SQLTABLEPRIVILEGES,
        SQL_API_SQLCOLUMNPRIVILEGES,
        
        // Data retrieval functions
        SQL_API_SQLGETDATA,
        SQL_API_SQLBINDCOL,
        SQL_API_SQLBINDPARAMETER,
        SQL_API_SQLDESCRIBECOL,
        SQL_API_SQLCOLATTRIBUTE,
        SQL_API_SQLNUMRESULTCOLS,
        SQL_API_SQLROWCOUNT,
        SQL_API_SQLDESCRIBEPARAM,
        SQL_API_SQLNUMPARAMS,
        
        // Transaction functions
        SQL_API_SQLENDTRAN,
        
        // Diagnostic functions
        SQL_API_SQLGETDIAGFIELD,
        SQL_API_SQLGETDIAGREC,
        
        // Attribute functions
        SQL_API_SQLGETCONNECTATTR,
        SQL_API_SQLSETCONNECTATTR,
        SQL_API_SQLGETSTMTATTR,
        SQL_API_SQLSETSTMTATTR,
        SQL_API_SQLGETENVATTR,
        SQL_API_SQLSETENVATTR,
        
        // Handle functions
        SQL_API_SQLALLOCHANDLE,
        SQL_API_SQLFREEHANDLE,
        
        // Info functions
        SQL_API_SQLGETINFO,
        SQL_API_SQLGETFUNCTIONS,
        SQL_API_SQLGETTYPEINFO,
        
        // Cursor functions
        SQL_API_SQLSETPOS,
        SQL_API_SQLBULKOPERATIONS,
        
        // Descriptor functions
        SQL_API_SQLCOPYDESC,
        SQL_API_SQLGETDESCFIELD,
        SQL_API_SQLSETDESCFIELD,
        SQL_API_SQLGETDESCREC,
        SQL_API_SQLSETDESCREC
    };
    
    for (auto func_id : important_functions) {
        FunctionAvailability func;
        func.function_id = func_id;
        func.function_name = get_function_name(func_id);
        func.supported = is_supported(func_id);
        functions_.push_back(func);
    }
}

bool FunctionInfo::is_supported(SQLUSMALLINT function_id) const {
    if (function_id >= SQL_API_ODBC3_ALL_FUNCTIONS_SIZE * 16) {
        return false;
    }
    
    SQLUSMALLINT index = function_id / 16;
    SQLUSMALLINT bit = function_id % 16;
    
    return (function_bitmap_[index] & (1 << bit)) != 0;
}

size_t FunctionInfo::supported_count() const {
    size_t count = 0;
    for (const auto& func : functions_) {
        if (func.supported) ++count;
    }
    return count;
}

size_t FunctionInfo::unsupported_count() const {
    return functions_.size() - supported_count();
}

std::string FunctionInfo::format_summary() const {
    std::ostringstream oss;
    
    oss << "\nODBC Functions Supported: " << supported_count() << "/" << functions_.size() << "\n";
    oss << "===============================================\n\n";
    
    oss << "Supported Functions:\n";
    for (const auto& func : functions_) {
        if (func.supported) {
            oss << "  ✓ " << func.function_name << "\n";
        }
    }
    
    if (unsupported_count() > 0) {
        oss << "\nUnsupported Functions:\n";
        for (const auto& func : functions_) {
            if (!func.supported) {
                oss << "  ✗ " << func.function_name << "\n";
            }
        }
    }
    
    return oss.str();
}

std::string FunctionInfo::get_function_name(SQLUSMALLINT function_id) {
    switch (function_id) {
        case SQL_API_SQLCONNECT: return "SQLConnect";
        case SQL_API_SQLDRIVERCONNECT: return "SQLDriverConnect";
        case SQL_API_SQLDISCONNECT: return "SQLDisconnect";
        case SQL_API_SQLBROWSECONNECT: return "SQLBrowseConnect";
        case SQL_API_SQLEXECDIRECT: return "SQLExecDirect";
        case SQL_API_SQLPREPARE: return "SQLPrepare";
        case SQL_API_SQLEXECUTE: return "SQLExecute";
        case SQL_API_SQLFETCH: return "SQLFetch";
        case SQL_API_SQLFETCHSCROLL: return "SQLFetchScroll";
        case SQL_API_SQLMORERESULTS: return "SQLMoreResults";
        case SQL_API_SQLCLOSECURSOR: return "SQLCloseCursor";
        case SQL_API_SQLCANCEL: return "SQLCancel";
        case SQL_API_SQLTABLES: return "SQLTables";
        case SQL_API_SQLCOLUMNS: return "SQLColumns";
        case SQL_API_SQLPRIMARYKEYS: return "SQLPrimaryKeys";
        case SQL_API_SQLFOREIGNKEYS: return "SQLForeignKeys";
        case SQL_API_SQLSTATISTICS: return "SQLStatistics";
        case SQL_API_SQLSPECIALCOLUMNS: return "SQLSpecialColumns";
        case SQL_API_SQLPROCEDURES: return "SQLProcedures";
        case SQL_API_SQLPROCEDURECOLUMNS: return "SQLProcedureColumns";
        case SQL_API_SQLTABLEPRIVILEGES: return "SQLTablePrivileges";
        case SQL_API_SQLCOLUMNPRIVILEGES: return "SQLColumnPrivileges";
        case SQL_API_SQLGETDATA: return "SQLGetData";
        case SQL_API_SQLBINDCOL: return "SQLBindCol";
        case SQL_API_SQLBINDPARAMETER: return "SQLBindParameter";
        case SQL_API_SQLDESCRIBECOL: return "SQLDescribeCol";
        case SQL_API_SQLCOLATTRIBUTE: return "SQLColAttribute";
        case SQL_API_SQLNUMRESULTCOLS: return "SQLNumResultCols";
        case SQL_API_SQLROWCOUNT: return "SQLRowCount";
        case SQL_API_SQLDESCRIBEPARAM: return "SQLDescribeParam";
        case SQL_API_SQLNUMPARAMS: return "SQLNumParams";
        case SQL_API_SQLENDTRAN: return "SQLEndTran";
        case SQL_API_SQLGETDIAGFIELD: return "SQLGetDiagField";
        case SQL_API_SQLGETDIAGREC: return "SQLGetDiagRec";
        case SQL_API_SQLGETCONNECTATTR: return "SQLGetConnectAttr";
        case SQL_API_SQLSETCONNECTATTR: return "SQLSetConnectAttr";
        case SQL_API_SQLGETSTMTATTR: return "SQLGetStmtAttr";
        case SQL_API_SQLSETSTMTATTR: return "SQLSetStmtAttr";
        case SQL_API_SQLGETENVATTR: return "SQLGetEnvAttr";
        case SQL_API_SQLSETENVATTR: return "SQLSetEnvAttr";
        case SQL_API_SQLALLOCHANDLE: return "SQLAllocHandle";
        case SQL_API_SQLFREEHANDLE: return "SQLFreeHandle";
        case SQL_API_SQLGETINFO: return "SQLGetInfo";
        case SQL_API_SQLGETFUNCTIONS: return "SQLGetFunctions";
        case SQL_API_SQLGETTYPEINFO: return "SQLGetTypeInfo";
        case SQL_API_SQLSETPOS: return "SQLSetPos";
        case SQL_API_SQLBULKOPERATIONS: return "SQLBulkOperations";
        case SQL_API_SQLCOPYDESC: return "SQLCopyDesc";
        case SQL_API_SQLGETDESCFIELD: return "SQLGetDescField";
        case SQL_API_SQLSETDESCFIELD: return "SQLSetDescField";
        case SQL_API_SQLGETDESCREC: return "SQLGetDescRec";
        case SQL_API_SQLSETDESCREC: return "SQLSetDescRec";
        default: return "Unknown (" + std::to_string(function_id) + ")";
    }
}

FunctionInfo::FunctionSupport FunctionInfo::get_support() const {
    FunctionSupport support;
    support.supported_count = supported_count();
    support.total_checked = functions_.size();
    
    for (const auto& func : functions_) {
        if (func.supported) {
            support.supported.push_back(func.function_name);
        } else {
            support.unsupported.push_back(func.function_name);
        }
    }
    
    return support;
}

} // namespace odbc_crusher::discovery
