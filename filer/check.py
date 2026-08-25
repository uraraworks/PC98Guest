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
  5. Every message in g_msgJA/g_msgEN is checked against the screen-cell
     limit that applies to how FILER.C actually uses it, in both languages.
     Screen-cell width follows the same rule as FILER.C's own text_width():
     a lead byte in 0x81-0x9F or 0xE0-0xFC (and the byte after it) counts as
     2 cells, anything else counts as 1. Each message index is classified
     (see MSG_LIMITS below) into one of:
       - "box"    : placed alone into a header-box row (box_row()), must
                    fit within BOX_WIDTH cells.
       - "cmdline": the bottom-row help text (MSG_CMDLINE), must fit within
                    CMDLINE_WIDTH cells (the cap draw_screen_frame() now
                    truncates it to).
       - "dialog" : placed alone into a draw_dialog() box, must fit within
                    DIALOG_WIDTH cells.
       - "fragment": concatenated at runtime with other fragments and/or
                    variable-length data (a path, a filename, a number)
                    before being placed into a row. The combined row is
                    still safely truncated at render time (sappend() /
                    put_str_cells() never split a multi-byte character),
                    but no meaningful per-message cap can be computed
                    without knowing the runtime data, so these are
                    deliberately NOT length-checked here. They are still
                    listed in the report so the exclusion is visible
                    rather than silent.
     BOX_WIDTH, CMDLINE_WIDTH and DIALOG_WIDTH are all read out of
     FILER.C's #define lines, never hard-coded here.

Usage:
  python3 check.py [path-to-FILER.C]
"""
import re
import sys
import os


# Classification of every g_msgJA/g_msgEN index, by the #define name used
# as its index in FILER.C (see the MSG_* #defines near the top of
# FILER.C). Keep this in sync with FILER.C if messages are added, removed,
# or their call sites change how they're assembled onto the screen.
#
#   "box"      -> checked against BOX_WIDTH
#   "cmdline"  -> checked against CMDLINE_WIDTH
#   "dialog"   -> checked against DIALOG_WIDTH
#   "fragment" -> not length-checked (see module docstring); listed only
MSG_LIMITS = {
    "MSG_TITLE":                ("box",      0),
    "MSG_PATH_PREFIX":          ("fragment", 1),
    "MSG_TRUNC_PREFIX":         ("fragment", 2),
    "MSG_TRUNC_SUFFIX":         ("fragment", 3),
    "MSG_INFO_PREFIX":          ("fragment", 4),
    "MSG_INFO_EMPTY":           ("fragment", 5),
    "MSG_DISK_UNAVAIL":         ("box",      6),
    "MSG_DISK_TOTAL":           ("fragment", 7),
    "MSG_DISK_USED":            ("fragment", 8),
    "MSG_DISK_FREE":            ("fragment", 9),
    "MSG_BYTES_SUFFIX":         ("fragment", 10),
    "MSG_ATTR_LABEL":           ("fragment", 11),
    "MSG_CMDLINE":              ("cmdline",  12),
    "MSG_MARKED_LABEL":         ("fragment", 13),
    "MSG_DEL_CONFIRM_ONE_PRE":  ("fragment", 14),
    "MSG_DEL_CONFIRM_ONE_SUF":  ("fragment", 15),
    "MSG_DEL_CONFIRM_MARK_PRE": ("fragment", 16),
    "MSG_DEL_CONFIRM_MARK_SUF": ("fragment", 17),
    "MSG_DEL_ERR_ISDIR":        ("dialog",   18),
    "MSG_DEL_ERR_FAILED":       ("dialog",   19),
}


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

    # ---- extract the screen-cell width limits and both message tables -
    box_width = extract_define_int(text, "BOX_WIDTH")
    cmdline_width = extract_define_int(text, "CMDLINE_WIDTH")
    dialog_width = extract_define_int(text, "DIALOG_WIDTH")
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

    # ---- check 5: every message fits the cell limit its use site implies --
    limits = {
        "box": box_width,
        "cmdline": cmdline_width,
        "dialog": dialog_width,
    }
    n = len(ja)
    known_indices = set(idx for (_kind, idx) in MSG_LIMITS.values())
    if known_indices != set(range(n)):
        missing = sorted(set(range(n)) - known_indices)
        extra = sorted(known_indices - set(range(n)))
        fail("MSG_LIMITS in check.py is out of sync with FILER.C's message "
             "table (%d entries): missing indices %r, unknown indices %r - "
             "update MSG_LIMITS to match FILER.C's MSG_* #defines and how "
             "each one is used" % (n, missing, extra))

    report = []
    for name in sorted(MSG_LIMITS, key=lambda k: MSG_LIMITS[k][1]):
        kind, idx = MSG_LIMITS[name]
        if kind == "fragment":
            report.append("  [%2d] %-26s fragment (not length-checked; "
                           "concatenated at runtime)" % (idx, name))
            continue
        limit = limits[kind]
        for label, table in (("g_msgJA", ja), ("g_msgEN", en)):
            s = table[idx]
            w = cell_width(s.encode("cp932"))
            if w > limit:
                fail("%s[%s] (index %d) is %d cells wide, exceeds the %s "
                     "limit (%d cells) - %r" %
                     (label, name, idx, w, kind.upper() + "_WIDTH" if kind != "box"
                      else "BOX_WIDTH", limit, s))
        ja_w = cell_width(ja[idx].encode("cp932"))
        en_w = cell_width(en[idx].encode("cp932"))
        report.append("  [%2d] %-26s %-8s limit=%3d  JA=%3d  EN=%3d" %
                       (idx, name, kind, limit, ja_w, en_w))

    print("PASS: 5) every message fits the cell limit implied by its use "
          "site, in both languages")
    print("     (box=%d cmdline=%d dialog=%d cells)" %
          (box_width, cmdline_width, dialog_width))
    for line in report:
        print(line)

    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
