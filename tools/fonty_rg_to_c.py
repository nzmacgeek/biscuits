#!/usr/bin/env python3

import sys
from pathlib import Path


def flush_glyph(font, labels, rows):
    if not labels:
        return
    if not rows:
        raise SystemExit(f"glyph {labels[0]} has no bitmap rows")
    if any(len(row) != 8 for row in rows):
        raise SystemExit(f"glyph {labels[0]} must be 8 pixels wide")

    for label in labels:
        if not label.startswith("U+"):
            continue
        codepoint = int(label[2:], 16)
        if 0 <= codepoint < 128:
            font[codepoint] = [sum((1 << (7 - index)) for index, ch in enumerate(row[:8]) if ch == "0") for row in rows]


def parse_sbf(path):
    font = {codepoint: [0] * 8 for codepoint in range(128)}
    labels = []
    rows = []

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            flush_glyph(font, labels, rows)
            labels = []
            rows = []
            continue
        if line.startswith("[") and line.endswith("]"):
            labels.append(line[1:-1])
            continue
        if set(line) <= {".", "0"}:
            rows.append(line[:8].ljust(8, "."))

    flush_glyph(font, labels, rows)
    return font


def write_header(font, output_path):
    lines = [
        "#pragma once",
        "",
        "#include \"../../include/types.h\"",
        "",
        "#define M68K_FONTY_RG_HEIGHT 8",
        "#define M68K_FONTY_RG_WIDTH 8",
        "",
        "static const uint8_t m68k_fonty_rg_ascii[128][M68K_FONTY_RG_HEIGHT] = {",
    ]

    for codepoint in range(128):
        glyph = ", ".join(f"0x{row:02x}" for row in font[codepoint])
        if 32 <= codepoint <= 126:
            comment = chr(codepoint)
            if comment == "\\":
                comment = "\\\\"
            elif comment == "\"":
                comment = '\\"'
            lines.append(f"    [{codepoint}] = {{{glyph}}}, /* {comment} */")
        else:
            lines.append(f"    [{codepoint}] = {{{glyph}}},")

    lines.append("};")
    lines.append("")
    output_path.write_text("\n".join(lines), encoding="utf-8")


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: fonty_rg_to_c.py <input.sbf> <output.h>")

    input_path = Path(argv[1])
    output_path = Path(argv[2])
    font = parse_sbf(input_path)
    write_header(font, output_path)


if __name__ == "__main__":
    main(sys.argv)