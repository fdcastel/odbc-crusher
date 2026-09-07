#!/usr/bin/env python3
"""Assert every SQL_API entry point carries the exception barrier.

IMPROVEMENT_PLAN.md D9. Letting a C++ exception unwind out of an entry point,
across the C ABI and into the Driver Manager, is undefined behaviour and in
practice terminates the host application. Every definition is wrapped in a
function-try-block via the MOCK_ENTRY_TRY / MOCK_ENTRY_CATCH pair declared in
src/driver/entry_guard.hpp.

That the barrier is *present* is a property of the source text, so it is
checked here rather than in the gtest suite, which covers how the handler
behaves. Same role as the mockodbc.def drift check (F11).

Usage:  python3 mock-driver/tools/check_entry_guards.py [mock-driver/src]
Exits non-zero and prints a GitHub Actions error annotation on failure.
"""
import os
import re
import sys

DEF_RE = re.compile(r'^SQLRETURN SQL_API ([A-Za-z0-9_]+)\(', re.M)


def skip_trivia(src, i):
    """Advance past whitespace and comments starting at i."""
    while i < len(src):
        if src[i].isspace():
            i += 1
        elif src.startswith('//', i):
            nl = src.find('\n', i)
            i = len(src) if nl < 0 else nl + 1
        elif src.startswith('/*', i):
            end = src.find('*/', i)
            i = len(src) if end < 0 else end + 2
        else:
            break
    return i


def close_paren(src, open_idx):
    depth = 0
    i = open_idx
    while i < len(src):
        if src[i] == '(':
            depth += 1
        elif src[i] == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def check_file(path):
    """Return (checked, [problem strings])."""
    with open(path, 'rb') as f:
        src = f.read().decode('utf-8').replace('\r\n', '\n')

    problems = []
    checked = 0
    for m in DEF_RE.finditer(src):
        name = m.group(1)
        close = close_paren(src, src.index('(', m.start()))
        if close < 0:
            problems.append('%s: unbalanced parameter list' % name)
            continue
        body = skip_trivia(src, close + 1)
        if src.startswith(';', body):
            continue                      # a declaration, not a definition
        checked += 1
        if not src.startswith('MOCK_ENTRY_TRY', body):
            problems.append('%s: no MOCK_ENTRY_TRY before the function body' % name)
    # Every guarded definition must also close with a catch. Counting is
    # enough: the compiler rejects a function-try-block with no handler, so
    # the only way these can disagree is a hand-edit that drops one.
    n_try = src.count('MOCK_ENTRY_TRY')
    n_catch = src.count('MOCK_ENTRY_CATCH(')
    if n_try != n_catch:
        problems.append('%d MOCK_ENTRY_TRY vs %d MOCK_ENTRY_CATCH' % (n_try, n_catch))
    if checked:
        problems.extend(check_include(src))
    return checked, problems


def check_include(src):
    """The barrier header must be included unconditionally.

    The first mechanical pass put `#include "driver/entry_guard.hpp"` after the
    *last* existing #include in each file, which in driver_main.cpp is
    `<windows.h>` inside an `#ifdef _WIN32`. Windows built fine and Linux and
    macOS did not, so a Windows-only local build could not see it. Cheap to
    check here; expensive to discover from a CI log.
    """
    depth = 0
    for line in src.split(chr(10)):
        stripped = line.strip()
        if stripped.startswith('#if'):
            depth += 1
        elif stripped.startswith('#endif'):
            depth = max(0, depth - 1)
        elif 'entry_guard.hpp' in stripped and stripped.startswith('#include'):
            if depth != 0:
                return ['includes driver/entry_guard.hpp inside a #if block; '
                        'it must be unconditional or non-Windows builds break']
            return []
    return ['uses the barrier macros without including driver/entry_guard.hpp']


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'mock-driver/src'
    if not os.path.isdir(root):
        print('::error::%s is not a directory' % root)
        return 2

    total = 0
    failed = False
    for dirpath, _, filenames in os.walk(root):
        for fn in sorted(filenames):
            if not fn.endswith('.cpp'):
                continue
            path = os.path.join(dirpath, fn)
            checked, problems = check_file(path)
            total += checked
            for p in problems:
                print('::error file=%s::%s' % (path.replace(os.sep, '/'), p))
                failed = True

    if failed:
        print('::error::Wrap the entry points listed above with MOCK_ENTRY_TRY / '
              'MOCK_ENTRY_CATCH from driver/entry_guard.hpp (D9).')
        return 1

    if total == 0:
        print('::error::No SQL_API entry-point definitions found under %s — '
              'the check itself is broken, not the sources.' % root)
        return 1

    print('All %d SQL_API entry points carry the exception barrier.' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
