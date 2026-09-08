#!/usr/bin/env python3
"""Assert no `sizeof(x)` survives where x is a GuardedBuffer — IMPROVEMENT_PLAN D62.

`sizeof` on a bare array is the buffer's length. On a `GuardedBuffer` it is the
size of the *object* — a size_t, an element and a std::vector, about forty
bytes — and the compiler accepts it silently. D62 converted 44 buffers and five
of these survived as read lengths; in `call_native_sql` that turned a 4096-byte
read into ~40 and truncated `SQLNativeSql`'s output at a point that moved
between runs. Nothing in the build catches it. The byte-identical report diff
did, once, by luck of which probe it hit.

Matching is by **nearest preceding declaration**, not by name. Several files
declare a `SQLCHAR value[256]` in one probe and a `SQLINTEGER value` in
another, and `sizeof(value)` is correct in the second — a name-keyed check
reports those as defects and a suppression list would then have to be
maintained against real ones.

Exit status is 1 when anything is found, so it can gate CI.
"""
import io
import os
import re
import sys

ROOTS = ['src', 'tests']

GUARDED = re.compile(r'\bGuardedBuffer<[^>]+>\s+(\w+)\s*\(')
# Any other declaration of the same name: a type, the name, then `[`, `=`, `;`
# or `,`. Deliberately loose — it only has to establish that *something else*
# with this name was declared closer.
PLAIN = re.compile(r'\b(?:const\s+)?[A-Za-z_]\w*\s*[*&]?\s+(\w+)\s*(?:\[|=|;|,|\))')
SIZEOF = re.compile(r'\bsizeof\((\w+)\)')


def check(path):
    text = io.open(path, encoding='utf-8', newline='').read().replace('\r\n', '\n')
    lines = text.split('\n')

    # name -> [(line, is_guarded)], in source order.
    decls = {}
    for n, line in enumerate(lines, 1):
        for m in GUARDED.finditer(line):
            decls.setdefault(m.group(1), []).append((n, True))
        for m in PLAIN.finditer(line):
            name = m.group(1)
            if GUARDED.search(line):
                continue          # already recorded as guarded
            decls.setdefault(name, []).append((n, False))

    if not any(g for sites in decls.values() for _, g in sites):
        return []

    bad = []
    for n, line in enumerate(lines, 1):
        for m in SIZEOF.finditer(line):
            sites = decls.get(m.group(1))
            if not sites:
                continue
            nearest = None
            for decl_line, is_guarded in sites:
                if decl_line <= n:
                    nearest = is_guarded
            if nearest:
                bad.append((n, m.group(1), line.strip()[:100]))
    return bad


def main():
    total = 0
    for root in ROOTS:
        for dirpath, _dirs, names in os.walk(root):
            for n in sorted(names):
                if not n.endswith(('.cpp', '.hpp')):
                    continue
                path = os.path.join(dirpath, n).replace(os.sep, '/')
                for line_no, name, src in check(path):
                    print('%s:%d: sizeof(%s) is the GuardedBuffer object, not '
                          'the buffer' % (path, line_no, name))
                    print('    %s' % src)
                    print('    use %s.declared_bytes() for a length in bytes, '
                          'or %s.declared_elements() for one in elements'
                          % (name, name))
                    total += 1

    if total:
        print('\n%d bad sizeof — see IMPROVEMENT_PLAN.md D62' % total)
        return 1
    print('No sizeof() on a GuardedBuffer.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
