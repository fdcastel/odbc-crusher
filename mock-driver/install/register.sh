#!/usr/bin/env bash
#
# Register Mock ODBC Driver on Linux / macOS via unixODBC.
#
# Usage:
#   ./register.sh                           # Auto-detect next to this script
#   ./register.sh /path/to/libmockodbc.so   # Explicit driver path
#
# System-wide (requires sudo) vs. user-level: if run as root the registration
# lands in /etc/odbcinst.ini; otherwise it uses $HOME/.odbcinst.ini, which
# unixODBC honours when ODBCSYSINI is unset.

set -euo pipefail

DRIVER_PATH="${1:-}"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

# Auto-detect driver binary: same directory as script, then typical build
# output paths. Extension differs per platform.
if [[ -z "$DRIVER_PATH" ]]; then
    for candidate in \
        "$script_dir/libmockodbc.so" \
        "$script_dir/libmockodbc.dylib" \
        "$script_dir/../build/libmockodbc.so" \
        "$script_dir/../build/libmockodbc.dylib"
    do
        if [[ -f "$candidate" ]]; then
            DRIVER_PATH="$(cd "$(dirname "$candidate")" && pwd)/$(basename "$candidate")"
            break
        fi
    done
fi

if [[ -z "$DRIVER_PATH" || ! -f "$DRIVER_PATH" ]]; then
    echo "error: driver binary not found." >&2
    echo "hint:  pass the path explicitly, e.g. $0 /usr/local/lib/libmockodbc.so" >&2
    exit 1
fi

# Find odbcinst. `command -v` honours $PATH, but Linux sudo's secure_path
# can override what the caller passed in (Ubuntu defaults set it explicitly,
# macOS doesn't), so check the standard locations as a fallback.
if command -v odbcinst >/dev/null 2>&1; then
    ODBCINST=$(command -v odbcinst)
elif [[ -x /usr/bin/odbcinst ]]; then
    ODBCINST=/usr/bin/odbcinst
elif [[ -x /opt/homebrew/bin/odbcinst ]]; then
    ODBCINST=/opt/homebrew/bin/odbcinst
elif [[ -x /usr/local/bin/odbcinst ]]; then
    ODBCINST=/usr/local/bin/odbcinst
else
    echo "error: 'odbcinst' not found. Install unixODBC first:" >&2
    echo "       apt-get install unixodbc   # Debian/Ubuntu" >&2
    echo "       brew install unixodbc      # macOS" >&2
    exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
    INSTALL_SCOPE="system"
    INI_FLAG=""
    INI_PATH="$("$ODBCINST" -j 2>/dev/null | awk -F': +' '/DRIVERS/ {print $2}')"
else
    INSTALL_SCOPE="user"
    INI_FLAG="-h"
    INI_PATH="$HOME/.odbcinst.ini"
fi

echo "Registering Mock ODBC Driver ($INSTALL_SCOPE scope)"
echo "Driver binary: $DRIVER_PATH"
echo "Target ini:    ${INI_PATH:-unknown}"

template="$(mktemp)"
trap 'rm -f "$template"' EXIT

# D3: these have to match what register.ps1 writes to the Windows registry,
# or the Linux and Windows CI slots are not testing the same registration.
# unixODBC consults APILevel, ConnectFunctions and DriverODBCVer when it
# decides how to treat a driver - which functions it may assume exist, and
# whether to emulate the ones it does not find - so omitting them made the two
# platforms structurally different before a single probe ran.
cat >"$template" <<EOF
[Mock ODBC Driver]
Description      = Mock ODBC Driver for conformance testing
Driver           = $DRIVER_PATH
Setup            = $DRIVER_PATH
APILevel         = 2
ConnectFunctions = YYY
DriverODBCVer    = 03.80
SQLLevel         = 1
FileUsage        = 0
UsageCount       = 1
EOF

# D30: `odbcinst -i -d` on a section that already exists bumps UsageCount and
# leaves Driver= alone, so rebuilding into a different directory left the old
# path registered while this script printed "registered." Every mock-backed
# test then loaded yesterday's binary, or nothing at all.
#
# Uninstall first when the section is there. `-u` decrements UsageCount and
# removes the section when it reaches zero, so this loops: a section written
# by several earlier runs has a count above one and one -u will not clear it.
# Bounded, because a driver whose count cannot be driven to zero would
# otherwise spin here forever.
# shellcheck disable=SC2086
if "$ODBCINST" -q -d $INI_FLAG 2>/dev/null | grep -q '^\[Mock ODBC Driver\]$'; then
    echo "Existing registration found; removing it so Driver= is rewritten."
    attempts=0
    # shellcheck disable=SC2086
    while "$ODBCINST" -q -d $INI_FLAG 2>/dev/null | grep -q '^\[Mock ODBC Driver\]$'; do
        attempts=$((attempts + 1))
        if [ "$attempts" -gt 10 ]; then
            echo "ERROR: could not remove the existing [Mock ODBC Driver] section" >&2
            echo "       after 10 attempts. Edit the odbcinst.ini by hand:" >&2
            echo "       ${INI_PATH:-run 'odbcinst -j' to find it}" >&2
            exit 1
        fi
        # shellcheck disable=SC2086
        "$ODBCINST" -u -d $INI_FLAG -n "Mock ODBC Driver" >/dev/null 2>&1 || break
    done
fi

# -i install, -d driver-section, -f template file. -h sets user scope.
# shellcheck disable=SC2086
"$ODBCINST" -i -d $INI_FLAG -f "$template"

# D30: verify rather than announce. `odbcinst -i` can succeed and still leave
# a Driver= that is not the one we asked for - which is the whole bug above -
# so the script reads it back and fails loudly if it disagrees.
# shellcheck disable=SC2086
registered="$("$ODBCINST" -q -d $INI_FLAG -n "Mock ODBC Driver" 2>/dev/null \
    | sed -n 's/^ *Driver *= *//p' | head -1)"
if [ -z "$registered" ]; then
    echo "ERROR: the driver section is missing after install." >&2
    exit 1
fi
if [ "$registered" != "$DRIVER_PATH" ]; then
    echo "ERROR: registration points at the wrong binary." >&2
    echo "  wanted:     $DRIVER_PATH" >&2
    echo "  registered: $registered" >&2
    exit 1
fi

echo
echo "Mock ODBC Driver registered."
echo "Verify with:"
echo "  odbcinst -q -d"
echo
echo "Connection string:"
echo '  "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=100;"'
