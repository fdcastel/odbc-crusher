// Descriptor API - SQLGetDescField, SQLSetDescField, etc.

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLGetDescField(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    
    (void)iRecord;
    (void)cbValueMax;
    
    switch (iField) {
        case SQL_DESC_COUNT:
            if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = desc->count_;
            if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
            break;
            
        case SQL_DESC_ALLOC_TYPE:
            if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = desc->alloc_type_;
            if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
            break;
            
        default:
            // Return success for unknown fields
            if (pcbValue) *pcbValue = 0;
            break;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetDescField(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    
    (void)iRecord;
    (void)cbValue;
    
    switch (iField) {
        case SQL_DESC_COUNT:
            desc->count_ = static_cast<SQLSMALLINT>(reinterpret_cast<intptr_t>(rgbValue));
            break;
            
        default:
            // Ignore unknown fields
            break;
    }
    
    return SQL_SUCCESS;
}

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
    SQLSMALLINT* pNullable) {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    
    if (iRecord < 1 || iRecord > static_cast<SQLSMALLINT>(desc->records_.size())) {
        return SQL_NO_DATA;
    }
    
    const auto& rec = desc->records_[iRecord - 1];
    
    (void)szName;
    (void)cbNameMax;
    (void)pcbName;
    
    if (pfType) *pfType = rec.type;
    if (pfSubType) *pfSubType = rec.datetime_interval_code;
    if (pLength) *pLength = rec.length;
    if (pPrecision) *pPrecision = rec.precision;
    if (pScale) *pScale = rec.scale;
    if (pNullable) *pNullable = rec.nullable;
    
    return SQL_SUCCESS;
}

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
    SQLLEN* pcbIndicator) {
    
    auto* desc = validate_desc_handle(hdesc);
    if (!desc) return SQL_INVALID_HANDLE;
    
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

SQLRETURN SQL_API SQLCopyDesc(
    SQLHDESC hDescSource,
    SQLHDESC hDescTarget) {
    
    auto* src = validate_desc_handle(hDescSource);
    auto* tgt = validate_desc_handle(hDescTarget);
    
    if (!src || !tgt) return SQL_INVALID_HANDLE;
    
    tgt->count_ = src->count_;
    tgt->records_ = src->records_;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColAttribute(
    SQLHSTMT hstmt,
    SQLUSMALLINT iCol,
    SQLUSMALLINT iField,
    SQLPOINTER pCharAttr,
    SQLSMALLINT cbCharAttrMax,
    SQLSMALLINT* pcbCharAttr,
    SQLLEN* pNumAttr) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (iCol < 1 || iCol > static_cast<SQLUSMALLINT>(stmt->column_names_.size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const std::string& col_name = stmt->column_names_[iCol - 1];
    SQLSMALLINT col_type = stmt->column_types_[iCol - 1];
    
    switch (iField) {
        case SQL_DESC_NAME:
        case SQL_COLUMN_NAME:
            if (pCharAttr && cbCharAttrMax > 0) {
                size_t copy_len = std::min(col_name.length(), 
                                           static_cast<size_t>(cbCharAttrMax - 1));
                std::memcpy(pCharAttr, col_name.c_str(), copy_len);
                static_cast<char*>(pCharAttr)[copy_len] = '\0';
            }
            if (pcbCharAttr) *pcbCharAttr = static_cast<SQLSMALLINT>(col_name.length());
            break;
            
        case SQL_DESC_TYPE:
        case SQL_COLUMN_TYPE:
            if (pNumAttr) *pNumAttr = col_type;
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
            
        default:
            if (pNumAttr) *pNumAttr = 0;
            if (pcbCharAttr) *pcbCharAttr = 0;
            break;
    }
    
    return SQL_SUCCESS;
}

} // extern "C"
