#!/usr/bin/env python3
"""
tools/patch-config-sub.py
Teach autoconf's config.sub to recognise 'blueyos' as a valid OS name.

Without this patch, ./configure will fail with:
  Invalid configuration `i386-pc-blueyos': OS `blueyos' not recognized

Usage:
    # Patch every config.sub found under a source tree
    python3 /path/to/biscuits/tools/patch-config-sub.py /path/to/source-tree

    # Patch a specific config.sub
    python3 /path/to/biscuits/tools/patch-config-sub.py /path/to/config.sub

Run this once per extracted source package before ./configure.
It is safe to run multiple times (already-patched files are skipped).
"""

import sys
import os
import re


def find_config_subs(root):
    """Return all config.sub paths found under root (or root itself)."""
    if os.path.isfile(root) and os.path.basename(root) == "config.sub":
        return [root]
    result = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Skip hidden directories and common build output dirs
        dirnames[:] = [
            d for d in dirnames
            if not d.startswith(".") and d not in ("build", ".git")
        ]
        if "config.sub" in filenames:
            result.append(os.path.join(dirpath, "config.sub"))
    return result


def patch(path):
    with open(path, "r", errors="replace") as f:
        text = f.read()

    if "blueyos" in text:
        print(f"  [skip] already patched: {path}")
        return True

    new_text = None

    # Strategy 1: find '| linux* \' as a continuation line in the OS case block.
    # In autoconf 2.69–2.71 (binutils 2.41, gcc 13.x) the OS list looks like:
    #       | linux* \
    #       | linux-android* \
    # We insert '| blueyos* \' immediately before the first 'linux' entry.
    m = re.search(r"(\|\s+linux\*\s*\\)", text)
    if m:
        # Preserve indentation of the matched line
        line_start = text.rfind("\n", 0, m.start()) + 1
        indent = ""
        for ch in text[line_start : m.start()]:
            if ch in (" ", "\t"):
                indent += ch
            else:
                break
        insert = f"| blueyos* \\\n{indent}"
        new_text = text[: m.start()] + insert + text[m.start() :]

    # Strategy 2: '| linux*' without a trailing backslash (closing entry or
    # inline form).  Less common but present in some older versions.
    if new_text is None:
        m = re.search(r"(\|\s+linux\*\b)", text)
        if m:
            new_text = text[: m.start()] + "| blueyos* | " + text[m.start() :]

    # Strategy 3: any '| linux' occurrence.
    if new_text is None:
        m = re.search(r"(\|\s+linux\b)", text)
        if m:
            new_text = text[: m.start()] + "| blueyos | linux" + text[m.start() + len(m.group()) :]

    if new_text is None or "blueyos" not in new_text:
        print(
            f"  [WARN] could not find insertion point in: {path}\n"
            f"         Add '| blueyos*' to the OS list manually.",
            file=sys.stderr,
        )
        return False

    with open(path, "w") as f:
        f.write(new_text)

    print(f"  [ok]   patched: {path}")
    return True


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    paths = []
    for arg in sys.argv[1:]:
        found = find_config_subs(arg)
        if not found:
            print(f"  [warn] no config.sub found under: {arg}", file=sys.stderr)
        paths.extend(found)

    if not paths:
        print("No config.sub files found.", file=sys.stderr)
        sys.exit(1)

    failures = 0
    for p in paths:
        if not patch(p):
            failures += 1

    if failures:
        sys.exit(1)


if __name__ == "__main__":
    main()
