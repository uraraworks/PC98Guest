#!/usr/bin/env python3
"""
check.py - integrity checks for FILER.C

FILER.C is encoded as CP932 (Shift_JIS) and contains raw Japanese bytes in
its message table. This script must never itself convert the file's
encoding: it reads FILER.C as bytes and only decodes to text for parsing,
never writes the file back.

Checks (all must pass, otherwise this exits non-zero):
  1. FILER.C decodes as CP932 without error.
  2. The byte sequence EF BF BD (the UTF-8 encoding of U+FFFD, the mark left
     behind when non-UTF-8 bytes get force-converted and lossily replaced)
     does not appear anywhere in the file.
  3. g_msgJA and g_msgEN have the same number of entries.
  4. Neither message table contains an empty string.
  5. Every message that is placed directly into the header box (currently
     just MSG_TITLE, via build_title_row()) fits within BOX_WIDTH screen
     cells, in both languages. Screen-cell width follows the same rule as
     FILER.C's own text_width(): a lead byte in 0x81-0x9F or 0xE0-0xFC (and
     the byte after it) counts as 2 cells, anything else counts as 1.

Usage:
  python3 check.py [path-to-FILER.C]
"""
import re
import sys
import os


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def cell_width(raw_bytes):
    """screen-cell width of a CP932-encoded byte string, using the same
    lead-byte rule as FILER.C's text_width()."""
    w = 0
    i = 0
    n = len(raw_bytes)
    while i < n:
        c = raw_bytes[i]
        if (0x81 <= c <= 0x9F) or (0xE0 <= c <= 0xFC):
            w += 2
            i += 2
        else:
            w += 1
            i += 1
    return w


def extract_array(text, name):
    """pulls the double-quoted string literals out of
    'const char *name[] = { ... };'. FILER.C's message tables use plain
    literals with no escaped quotes, so a straightforward literal scan is
    enough (no need for a full C string-literal parser)."""
    m = re.search(r"const char \*" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if m is None:
        fail("could not find array %s[] in FILER.C" % name)
    body = m.group(1)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def extract_define_int(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(\d+)", text)
    if m is None:
        fail("could not find #define %s in FILER.C" % name)
    return int(m.group(1))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "FILER.C")

    with open(path, "rb") as f:
        data = f.read()

    # ---- check 1: CP932-decodable -----------------------------------
    try:
        text = data.decode("cp932")
    except UnicodeDecodeError as e:
        fail("FILER.C is not valid CP932: %s" % e)
    print("PASS: 1) FILER.C decodes as CP932")

    # ---- check 2: no U+FFFD replacement-character bytes --------------
    fffd_count = data.count(b"\xef\xbf\xbd")
    if fffd_count != 0:
        fail("found %d occurrence(s) of EF BF BD (U+FFFD) - the file was "
             "likely round-tripped through a lossy UTF-8 conversion" % fffd_count)
    print("PASS: 2) no EF BF BD (U+FFFD) bytes in the file")

    # ---- extract BOX_WIDTH and both message tables --------------------
    box_width = extract_define_int(text, "BOX_WIDTH")
    ja = extract_array(text, "g_msgJA")
    en = extract_array(text, "g_msgEN")

    # ---- check 3: same element count ----------------------------------
    if len(ja) != len(en):
        fail("g_msgJA has %d entries but g_msgEN has %d" % (len(ja), len(en)))
    print("PASS: 3) g_msgJA and g_msgEN have the same element count (%d)" % len(ja))

    # ---- check 4: no empty strings in either table --------------------
    for i, s in enumerate(ja):
        if s == "":
            fail("g_msgJA[%d] is an empty string" % i)
    for i, s in enumerate(en):
        if s == "":
            fail("g_msgEN[%d] is an empty string" % i)
    print("PASS: 4) no empty strings in g_msgJA or g_msgEN")

    # ---- check 5: header-box messages fit within BOX_WIDTH cells ------
    # MSG_TITLE (index 0) is the only message placed directly into a
    # header box row with no other runtime-length content appended
    # (build_title_row() in FILER.C), so it is the one whose byte
    # length translates straight into on-screen box width.
    MSG_TITLE = 0
    for label, table in (("g_msgJA", ja), ("g_msgEN", en)):
        title = table[MSG_TITLE]
        w = cell_width(title.encode("cp932"))
        if w > box_width:
            fail("%s[MSG_TITLE] is %d cells wide, exceeds BOX_WIDTH (%d)" %
                 (label, w, box_width))
    print("PASS: 5) MSG_TITLE fits within BOX_WIDTH (%d) cells in both languages" % box_width)

    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
