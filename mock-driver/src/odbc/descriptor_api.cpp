// Descriptor API - SQLGetDescField, SQLSetDescField, etc.

#include "mock/behaviors.hpp"
#include "driver/handles.hpp"
#include "utils/buffer_copy.hpp"
#include "driver/diagnostics.hpp"

#include <cstring>  // std::memcpy — Linux GCC is stricter than MSVC about transitives
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLGetDescField(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) MOCK_ENTRY_TRY {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    HandleLock lock(desc);
    desc->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLGetDescField")) {
            desc->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLGetDescField failure");
            return SQL_ERROR;
        }
    }

    (void)iRecord;
    (void)cbValueMax;
    
    // D29: read back what SQLSetDescField now stores; a field an application
    // can set and not read is only half implemented.
    const DescriptorHandle::DescriptorRecord* rec =
        (iRecord >= 1 && static_cast<size_t>(iRecord) <= desc->records_.size())
            ? &desc->records_[static_cast<size_t>(iRecord) - 1]
            : nullptr;
    auto emit_small = [&](SQLSMALLINT v) {
        if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = v;
        if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
    };
    auto emit_len = [&](SQLLEN v) {
        if (rgbValue) *static_cast<SQLLEN*>(rgbValue) = v;
        if (pcbValue) *pcbValue = sizeof(SQLLEN);
    };

    switch (iField) {
        case SQL_DESC_COUNT:
            if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = desc->count_;
            if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
            break;

        case SQL_DESC_ALLOC_TYPE:
            if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = desc->alloc_type_;
            if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
            break;

        case SQL_DESC_TYPE:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->type);
            break;

        case SQL_DESC_CONCISE_TYPE:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->concise_type);
            break;

        case SQL_DESC_PRECISION:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->precision);
            break;

        case SQL_DESC_SCALE:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->scale);
            break;

        case SQL_DESC_LENGTH:
            if (!rec) return SQL_NO_DATA;
            emit_len(rec->length);
            break;

        case SQL_DESC_OCTET_LENGTH:
            if (!rec) return SQL_NO_DATA;
            emit_len(rec->octet_length);
            break;

        // IMPROVEMENT_PLAN_V2 P6/P7: four fields the driver did not answer, so
        // the probes for issue #316's shape could not ask it anything. Each is
        // emitted at its specification width - emit_len is SQLLEN, emit_small
        // is SQLSMALLINT - which is the other half of that issue.
        case SQL_DESC_NAME:
        case SQL_DESC_LABEL: {
            if (!rec) return SQL_NO_DATA;
            const std::string& n = rec->name;
            if (rgbValue && cbValueMax > 0) {
                const size_t room = static_cast<size_t>(cbValueMax) - 1;
                const size_t copy = n.size() < room ? n.size() : room;
                std::memcpy(rgbValue, n.data(), copy);
                static_cast<char*>(rgbValue)[copy] = 0;
            }
            if (pcbValue) *pcbValue = static_cast<SQLINTEGER>(n.size());
            break;
        }

        case SQL_DESC_NULLABLE:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->nullable);
            break;

        case SQL_DESC_DISPLAY_SIZE:
            if (!rec) return SQL_NO_DATA;
            emit_len(rec->display_size);
            break;

        case SQL_DESC_DATETIME_INTERVAL_CODE:
            if (!rec) return SQL_NO_DATA;
            emit_small(rec->datetime_interval_code);
            break;

        case SQL_DESC_DATA_PTR:
            if (!rec) return SQL_NO_DATA;
            if (rgbValue) *static_cast<SQLPOINTER*>(rgbValue) = rec->data_ptr;
            if (pcbValue) *pcbValue = sizeof(SQLPOINTER);
            break;
            
        default:
            // D29: this returned SQL_SUCCESS with a zeroed output for every
            // field it did not implement, so an application could not tell
            // "the answer is 0" from "I do not know that field".
            desc->add_diagnostic(sqlstate::INVALID_DESCRIPTOR_FIELD, 0,
                                 "Invalid descriptor field identifier: "
                                 + std::to_string(iField));
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLSetDescField(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) MOCK_ENTRY_TRY {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    HandleLock lock(desc);
    desc->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLSetDescField")) {
            desc->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLSetDescField failure");
            return SQL_ERROR;
        }
    }

    (void)cbValue;

    // D29: the per-record fields were not implemented at all. They were
    // accepted silently, so an application configuring an ARD - which is the
    // only way to bind SQL_C_NUMERIC with a chosen precision and scale - was
    // told every field took effect while none of them did.
    auto record_for = [&](SQLSMALLINT rec) -> DescriptorHandle::DescriptorRecord* {
        if (rec < 1) return nullptr;
        if (static_cast<size_t>(rec) > desc->records_.size()) {
            desc->records_.resize(static_cast<size_t>(rec));
            if (desc->count_ < rec) desc->count_ = rec;
        }
        return &desc->records_[static_cast<size_t>(rec) - 1];
    };
    const SQLLEN value = reinterpret_cast<SQLLEN>(rgbValue);

    switch (iField) {
        case SQL_DESC_COUNT:
            desc->count_ = static_cast<SQLSMALLINT>(reinterpret_cast<intptr_t>(rgbValue));
            break;

        case SQL_DESC_TYPE:
        case SQL_DESC_CONCISE_TYPE: {
            auto* rec = record_for(iRecord);
            if (!rec) break;
            rec->type = static_cast<SQLSMALLINT>(value);
            rec->concise_type = static_cast<SQLSMALLINT>(value);
            break;
        }

        case SQL_DESC_PRECISION: {
            auto* rec = record_for(iRecord);
            if (rec) rec->precision = static_cast<SQLSMALLINT>(value);
            break;
        }

        case SQL_DESC_SCALE: {
            auto* rec = record_for(iRecord);
            if (rec) rec->scale = static_cast<SQLSMALLINT>(value);
            break;
        }

        case SQL_DESC_LENGTH: {
            auto* rec = record_for(iRecord);
            if (rec) rec->length = value;
            break;
        }

        case SQL_DESC_OCTET_LENGTH: {
            auto* rec = record_for(iRecord);
            if (rec) rec->octet_length = value;
            break;
        }

        case SQL_DESC_DATA_PTR: {
            auto* rec = record_for(iRecord);
            if (rec) rec->data_ptr = rgbValue;
            break;
        }

        case SQL_DESC_INDICATOR_PTR: {
            auto* rec = record_for(iRecord);
            if (rec) rec->indicator_ptr = static_cast<SQLLEN*>(rgbValue);
            break;
        }

        case SQL_DESC_OCTET_LENGTH_PTR: {
            auto* rec = record_for(iRecord);
            if (rec) rec->octet_length_ptr = static_cast<SQLLEN*>(rgbValue);
            break;
        }
            
        default:
            // D29: silently accepting a field the driver does not implement
            // tells the application its setting took effect.
            desc->add_diagnostic(sqlstate::INVALID_DESCRIPTOR_FIELD, 0,
                                 "Invalid descriptor field identifier: "
                                 + std::to_string(iField));
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLGetDescRec(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLCHAR* szName,
    SQLSMALLINT cbNameMax,
    SQLSMALLINT* pcbName,
    SQLSMALLINT* pfType,
    SQLSMALLINT* pfSubType,
    SQLLEN* pLength,
    SQLSMALLINT* pPrecision,
    SQLSMALLINT* pScale,
    SQLSMALLINT* pNullable) MOCK_ENTRY_TRY {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    HandleLock lock(desc);
    desc->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLGetDescRec")) {
            desc->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLGetDescRec failure");
            return SQL_ERROR;
        }
    }

    if (iRecord < 1 || iRecord > static_cast<SQLSMALLINT>(desc->records_.size())) {
        return SQL_NO_DATA;
    }
    
    const auto& rec = desc->records_[iRecord - 1];
    
    // D29: the name output was explicitly discarded, so a caller asking
    // SQLGetDescRec for a column's name got its buffer back untouched.
    if (szName || pcbName) {
        const BufferCopyResult res = copy_chars(
            rec.name, 0, szName, static_cast<SQLLEN>(cbNameMax));
        if (pcbName) *pcbName = static_cast<SQLSMALLINT>(res.remaining);
        if (res.truncated) {
            desc->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                 "String data, right truncated");
        }
    }
    
    if (pfType) *pfType = rec.type;
    if (pfSubType) *pfSubType = rec.datetime_interval_code;
    if (pLength) *pLength = rec.length;
    if (pPrecision) *pPrecision = rec.precision;
    if (pScale) *pScale = rec.scale;
    if (pNullable) *pNullable = rec.nullable;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLSetDescRec(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT fType,
    SQLSMALLINT fSubType,
    SQLLEN cbLength,
    SQLSMALLINT ibPrecision,
    SQLSMALLINT ibScale,
    SQLPOINTER rgbValue,
    SQLLEN* pcbStringLength,
    SQLLEN* pcbIndicator) MOCK_ENTRY_TRY {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    HandleLock lock(desc);
    desc->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLSetDescRec")) {
            desc->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLSetDescRec failure");
            return SQL_ERROR;
        }
    }

    // Expand records if needed
    while (static_cast<SQLSMALLINT>(desc->records_.size()) < iRecord) {
        desc->records_.push_back(DescriptorHandle::DescriptorRecord{});
    }
    
    auto& rec = desc->records_[iRecord - 1];
    rec.type = fType;
    rec.datetime_interval_code = fSubType;
    rec.length = cbLength;
    rec.precision = ibPrecision;
    rec.scale = ibScale;
    rec.data_ptr = rgbValue;
    rec.octet_length_ptr = pcbStringLength;
    rec.indicator_ptr = pcbIndicator;
    
    desc->count_ = static_cast<SQLSMALLINT>(desc->records_.size());
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLCopyDesc(
    SQLHDESC hDescSource,
    SQLHDESC hDescTarget) MOCK_ENTRY_TRY {
    
    auto* src = validate_desc_handle(hDescSource);
    auto* tgt = validate_desc_handle(hDescTarget);

    if (!src || !tgt) return SQL_INVALID_HANDLE;
    HandleLock lock(tgt);
    tgt->clear_diagnostics();

    tgt->count_ = src->count_;
    tgt->records_ = src->records_;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hDescSource)

SQLRETURN SQL_API SQLColAttribute(
    SQLHSTMT hstmt,
    SQLUSMALLINT iCol,
    SQLUSMALLINT iField,
    SQLPOINTER pCharAttr,
    SQLSMALLINT cbCharAttrMax,
    SQLSMALLINT* pcbCharAttr,
    SQLLEN* pNumAttr) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLColAttribute")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLColAttribute failure");
            return SQL_ERROR;
        }
    }

    if (iCol < 1 || iCol > static_cast<SQLUSMALLINT>(stmt->column_names_.size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const std::string& col_name = stmt->column_names_[iCol - 1];
    SQLSMALLINT col_type = stmt->column_types_[iCol - 1];
    const SQLULEN col_size = (iCol <= stmt->column_sizes_.size())
                           ? stmt->column_sizes_[iCol - 1] : 0;

    // D29 helpers, shared by the fields added below.
    auto is_character_type = [](SQLSMALLINT t) {
        return t == SQL_CHAR || t == SQL_VARCHAR || t == SQL_LONGVARCHAR
            || t == SQL_WCHAR || t == SQL_WVARCHAR || t == SQL_WLONGVARCHAR;
    };
    auto type_name_for = [](SQLSMALLINT t) -> std::string {
        switch (t) {
            case SQL_INTEGER:        return "INTEGER";
            case SQL_SMALLINT:       return "SMALLINT";
            case SQL_BIGINT:         return "BIGINT";
            case SQL_VARCHAR:        return "VARCHAR";
            case SQL_CHAR:           return "CHAR";
            case SQL_WVARCHAR:       return "NVARCHAR";
            case SQL_WCHAR:          return "NCHAR";
            case SQL_DECIMAL:        return "DECIMAL";
            case SQL_NUMERIC:        return "NUMERIC";
            case SQL_DOUBLE:         return "DOUBLE";
            case SQL_REAL:           return "REAL";
            case SQL_TYPE_DATE:      return "DATE";
            case SQL_TYPE_TIME:      return "TIME";
            case SQL_TYPE_TIMESTAMP: return "TIMESTAMP";
            case SQL_BINARY:         return "BINARY";
            case SQL_VARBINARY:      return "VARBINARY";
            default:                 return "UNKNOWN";
        }
    };
    auto octet_length_for = [&](SQLSMALLINT t, SQLULEN size) -> SQLLEN {
        switch (t) {
            case SQL_INTEGER:  return 4;
            case SQL_SMALLINT: return 2;
            case SQL_BIGINT:   return 8;
            case SQL_DOUBLE:   return 8;
            case SQL_REAL:     return 4;
            case SQL_WCHAR:
            case SQL_WVARCHAR:
            case SQL_WLONGVARCHAR:
                return static_cast<SQLLEN>(size ? size * 2 : 510);
            default:
                return static_cast<SQLLEN>(size ? size : 255);
        }
    };
    auto emit_string = [&](const std::string& value) {
        const BufferCopyResult res = copy_chars(
            value, 0, pCharAttr, static_cast<SQLLEN>(cbCharAttrMax));
        if (pcbCharAttr) {
            *pcbCharAttr = static_cast<SQLSMALLINT>(res.remaining);
        }
        if (res.truncated) {
            stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                 "String data, right truncated");
        }
    };
    
    switch (iField) {
        case SQL_DESC_NAME:
        case SQL_COLUMN_NAME:
            {
                // D18/D24: was a hand-rolled memcpy with no truncation
                // signal of any kind - not even the return code, which
                // the caller could at least have inspected.
                const BufferCopyResult res = copy_chars(
                    col_name, 0, pCharAttr,
                    static_cast<SQLLEN>(cbCharAttrMax));
                if (pcbCharAttr) {
                    *pcbCharAttr = static_cast<SQLSMALLINT>(res.remaining);
                }
                if (res.truncated) {
                    stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                         "String data, right truncated");
                }
            }
            break;
            
        // SQL_DESC_CONCISE_TYPE and SQL_COLUMN_TYPE are both 2, so the
        // concise type is answered here - D29 listed it as missing, but it was
        // reachable all along under the other name.
        case SQL_COLUMN_TYPE:
            if (pNumAttr) *pNumAttr = col_type;
            break;

        // IMPROVEMENT_PLAN_V2 P7: SQL_DESC_TYPE (1002) is a *different* field
        // from the concise type, and for a datetime column the specification
        // asks for SQL_DATETIME with the specific type carried in
        // SQL_DESC_DATETIME_INTERVAL_CODE. Answering the concise code for both
        // is issue #316 defect 3 in the Firebird driver, and this driver had
        // the same shape: one shared case returning col_type.
        case SQL_DESC_TYPE:
            if (pNumAttr) {
                switch (col_type) {
                    case SQL_TYPE_DATE:
                    case SQL_TYPE_TIME:
                    case SQL_TYPE_TIMESTAMP:
                        *pNumAttr = SQL_DATETIME;
                        break;
                    default:
                        *pNumAttr = col_type;
                        break;
                }
            }
            break;

        case SQL_DESC_DATETIME_INTERVAL_CODE:
            if (pNumAttr) {
                switch (col_type) {
                    case SQL_TYPE_DATE:      *pNumAttr = SQL_CODE_DATE; break;
                    case SQL_TYPE_TIME:      *pNumAttr = SQL_CODE_TIME; break;
                    case SQL_TYPE_TIMESTAMP: *pNumAttr = SQL_CODE_TIMESTAMP; break;
                    default:                 *pNumAttr = 0; break;
                }
            }
            break;
            
        case SQL_DESC_LENGTH:
        case SQL_COLUMN_LENGTH:
            if (pNumAttr) {
                switch (col_type) {
                    case SQL_INTEGER: *pNumAttr = 4; break;
                    case SQL_SMALLINT: *pNumAttr = 2; break;
                    case SQL_BIGINT: *pNumAttr = 8; break;
                    case SQL_VARCHAR:
                    case SQL_WVARCHAR: *pNumAttr = 255; break;
                    case SQL_DECIMAL: *pNumAttr = 18; break;
                    default: *pNumAttr = 255;
                }
            }
            break;
            
        case SQL_DESC_PRECISION:
        case SQL_COLUMN_PRECISION:
            if (pNumAttr) {
                switch (col_type) {
                    case SQL_INTEGER: *pNumAttr = 10; break;
                    case SQL_SMALLINT: *pNumAttr = 5; break;
                    case SQL_BIGINT: *pNumAttr = 19; break;
                    case SQL_DECIMAL: *pNumAttr = 18; break;
                    default: *pNumAttr = 0;
                }
            }
            break;
            
        case SQL_DESC_SCALE:
        case SQL_COLUMN_SCALE:
            if (pNumAttr) *pNumAttr = (col_type == SQL_DECIMAL) ? 2 : 0;
            break;
            
        case SQL_DESC_NULLABLE:
        case SQL_COLUMN_NULLABLE:
            if (pNumAttr) *pNumAttr = SQL_NULLABLE;
            break;
            
        case SQL_DESC_DISPLAY_SIZE:
            if (pNumAttr) {
                switch (col_type) {
                    case SQL_INTEGER: *pNumAttr = 11; break;
                    case SQL_SMALLINT: *pNumAttr = 6; break;
                    case SQL_BIGINT: *pNumAttr = 20; break;
                    case SQL_VARCHAR:
                    case SQL_WVARCHAR: *pNumAttr = 255; break;
                    case SQL_DECIMAL: *pNumAttr = 20; break;
                    case SQL_TYPE_DATE: *pNumAttr = 10; break;
                    case SQL_TYPE_TIMESTAMP: *pNumAttr = 26; break;
                    default: *pNumAttr = 255;
                }
            }
            break;
            
        case SQL_DESC_UNSIGNED:
            if (pNumAttr) *pNumAttr = SQL_FALSE;
            break;
            
        case SQL_DESC_AUTO_UNIQUE_VALUE:
            if (pNumAttr) *pNumAttr = SQL_FALSE;
            break;
            
        case SQL_DESC_UPDATABLE:
            if (pNumAttr) *pNumAttr = SQL_ATTR_READONLY;
            break;
            
        // D29: ten fields the spec defines that used to fall through to a
        // silent zero. An application reading SQL_DESC_COUNT off a result set
        // was told the set had no columns.
        case SQL_DESC_COUNT:
            if (pNumAttr) {
                *pNumAttr = static_cast<SQLLEN>(stmt->column_names_.size());
            }
            break;

        case SQL_DESC_OCTET_LENGTH:
            if (pNumAttr) *pNumAttr = octet_length_for(col_type, col_size);
            break;

        case SQL_DESC_TYPE_NAME:
            emit_string(type_name_for(col_type));
            break;

        case SQL_DESC_LABEL:
        case SQL_DESC_BASE_COLUMN_NAME:
            emit_string(col_name);
            break;

        case SQL_DESC_TABLE_NAME:
        case SQL_DESC_BASE_TABLE_NAME:
            // The mock does not track which table a result column came from,
            // and the spec allows an empty string for that case. Saying so is
            // different from saying nothing.
            emit_string("");
            break;

        case SQL_DESC_UNNAMED:
            if (pNumAttr) {
                *pNumAttr = col_name.empty() ? SQL_UNNAMED : SQL_NAMED;
            }
            break;

        case SQL_DESC_CASE_SENSITIVE:
            if (pNumAttr) {
                *pNumAttr = is_character_type(col_type) ? SQL_TRUE : SQL_FALSE;
            }
            break;

        case SQL_DESC_SEARCHABLE:
            if (pNumAttr) *pNumAttr = SQL_PRED_SEARCHABLE;
            break;

        default:
            // D29: SQL_SUCCESS with a zeroed output said "the answer is 0"
            // for every field the driver does not implement.
            stmt->add_diagnostic(sqlstate::INVALID_DESCRIPTOR_FIELD, 0,
                                 "Invalid descriptor field identifier: "
                                 + std::to_string(iField));
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

} // extern "C"
