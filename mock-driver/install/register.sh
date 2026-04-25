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

cat >"$template" <<EOF
[Mock ODBC Driver]
Description = Mock ODBC Driver for conformance testing
Driver      = $DRIVER_PATH
Setup       = $DRIVER_PATH
FileUsage   = 0
UsageCount  = 1
EOF

# -i install, -d driver-section, -f template file. -h sets user scope.
# shellcheck disable=SC2086
"$ODBCINST" -i -d $INI_FLAG -f "$template"

echo
echo "Mock ODBC Driver registered."
echo "Verify with:"
echo "  odbcinst -q -d"
echo
echo "Connection string:"
echo '  "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=100;"'
