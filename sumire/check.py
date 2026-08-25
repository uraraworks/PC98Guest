#!/usr/bin/env python3
"""
check.py - integrity checks for SUMIRE.C

SUMIRE.C is encoded as CP932 (Shift_JIS) and contains raw Japanese bytes in
its message table. This script must never itself convert the file's
encoding: it reads SUMIRE.C as bytes and only decodes to text for parsing,
never writes the file back.

Checks (all must pass, otherwise this exits non-zero):
  1. SUMIRE.C decodes as CP932 without error.
  2. The byte sequence EF BF BD (the UTF-8 encoding of U+FFFD, the mark left
     behind when non-UTF-8 bytes get force-converted and lossily replaced)
     does not appear anywhere in the file.
  3. g_msgJA and g_msgEN have the same number of entries.
  4. Neither message table contains an empty string.
  5. Every message in g_msgJA/g_msgEN is checked against the screen-cell
     limit that applies to how SUMIRE.C actually uses it, in both languages.
     Screen-cell width follows the same rule as SUMIRE.C's own text_width():
     a lead byte in 0x81-0x9F or 0xE0-0xFC (and the byte after it) counts as
     2 cells, anything else counts as 1. Each message index is classified
     (see MSG_LIMITS below) into one of:
       - "box"    : placed alone into a header-box row (box_row()), must
                    fit within BOX_WIDTH cells.
       - "dialog" : placed alone into a draw_dialog() or draw_input_box()
                    box, must fit within DIALOG_WIDTH cells.
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
     BOX_WIDTH and DIALOG_WIDTH are both read out of SUMIRE.C's #define
     lines, never hard-coded here.
  6. The bottom row (g_fkeyLabel[]/g_fkeyCol[] - since milestone 6 this is
     the measured PC-98 function-key assignment, no longer a sentence-like
     command line at all, see the module note below):
       - g_fkeyLabel[], g_fkeyCol[] and g_fkeyHiPos[] all have FKEY_COUNT
         entries (FKEY_COUNT is read out of SUMIRE.C's #define, never
         hard-coded here).
       - every g_fkeyLabel[i] fits within FKEY_FIELD_WIDTH screen cells
         (same text_width()-style rule as check 5; these labels happen to
         be plain ASCII so the 2-cells-per-SJIS-char rule never actually
         triggers, but the same measurement code is reused rather than
         assuming that).
       - every field (g_fkeyCol[i] .. g_fkeyCol[i]+FKEY_FIELD_WIDTH-1)
         stays inside the 80-column screen and does not overlap the next
         field.
       - g_fkeyHiPos[i] is either -1 (only valid when g_fkeyLabel[i] is
         empty) or a valid index into g_fkeyLabel[i].

Milestone 6 note: the milestone-4/5 bilingual-independent command line
(g_cmdWords[]/g_cmdHiPos[]/cmdline_put_word(), one sentence-like row of
English command words) has been replaced by g_fkeyLabel[]/g_fkeyCol[]/
g_fkeyHiPos[] - ten fixed-position fields matching the real product's
function-key row, measured off real hardware (see
docs/filer-measure-05.md). It is plain C data, not part of g_msgJA/
g_msgEN, so it is checked separately (check 6) rather than through
MSG_LIMITS/extract_array.

Usage:
  python3 check.py [path-to-SUMIRE.C]
"""
import re
import sys
import os


# Classification of every g_msgJA/g_msgEN index, by the #define name used
# as its index in SUMIRE.C (see the MSG_* #defines near the top of
# SUMIRE.C). Keep this in sync with SUMIRE.C if messages are added, removed,
# or their call sites change how they're assembled onto the screen.
#
#   "box"      -> checked against BOX_WIDTH
#   "dialog"   -> checked against DIALOG_WIDTH
#   "fragment" -> not length-checked (see module docstring); listed only
MSG_LIMITS = {
    "MSG_TITLE":                ("box_title", 0),
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
    "MSG_MARKED_LABEL":         ("fragment", 12),
    "MSG_DEL_CONFIRM_ONE_PRE":  ("fragment", 13),
    "MSG_DEL_CONFIRM_ONE_SUF":  ("fragment", 14),
    "MSG_DEL_CONFIRM_MARK_PRE": ("fragment", 15),
    "MSG_DEL_CONFIRM_MARK_SUF": ("fragment", 16),
    "MSG_DEL_ERR_ISDIR":        ("dialog",   17),
    "MSG_DEL_ERR_FAILED":       ("dialog",   18),
    "MSG_RENAME_PROMPT":        ("fragment", 19),
    "MSG_RENAME_ERR_EMPTY":     ("dialog",   20),
    "MSG_RENAME_ERR_FAILED":    ("dialog",   21),
    "MSG_MKDIR_PROMPT":         ("fragment", 22),
    "MSG_MKDIR_ERR_EMPTY":      ("dialog",   23),
    "MSG_MKDIR_ERR_FAILED":     ("dialog",   24),
    "MSG_CM_ERR_HASDIR":        ("dialog",   25),
    "MSG_COPY_PROMPT":          ("fragment", 26),
    "MSG_MOVE_PROMPT":          ("fragment", 27),
    "MSG_CM_ERR_EMPTY":         ("dialog",   28),
    "MSG_COPY_ERR_PRE":         ("fragment", 29),
    "MSG_MOVE_ERR_PRE":         ("fragment", 30),
    "MSG_OVERWRITE_PRE":        ("fragment", 31),
    "MSG_OVERWRITE_SUF":        ("fragment", 32),
    "MSG_COPY_DONE_PRE":        ("fragment", 33),
    "MSG_COPY_DONE_SUF":        ("fragment", 34),
    "MSG_MOVE_DONE_PRE":        ("fragment", 35),
    "MSG_MOVE_DONE_SUF":        ("fragment", 36),
    "MSG_EXEC_ERR_NOTEXE":      ("dialog",   37),
    "MSG_EXEC_ERR_FAILED":      ("dialog",   38),
    "MSG_EXEC_PRESS_KEY":       ("dialog",   39),
}


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def cell_width(raw_bytes):
    """screen-cell width of a CP932-encoded byte string, using the same
    lead-byte rule as SUMIRE.C's text_width()."""
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
    '[const ]char *name[] = { ... };'. SUMIRE.C's message tables (and,
    since milestone 4, g_cmdWords[]) use plain literals with no escaped
    quotes, so a straightforward literal scan is enough (no need for a
    full C string-literal parser)."""
    m = re.search(r"(?:const\s+)?char \*" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if m is None:
        fail("could not find array %s[] in SUMIRE.C" % name)
    body = m.group(1)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def extract_define_int(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+(\d+)", text)
    if m is None:
        fail("could not find #define %s in SUMIRE.C" % name)
    return int(m.group(1))


def extract_int_array(text, name):
    """pulls the (bare, non-negative-or-negative decimal) integer literals
    out of 'int name[...][] = { ... };' - used for g_fkeyCol[]/
    g_fkeyHiPos[], which (unlike g_msgJA/g_msgEN/g_fkeyLabel) hold ints,
    not string literals."""
    m = re.search(r"int " + re.escape(name) + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if m is None:
        fail("could not find array %s[] in SUMIRE.C" % name)
    body = m.group(1)
    return [int(x) for x in re.findall(r"-?\d+", body)]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "SUMIRE.C")

    with open(path, "rb") as f:
        data = f.read()

    # ---- check 1: CP932-decodable -----------------------------------
    try:
        text = data.decode("cp932")
    except UnicodeDecodeError as e:
        fail("SUMIRE.C is not valid CP932: %s" % e)
    print("PASS: 1) SUMIRE.C decodes as CP932")

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
    # MSG_TITLE (row 0) shares its row with a right-hand clock field
    # ("YY-MM-DD HH:MM:SS", see draw_title_row() in SUMIRE.C), so its real
    # on-screen budget is BOX_WIDTH minus that field, its gap, the 2-cell
    # tail (space + border-line cell) between the clock and the top-right
    # corner, and the title's own "--"/" "/" " decoration - all read out
    # of SUMIRE.C's #defines, never hardcoded here, so this tracks
    # draw_title_row()'s own arithmetic (used = TITLE_DECOR_WIDTH +
    # titleCells; fillCells = BOX_WIDTH - used - TITLE_DATETIME_GAP -
    # DATETIME_WIDTH - TITLE_TAIL_WIDTH) automatically.
    datetime_width = extract_define_int(text, "DATETIME_WIDTH")
    title_datetime_gap = extract_define_int(text, "TITLE_DATETIME_GAP")
    title_decor_width = extract_define_int(text, "TITLE_DECOR_WIDTH")
    title_tail_width = extract_define_int(text, "TITLE_TAIL_WIDTH")
    title_width = (box_width - datetime_width - title_datetime_gap
                   - title_decor_width - title_tail_width)
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
        "box_title": title_width,
        "dialog": dialog_width,
    }
    limit_names = {
        "box": "BOX_WIDTH",
        "box_title": "the title's clock-adjusted width (BOX_WIDTH - "
                      "DATETIME_WIDTH - TITLE_DATETIME_GAP - "
                      "TITLE_DECOR_WIDTH - TITLE_TAIL_WIDTH)",
        "dialog": "DIALOG_WIDTH",
    }
    n = len(ja)
    known_indices = set(idx for (_kind, idx) in MSG_LIMITS.values())
    if known_indices != set(range(n)):
        missing = sorted(set(range(n)) - known_indices)
        extra = sorted(known_indices - set(range(n)))
        fail("MSG_LIMITS in check.py is out of sync with SUMIRE.C's message "
             "table (%d entries): missing indices %r, unknown indices %r - "
             "update MSG_LIMITS to match SUMIRE.C's MSG_* #defines and how "
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
                     (label, name, idx, w, limit_names[kind], limit, s))
        ja_w = cell_width(ja[idx].encode("cp932"))
        en_w = cell_width(en[idx].encode("cp932"))
        report.append("  [%2d] %-26s %-8s limit=%3d  JA=%3d  EN=%3d" %
                       (idx, name, kind, limit, ja_w, en_w))

    print("PASS: 5) every message fits the cell limit implied by its use "
          "site, in both languages")
    print("     (box=%d box_title=%d dialog=%d cells)" %
          (box_width, title_width, dialog_width))
    for line in report:
        print(line)

    # ---- check 6: bottom row (g_fkeyLabel[]/g_fkeyCol[]/g_fkeyHiPos[]) ---
    # ten fixed-position function-key fields, measured off real hardware
    # (see docs/filer-measure-05.md) - not a sentence-like line any more,
    # so this checks field placement/width instead of an assembled string.
    fkey_count = extract_define_int(text, "FKEY_COUNT")
    fkey_field_width = extract_define_int(text, "FKEY_FIELD_WIDTH")
    fkey_labels = extract_array(text, "g_fkeyLabel")
    fkey_cols = extract_int_array(text, "g_fkeyCol")
    fkey_hipos = extract_int_array(text, "g_fkeyHiPos")

    for name, arr in (("g_fkeyLabel", fkey_labels), ("g_fkeyCol", fkey_cols),
                       ("g_fkeyHiPos", fkey_hipos)):
        if len(arr) != fkey_count:
            fail("%s[] has %d entries, expected FKEY_COUNT (%d)" %
                 (name, len(arr), fkey_count))

    screen_cols = extract_define_int(text, "VRAM_COLS")

    report = []
    prev_end = -1
    for i in range(fkey_count):
        label = fkey_labels[i]
        col = fkey_cols[i]
        hipos = fkey_hipos[i]
        w = cell_width(label.encode("cp932"))

        if w > fkey_field_width:
            fail("g_fkeyLabel[%d] (%r) is %d cells wide, exceeds "
                 "FKEY_FIELD_WIDTH (%d cells)" % (i, label, w, fkey_field_width))
        if col < 0 or col + fkey_field_width > screen_cols:
            fail("g_fkeyCol[%d] (%d) places F%d's field outside the "
                 "%d-column screen (field is %d cells wide)" %
                 (i, col, i + 1, screen_cols, fkey_field_width))
        if col < prev_end:
            fail("g_fkeyCol[%d] (%d) overlaps the previous field, which "
                 "ends at column %d" % (i, col, prev_end))
        prev_end = col + fkey_field_width
        if label == "":
            if hipos != -1:
                fail("g_fkeyHiPos[%d] is %d but g_fkeyLabel[%d] is empty "
                     "(unassigned key slots must use hiPos -1)" % (i, hipos, i))
        else:
            if hipos < 0 or hipos >= len(label):
                fail("g_fkeyHiPos[%d] (%d) is not a valid index into "
                     "g_fkeyLabel[%d] (%r)" % (i, hipos, i, label))
        report.append("  F%-2d col=%2d width<=%d  %r" %
                       (i + 1, col, fkey_field_width, label))

    print("PASS: 6) all %d function-key fields fit FKEY_FIELD_WIDTH (%d) "
          "cells and stay inside the %d-column screen without overlapping" %
          (fkey_count, fkey_field_width, screen_cols))
    for line in report:
        print(line)

    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
