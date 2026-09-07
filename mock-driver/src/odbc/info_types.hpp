#pragma once

// Which SQLGetInfo types answer with a character string — D49.
//
// SQLGetInfoW needs to know this to convert the ANSI answer, and it used to
// keep its own hand-maintained list. The two drifted: `SQL_LIKE_ESCAPE_CLAUSE`
// answers `"Y"` on the ANSI path and came back empty through the W path, which
// on Windows is *every* path — the driver manager converts an application's
// ANSI call into a W call before it reaches the driver. Nine string types were
// missing from the wrapper's copy.
//
// One list, in the same translation unit as the switch it describes, so a new
// string type is added in one place rather than two.

#include "driver/common.hpp"

namespace mock_odbc {

// True when SQLGetInfo answers `type` with a character string rather than a
// number. Mirrors the RETURN_STRING cases in info_api.cpp's switch.
bool info_type_is_string(SQLUSMALLINT type);

}  // namespace mock_odbc
