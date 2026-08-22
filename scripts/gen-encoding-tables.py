#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Generate the ISO 2022 encoding tables used by src/encoding.c:
#
#   src/encoding/DECdrawing.inc
#   src/encoding/uk.inc
#
# Each is a C initializer for a StaticTableEncoding, built from the matching
# .tbl file beside it. A .tbl line is "COL/ROW  U+XXXX" or "COL/ROW  "C"",
# with '#' comments; COL/ROW is the ISO 2022 cell, so 3/1 is 0x31.
#
#     python3 scripts/gen-encoding-tables.py
#
# Run with no arguments to (re)write every table in place. The generated
# files are checked in, so an ordinary build never needs this.

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENCDIR = os.path.normpath(os.path.join(HERE, "..", "src", "encoding"))


def convert(tbl_path):
    encname = os.path.basename(tbl_path)[:-len(".tbl")]

    out = ["static const struct StaticTableEncoding encoding_%s = {" % encname,
           "  { .decode = &decode_table },",
           "  {"]

    with open(tbl_path, encoding="utf-8") as f:
        for line in f:
            line = re.sub(r"\s*#.*", "", line.rstrip("\n"))   # strip comment
            # 3/1 -> [0x31]
            line = re.sub(r"^(\d+)/(\d+)",
                          lambda m: "[0x%02x]" % (int(m.group(1)) * 16
                                                  + int(m.group(2))),
                          line, count=1)
            # "A" -> 0x0041
            line = re.sub(r'"(.)"', lambda m: "0x%04x" % ord(m.group(1)),
                          line, count=1)
            # U+0041 -> 0x0041
            line = line.replace("U+", "0x", 1)
            out.append("    %s," % line)

    out += ["  }", "};"]
    return "\n".join(out) + "\n"


def main():
    for name in sorted(os.listdir(ENCDIR)):
        if not name.endswith(".tbl"):
            continue
        tbl = os.path.join(ENCDIR, name)
        inc = tbl[:-len(".tbl")] + ".inc"
        with open(inc, "w") as f:
            f.write(convert(tbl))
        print("wrote %s" % inc, file=sys.stderr)


if __name__ == "__main__":
    main()
