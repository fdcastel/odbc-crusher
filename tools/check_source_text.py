#!/usr/bin/env python3
"""Every tracked source file must still be a text file.

`mock-driver/tests/test_dml.cpp` was committed in 3163e3e with a literal NUL
byte at line 259. The comment meant to write ``abc\\0``; the backslash was
eaten before it reached the file and the byte itself went in. Nothing noticed
for eleven commits, because nothing *could*: git classifies a file holding a
NUL as binary, so `git diff`, `git blame`, `git grep` and ripgrep all skip it
in silence. A 660-line test file had dropped out of every text tool in the
repository and still compiled, still passed, still looked fine in an editor.

That is the failure mode this repository keeps meeting from the other side: a
thing that does nothing and says nothing about it. The compiler cannot catch
this one - a NUL inside a `//` comment is legal C++ - so it needs a check of
its own.

Two rules, both about a file being readable by the tools that read files:

  * no NUL byte anywhere in a tracked text source; and
  * decodable as UTF-8, since the tree is UTF-8 throughout and a stray byte
    from a mis-encoded paste is the same class of invisible damage.

And a third of the same family, added by D90: **the driver does not print.**
`fprintf(stderr, "[T] before=...")` and its twin were committed in b2afc49 on
the WHERE-marker path and fired on every parameterised SELECT or DELETE, from
inside a shared library loaded into someone else\'s process. Unconditional
console output from a driver is a defect in its own right, and it is the same
shape as the two above: harmless-looking, invisible in review, caught by
nothing in the build. Checked here rather than in the gtest suite because
stderr is a process-global a test would have to capture and restore, while the
property wanted - the call is not in the source - is a property of the text.

Run from the repository root. Exits non-zero and names file, line and column
on the first offence in each file.
"""
import os
import re
import subprocess
import sys

# Text sources. Anything genuinely binary (an .ico, a .png) is excluded by
# not being listed rather than by a guess at its content.
SUFFIXES = (
    '.c', '.cc', '.cpp', '.h', '.hpp', '.py', '.md', '.txt', '.yml', '.yaml',
    '.json', '.cmake', '.def', '.in', '.sh', '.ps1',
)
BASENAMES = ('CMakeLists.txt', 'Makefile', '.gitignore', '.gitattributes')

# D90: console writes from the driver itself. Only the mock's `src` - its
# tests print, `tools/` are command-line programs, and the crusher is an
# application whose whole job is to write a report.
DRIVER_SRC = os.path.join('mock-driver', 'src')
PRINT_RE = re.compile(
    r'^(?!\s*//).*\b(?:fprintf\s*\(\s*std(?:err|out)'
    r'|printf\s*\(|std::c(?:out|err)\s*<<|puts\s*\()', re.M)


def tracked_files():
    out = subprocess.run(['git', 'ls-files', '-z'],
                         stdout=subprocess.PIPE, check=True).stdout
    for raw in out.split(b'\x00'):
        if not raw:
            continue
        path = raw.decode('utf-8')
        base = os.path.basename(path)
        if base in BASENAMES or path.endswith(SUFFIXES):
            yield path


def position(data, offset):
    """1-based line and column of `offset` within `data`."""
    line = data.count(b'\n', 0, offset) + 1
    line_start = data.rfind(b'\n', 0, offset) + 1
    return line, offset - line_start + 1


def main():
    problems = []
    checked = 0

    for path in tracked_files():
        if not os.path.isfile(path):
            continue          # a deleted-but-staged path
        with open(path, 'rb') as fh:
            data = fh.read()
        checked += 1

        nul = data.find(b'\x00')
        if nul != -1:
            line, col = position(data, nul)
            problems.append(
                '%s:%d:%d: NUL byte in a text source. Git treats this file as '
                'binary, so diff, blame and grep all skip it. A literal `\\0` '
                'in a string or comment is written as two characters.'
                % (path, line, col))
            continue

        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError as exc:
            line, col = position(data, exc.start)
            problems.append(
                '%s:%d:%d: not valid UTF-8 (%s). The tree is UTF-8 throughout.'
                % (path, line, col, exc.reason))
            continue

        # D90: a driver is loaded into someone else's process and does not
        # get to write on their console.
        if os.path.normpath(path).startswith(DRIVER_SRC):
            m = PRINT_RE.search(text)
            if m:
                line = text.count('\n', 0, m.start()) + 1
                problems.append(
                    '%s:%d: the driver writes to the console. Two of these '
                    'were committed on the WHERE-marker path and fired on '
                    'every parameterised SELECT (D90). Use a diagnostic '
                    'record, or delete it.' % (path, line))

    if problems:
        print('check_source_text: %d file(s) are not clean text\n' % len(problems))
        for p in problems:
            print('  ' + p)
        return 1

    print('check_source_text: %d tracked text files, all clean' % checked)
    return 0


if __name__ == '__main__':
    sys.exit(main())
