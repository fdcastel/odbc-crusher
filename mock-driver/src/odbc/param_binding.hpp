#pragma once

// Parameter binding helpers shared by the statement entry points — D18.
//
// Split out of statement_api.cpp, which held three unrelated jobs in one
// 2,500-line file. These are internal to the driver; statement_api.cpp is the
// only caller.

#include "driver/common.hpp"
#include "driver/handles.hpp"
#include "mock/mock_data.hpp"

namespace mock_odbc {

// Read parameter set `row` of a bound parameter into a CellValue, honouring
// the binding's C type, indicator and bind offset.
CellValue read_param_value(const StatementHandle::ParameterBinding& pb,
                           SQLULEN row,
                           SQLULEN param_bind_type);

// After a CALL, copy the procedure's output values back into the bound
// OUT/INOUT/RETURN parameter buffers.
void apply_proc_output_writeback(StatementHandle* stmt,
                                 const std::string& proc_name,
                                 const std::vector<CellValue>& output_values);

// Substitute bound parameter values into a ParsedQuery for parameter set
// `row` - INSERT values, literal-SELECT expressions, CALL arguments and, since
// D10, the `?` markers in a WHERE clause.
void substitute_params(
    ParsedQuery& parsed,
    const std::unordered_map<SQLUSMALLINT, StatementHandle::ParameterBinding>& bindings,
    SQLULEN row,
    SQLULEN param_bind_type);

}  // namespace mock_odbc
