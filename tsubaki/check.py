#!/usr/bin/env python3
"""
check.py - integrity checks for TSUBAKI.C

TSUBAKI.C is encoded as CP932 (Shift_JIS) and contains raw Japanese bytes in
its message table. This script must never itself convert the file's
encoding: it reads TSUBAKI.C as bytes and only decodes to text for parsing,
never writes the file back. Modeled on guest/sumire/check.py.

Checks (all must pass, otherwise this exits non-zero):
  1. TSUBAKI.C decodes as CP932 without error.
  2. The byte sequence EF BF BD (the UTF-8 encoding of U+FFFD, the mark left
     behind when non-UTF-8 bytes get force-converted and lossily replaced)
     does not appear anywhere in the file.
  3. g_msgJA and g_msgEN have the same number of entries.
  4. Neither message table contains an empty string.
  5. g_title (the status line's leftmost product-name field, fixed
     English text kept out of g_msgJA/g_msgEN - see its comment in
     TSUBAKI.C, right above g_fkeyCol[]) fits STATUS_TITLE_WIDTH cells.
  6. Every message in g_msgJA/g_msgEN is checked against the screen-cell
     limit that applies to how TSUBAKI.C actually uses it, in both
     languages. Screen-cell width follows the same rule as TSUBAKI.C's own
     text_width(): a lead byte in 0x81-0x9F or 0xE0-0xFC (and the byte
     after it) counts as 2 cells, anything else counts as 1. Each message
     index is classified (see MSG_LIMITS below) into one of:
       - "untitled": placed alone into the status line's file-name field
                     (draw_status_line()) when no file is loaded, must fit
                     within STATUS_FILE_WIDTH-1 cells (1 cell reserved for
                     the '*' modified-marker that draw_status_line() may
                     append right after it).
       - "mode"    : placed alone inside the status line's "[...]" mode
                     indicator (draw_status_line()), must fit within a
                     small fixed budget (MODE_WIDTH below) - generous
                     enough for "INS"/"OVR"/"挿入"/"上書き" with room to
                     spare, but this is still checked rather than assumed.
       - "dialog"  : placed alone into a draw_dialog() or draw_input_box()
                     box, must fit within DIALOG_WIDTH cells.
       - "notice"  : placed into the status line's notice area
                     (draw_status_line(), between the file-name field and
                     the right-hand line:col/mode field), must fit within
                     STATUS_MID_WIDTH cells.
     STATUS_FILE_WIDTH, STATUS_MID_WIDTH and DIALOG_WIDTH are all read out
     of TSUBAKI.C's #define lines, never hard-coded here.
  7. The bottom row (g_fkeyLabel[]/g_fkeyCol[]/g_fkeyHiPos[] - ten
     fixed-position function-key fields, the same convention as
     guest/sumire/SUMIRE.C's F-key row):
       - g_fkeyLabel[], g_fkeyCol[] and g_fkeyHiPos[] all have FKEY_COUNT
         entries (FKEY_COUNT is read out of TSUBAKI.C's #define, never
         hard-coded here).
       - every g_fkeyLabel[i] fits within FKEY_FIELD_WIDTH screen cells
         (same text_width()-style rule as check 5/6; these labels happen
         to be plain ASCII so the 2-cells-per-SJIS-char rule never
         actually triggers, but the same measurement code is reused
         rather than assuming that).
       - every field (g_fkeyCol[i] .. g_fkeyCol[i]+FKEY_FIELD_WIDTH-1)
         stays inside the 80-column screen and does not overlap the next
         field.
       - g_fkeyHiPos[i] is either -1 (only valid when g_fkeyLabel[i] is
         empty) or a valid index into g_fkeyLabel[i].
  8. The status line (row 0) field widths - STATUS_TITLE_WIDTH,
     STATUS_SEP_WIDTH, STATUS_FILE_WIDTH, STATUS_MID_WIDTH and
     STATUS_RIGHT_WIDTH, all read out of TSUBAKI.C's #define lines, never
     hard-coded here - sum to exactly VRAM_COLS (80), so the reverse-video
     status band covers the row with no gap and no overrun.
  9. BODY_TOP + BODY_ROWS does not reach ROW_FKEY, so the scrollable body
     area never overlaps the bottom function-key row.

Usage:
  python3 check.py [path-to-TSUBAKI.C]
"""
import re
import sys
import os


# Classification of every g_msgJA/g_msgEN index, by the #define name used
# as its index in TSUBAKI.C (see the MSG_* #defines near the message
# table). Keep this in sync with TSUBAKI.C if messages are added, removed,
# or their call sites change how they're assembled onto the screen.
MSG_LIMITS = {
    "MSG_UNTITLED":            ("untitled", 0),
    "MSG_MODE_INS":            ("mode",     1),
    "MSG_MODE_OVR":            ("mode",     2),
    "MSG_GOTO_PROMPT":         ("dialog",   3),
    "MSG_SAVEAS_PROMPT":       ("dialog",   4),
    "MSG_QUIT_CONFIRM":        ("dialog",   5),
    "MSG_SAVE_FAIL":           ("dialog",   6),
    "MSG_LOAD_TOO_LARGE":      ("dialog",   7),
    "MSG_LOAD_TOO_MANY_LINES": ("dialog",   8),
    "MSG_LIMIT_NOTICE":        ("notice",   9),
}

# Generous fixed budget for the status line's "[...]" mode indicator (see
# draw_status_line() in TSUBAKI.C: "[" + MSG_MODE_INS/MSG_MODE_OVR + "]").
# Not derived from a #define because it isn't a screen-layout constant on
# its own - it is just how much of STATUS_RIGHT_WIDTH the mode text may
# reasonably claim, well short of crowding out the line:col/TAB fields it
# shares the field with.
MODE_WIDTH = 10


def fail(msg):
    print("FAIL: " + msg)
    sys.exit(1)


def cell_width(raw_bytes):
    """screen-cell width of a CP932-encoded byte string, using the same
    lead-byte rule as TSUBAKI.C's text_width()."""
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
    '[const ]char *name[] = { ... };'. TSUBAKI.C's message table (and
    g_fkeyLabel[]) uses plain literals with no escaped quotes, so a
    straightforward literal scan is enough."""
    m = re.search(r"(?:const\s+)?char \*" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if m is None:
        fail("could not find array %s[] in TSUBAKI.C" % name)
    body = m.group(1)
    return re.findall(r'"((?:[^"\\]|\\.)*)"', body)


def extract_define_int(text, name):
    m = re.search(r"#define\s+" + re.escape(name) + r"\s+\(?(-?\d+)", text)
    if m is None:
        fail("could not find #define %s in TSUBAKI.C" % name)
    return int(m.group(1))


def extract_char_var(text, name):
    """pulls the double-quoted string literal out of
    'char *name = "...";' - used for g_title, a single language-
    independent string, unlike g_msgJA/g_msgEN's arrays."""
    m = re.search(r"char \*" + re.escape(name) + r'\s*=\s*"((?:[^"\\]|\\.)*)"\s*;', text)
    if m is None:
        fail("could not find variable %s in TSUBAKI.C" % name)
    return m.group(1)


def extract_int_array(text, name):
    """pulls the (bare, non-negative-or-negative decimal) integer literals
    out of 'int name[...][] = { ... };' - used for g_fkeyCol[]/
    g_fkeyHiPos[], which hold ints, not string literals."""
    m = re.search(r"int " + re.escape(name) + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", text, re.S)
    if m is None:
        fail("could not find array %s[] in TSUBAKI.C" % name)
    body = m.group(1)
    return [int(x) for x in re.findall(r"-?\d+", body)]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "TSUBAKI.C")

    with open(path, "rb") as f:
        data = f.read()

    # ---- check 1: CP932-decodable -----------------------------------
    try:
        text = data.decode("cp932")
    except UnicodeDecodeError as e:
        fail("TSUBAKI.C is not valid CP932: %s" % e)
    print("PASS: 1) TSUBAKI.C decodes as CP932")

    # ---- check 2: no U+FFFD replacement-character bytes --------------
    fffd_count = data.count(b"\xef\xbf\xbd")
    if fffd_count != 0:
        fail("found %d occurrence(s) of EF BF BD (U+FFFD) - the file was "
             "likely round-tripped through a lossy UTF-8 conversion" % fffd_count)
    print("PASS: 2) no EF BF BD (U+FFFD) bytes in the file")

    # ---- extract the screen-cell width limits and both message tables -
    dialog_width = extract_define_int(text, "DIALOG_WIDTH")
    status_title_width = extract_define_int(text, "STATUS_TITLE_WIDTH")
    status_sep_width = extract_define_int(text, "STATUS_SEP_WIDTH")
    status_file_width = extract_define_int(text, "STATUS_FILE_WIDTH")
    status_mid_width = extract_define_int(text, "STATUS_MID_WIDTH")
    status_right_width = extract_define_int(text, "STATUS_RIGHT_WIDTH")
    ja = extract_array(text, "g_msgJA")
    en = extract_array(text, "g_msgEN")
    title = extract_char_var(text, "g_title")

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

    # ---- check 5: g_title fits the status line's title-field budget ---
    if title == "":
        fail("g_title is an empty string")
    title_w = cell_width(title.encode("cp932"))
    if title_w > status_title_width:
        fail("g_title is %d cells wide, exceeds STATUS_TITLE_WIDTH (%d cells) - %r" %
             (title_w, status_title_width, title))
    print("PASS: 5) g_title (%r) fits STATUS_TITLE_WIDTH (limit=%d cells, actual=%d cells)" %
          (title, status_title_width, title_w))

    # ---- check 6: every message fits the cell limit its use site implies --
    limits = {
        # STATUS_FILE_WIDTH minus 1 cell reserved for the '*' modified
        # marker draw_status_line() may append right after the name.
        "untitled": status_file_width - 1,
        "mode": MODE_WIDTH,
        "dialog": dialog_width,
        "notice": status_mid_width,
    }
    limit_names = {
        "untitled": "STATUS_FILE_WIDTH-1 (status-line file-name field, minus the '*' marker)",
        "mode": "MODE_WIDTH (status-line mode indicator budget)",
        "dialog": "DIALOG_WIDTH",
        "notice": "STATUS_MID_WIDTH (status-line notice area)",
    }
    n = len(ja)
    known_indices = set(idx for (_kind, idx) in MSG_LIMITS.values())
    if known_indices != set(range(n)):
        missing = sorted(set(range(n)) - known_indices)
        extra = sorted(known_indices - set(range(n)))
        fail("MSG_LIMITS in check.py is out of sync with TSUBAKI.C's message "
             "table (%d entries): missing indices %r, unknown indices %r - "
             "update MSG_LIMITS to match TSUBAKI.C's MSG_* #defines and how "
             "each one is used" % (n, missing, extra))

    report = []
    for name in sorted(MSG_LIMITS, key=lambda k: MSG_LIMITS[k][1]):
        kind, idx = MSG_LIMITS[name]
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
        report.append("  [%2d] %-26s %-9s limit=%3d  JA=%3d  EN=%3d" %
                       (idx, name, kind, limit, ja_w, en_w))

    print("PASS: 6) every message fits the cell limit implied by its use "
          "site, in both languages")
    print("     (untitled=%d mode=%d dialog=%d notice=%d cells)" %
          (limits["untitled"], limits["mode"], limits["dialog"], limits["notice"]))
    for line in report:
        print(line)

    # ---- check 7: bottom row (g_fkeyLabel[]/g_fkeyCol[]/g_fkeyHiPos[]) ---
    fkey_count = extract_define_int(text, "FKEY_COUNT")
    fkey_field_width = extract_define_int(text, "FKEY_FIELD_WIDTH")
    fkey_labels = extract_array(text, "g_fkeyLabel")
    fkey_cols = extract_int_array(text, "g_fkeyCol")
    fkey_hipos = extract_int_array(text, "g_fkeyHiPos")
    screen_cols = extract_define_int(text, "VRAM_COLS")

    for name, arr in (("g_fkeyLabel", fkey_labels), ("g_fkeyCol", fkey_cols),
                       ("g_fkeyHiPos", fkey_hipos)):
        if len(arr) != fkey_count:
            fail("%s[] has %d entries, expected FKEY_COUNT (%d)" %
                 (name, len(arr), fkey_count))

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

    print("PASS: 7) all %d function-key fields fit FKEY_FIELD_WIDTH (%d) "
          "cells and stay inside the %d-column screen without overlapping" %
          (fkey_count, fkey_field_width, screen_cols))
    for line in report:
        print(line)

    # ---- check 8: status-line field widths sum to exactly VRAM_COLS ---
    status_total = (status_title_width + status_sep_width + status_file_width +
                    status_mid_width + status_right_width)
    if status_total != screen_cols:
        fail("status line field widths sum to %d cells (title=%d + sep=%d + "
             "file=%d + mid=%d + right=%d), expected exactly VRAM_COLS (%d)" %
             (status_total, status_title_width, status_sep_width, status_file_width,
              status_mid_width, status_right_width, screen_cols))
    print("PASS: 8) status line field widths sum to exactly VRAM_COLS "
          "(title=%d + sep=%d + file=%d + mid=%d + right=%d = %d)" %
          (status_title_width, status_sep_width, status_file_width,
           status_mid_width, status_right_width, screen_cols))

    # ---- check 9: body area does not overlap the function-key row -----
    body_top = extract_define_int(text, "BODY_TOP")
    body_rows = extract_define_int(text, "BODY_ROWS")
    row_fkey = extract_define_int(text, "ROW_FKEY")
    if body_top + body_rows > row_fkey:
        fail("BODY_TOP (%d) + BODY_ROWS (%d) = %d overlaps ROW_FKEY (%d)" %
             (body_top, body_rows, body_top + body_rows, row_fkey))
    print("PASS: 9) BODY_TOP (%d) + BODY_ROWS (%d) = %d does not reach "
          "ROW_FKEY (%d)" % (body_top, body_rows, body_top + body_rows, row_fkey))

    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
