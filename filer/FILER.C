/*
 * FILER.C - PC-98 / FreeDOS(98) directory browser (milestone 2)
 *
 * Independent, from-scratch implementation. See README.md in this
 * directory for the independence declaration. All on-screen text below
 * is original wording written for this project; it does not reproduce
 * any text from any existing product.
 *
 * Milestone 1 scope: directory listing, cursor movement, quit only.
 * Milestone 2 adds: marking files (SPACE/TAB/HOME), moving into/out of
 * a directory (Enter, "." and ".." shown like the original), and
 * deleting the marked files (or the file under the cursor when nothing
 * is marked) with a confirm dialog. Directories are never marked and
 * are never deleted by this command. Copy/Move/Rename/mkdir are still
 * not implemented.
 *
 * Built with SmallerC (C89-ish subset, 16-bit small-model MZ EXE).
 * Screen I/O uses ANSI-style escape sequences (measured against
 * WebNP2/FreeDOS(98); see docs/escape-measure-01.md). Directory access
 * uses plain DOS INT 21h services (AH=1Ah/4Eh/4Fh/47h/19h/36h), which
 * are documented, generic DOS APIs and not derived from any particular
 * program's source code.
 *
 * Output is written directly to DOS via INT 21h AH=40h (handle 1),
 * not through stdio: a full-screen redraw is assembled into an in
 * memory buffer and flushed with a single write. stdio's line
 * buffering never flushes here because the screen has no newlines,
 * so printf()/puts()/putchar() must not be used for screen output.
 *
 * Screen text is bilingual (Japanese / English); see the message
 * table below. Select with the /J (Japanese, default) or /E (English)
 * command line switch.
 */

#include <string.h>

/* ---- constants ------------------------------------------------------ */

#define MAX_ENTRIES   1024
#define NAME_LEN      13     /* 8.3 name + dot + NUL, as returned by DOS */
#define LEFT_ROWS     17
#define VISIBLE_MAX   34     /* 17 rows x 2 columns; see README for the
                                 milestone-1 "no paging yet" limitation */

/* ---- cursor movement directions (used by move_cursor) ---------------- */
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* ---- key codes returned by DOS INT 21h AH=08h (console input, no echo)
 * Measured on real DOS/PC-98 with a probe program: unlike the IBM PC
 * convention of a 0x00 prefix byte followed by an extended scan code,
 * PC-98 DOS returns arrow keys as single control codes. The old
 * "if (key == 0) key2 = dos_getch();" two-byte read never saw its
 * prefix byte and so the arrow keys did nothing.
 * ------------------------------------------------------------------- */
#define KEY_UP     0x0b
#define KEY_DOWN   0x0a
#define KEY_LEFT   0x08   /* same code as BS; treated as left in the list */
#define KEY_RIGHT  0x0c
#define KEY_HOME   0x1a
#define KEY_ESC    0x1b
#define KEY_TAB    0x09
#define KEY_SPACE  0x20
#define KEY_ENTER  0x0d
/* WordStar-style alternates, up/down only. ^S (0x13, "left" in the
 * WordStar scheme) must NOT be bound here: real DOS's console driver
 * treats ^S as XOFF and freezes screen output until ^Q is pressed
 * (confirmed on real hardware). A BIOS/VRAM-direct program can use ^S
 * safely; this program goes through the DOS console API (AH=08h/40h),
 * so ^S is not usable. ^D is left unbound too: unlike ^E/^X it has not
 * been confirmed not to collide with anything. */
#define KEY_CTRL_E 0x05   /* alternate for up */
#define KEY_CTRL_X 0x18   /* alternate for down */

#define ATTR_RDONLY   0x01
#define ATTR_HIDDEN   0x02
#define ATTR_SYSTEM   0x04
#define ATTR_VOLLABEL 0x08
#define ATTR_DIR      0x10
#define ATTR_ARCHIVE  0x20

#define ROW_TITLE     0
#define ROW_DISK      1
#define ROW_SEP1      2
#define ROW_PATH      3
#define ROW_INFO      4
#define ROW_SEP2      5
#define ROW_LIST_TOP  6
#define ROW_CMD       24

#define CMDLINE_WIDTH 79  /* the console can scroll if col 80 (0-based
                              79) on the bottom row is written; see
                              draw_screen_frame()'s cmdline truncation */

#define COL_LEFT      0
#define COL_RIGHT     40

#define SCRBUF_SIZE   8192

/* ---- header box (rows 0-5) drawn with full-width box-drawing chars ---
 * Measured against the real console: the DOS-console text output path
 * treats bytes 0x81-0x9F as Shift_JIS lead bytes, so the half-width
 * single-line box characters (0x9C-0x9F etc.) cannot be sent through it
 * (see docs/ for the measurement). Full-width box-drawing characters
 * (each 2 screen cells) are used instead; all width math below is in
 * screen cells via text_width(), never byte counts or strlen().
 * ------------------------------------------------------------------- */
#define BOX_WIDTH     76   /* interior width in cells, between the borders */

#define BOXCH_TL      "\x84\xa1"  /* topleft corner  */
#define BOXCH_TR      "\x84\xa2"  /* topright corner */
#define BOXCH_BL      "\x84\xa4"  /* bottomleft corner  */
#define BOXCH_BR      "\x84\xa3"  /* bottomright corner */
#define BOXCH_H       "\x84\x9f"  /* horizontal line */
#define BOXCH_V       "\x84\xa0"  /* vertical line   */
#define BOXCH_LT      "\x84\xa5"  /* left T (mid separator, left end)  */
#define BOXCH_RT      "\x84\xa7"  /* right T (mid separator, right end) */

/* ---- language / message table ----------------------------------------
 * All on-screen text is collected here and looked up by index; no
 * literal UI text is written directly at the call site. See
 * docs/i18n-design.md for the design rationale.
 * ------------------------------------------------------------------- */

#define LANG_JA 0
#define LANG_EN 1

#define MSG_TITLE         0
#define MSG_PATH_PREFIX   1
#define MSG_TRUNC_PREFIX  2
#define MSG_TRUNC_SUFFIX  3
#define MSG_INFO_PREFIX   4
#define MSG_INFO_EMPTY    5
#define MSG_DISK_UNAVAIL  6
#define MSG_DISK_TOTAL    7
#define MSG_DISK_USED     8
#define MSG_DISK_FREE     9
#define MSG_BYTES_SUFFIX  10
#define MSG_ATTR_LABEL    11
#define MSG_CMDLINE       12
#define MSG_MARKED_LABEL       13
#define MSG_DEL_CONFIRM_ONE_PRE   14
#define MSG_DEL_CONFIRM_ONE_SUF   15
#define MSG_DEL_CONFIRM_MARK_PRE  16
#define MSG_DEL_CONFIRM_MARK_SUF  17
#define MSG_DEL_ERR_ISDIR      18
#define MSG_DEL_ERR_FAILED     19

const char *g_msgJA[] = {
  "PC98Guest ファイラ - ディレクトリビューア（マイルストーン2・開発中）",
  "パス=",
  "　（※上限",
  "件を超えたため一覧を打ち切りました ※）",
  "情報:",
  "（空）",
  "ディスク:（取得できません）",
  "合計:",
  "使用:",
  "空き:",
  "バイト",
  "属性:",
  "矢印:移動　SPACE/TAB:マーク　HOME:全マーク切替　Enter:移動　D:削除　Q/ESC:終了",
  "マーク:",
  "このファイルを削除しますか: ",
  " (Y/N)",
  "マークした ",
  " 件を削除します (Y/N)",
  "ディレクトリは削除できません（何かキーを押してください）",
  "削除に失敗しました（読み取り専用？）キーを押してください"
};

const char *g_msgEN[] = {
  "PC98Guest Filer - directory viewer (milestone 2, work in progress)",
  "Path=",
  "   (** over ",
  " entries, list truncated **)",
  "Info:",
  "(empty)",
  "Disk: (unavailable)",
  "Total:",
  "Used:",
  "Free:",
  " bytes",
  "Attr:",
  "Arrows:move SPACE/TAB:mark HOME:toggle all Enter:open D:delete Q/ESC:quit",
  "Marked:",
  "Delete this file: ",
  " (Y/N)",
  "Delete the ",
  " marked file(s)? (Y/N)",
  "Cannot delete a directory (press any key)",
  "Delete failed (read-only?) - press any key"
};

const char **g_msgTables[2] = { g_msgJA, g_msgEN };

int g_lang = LANG_JA;

#define MSG(id) (g_msgTables[g_lang][id])

/* checks that both language tables have the same number of entries and
   that no entry is an empty string; returns 1 if OK, 0 if broken */
int msg_selftest(void)
{
  int nJA;
  int nEN;
  int i;

  nJA = sizeof(g_msgJA) / sizeof(g_msgJA[0]);
  nEN = sizeof(g_msgEN) / sizeof(g_msgEN[0]);
  if (nJA != nEN) return 0;

  for (i = 0; i < nJA; i++) {
    if (g_msgJA[i] == 0 || g_msgJA[i][0] == 0) return 0;
    if (g_msgEN[i] == 0 || g_msgEN[i][0] == 0) return 0;
  }
  return 1;
}

/* ---- global state ------------------------------------------------------ */

char g_name[MAX_ENTRIES * NAME_LEN];
unsigned char g_attr[MAX_ENTRIES];
unsigned int g_time[MAX_ENTRIES];
unsigned int g_date[MAX_ENTRIES];
unsigned int g_sizeLo[MAX_ENTRIES];
unsigned int g_sizeHi[MAX_ENTRIES];
unsigned char g_marked[MAX_ENTRIES]; /* 1 = marked; directories are never
                                         marked (see mark_cursor()) */

int g_count;        /* number of entries actually stored (<= MAX_ENTRIES) */
int g_truncated;     /* 1 if the directory had more than MAX_ENTRIES files */
int g_cursor;         /* index into the visible list (0 .. visibleCount-1) */

char g_path[80];      /* current directory, always ends with '\' */
char g_search[88];    /* g_path + "*.*" */
unsigned char g_dta[43];

char g_scrbuf[SCRBUF_SIZE]; /* one full-screen frame, flushed in one write */
unsigned int g_scrlen;

/* ---- low level DOS calls (inline asm; see doc/smlrc.md "asm()") ------- */

int dos_getch(void)
{
  asm("mov ah, 8\n"
      "int 0x21\n"
      "mov ah, 0");
}

void dos_setdta(unsigned char *dta)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x1a\n"
      "int 0x21");
}

int dos_findfirst(char *path, unsigned int attr)
{
  asm("mov dx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov ah, 0x4e\n"
      "int 0x21\n"
      "sbb ax, ax");
}

int dos_findnext(void)
{
  asm("mov ah, 0x4f\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* delete a file (INT 21h AH=41h, DS:DX = ASCIZ path). Returns 0 on
   success, nonzero on failure (e.g. read-only, in use). Only ever called
   with a path built from a non-directory entry (see do_delete()); DOS
   itself would also refuse to unlink a directory this way, but we do not
   rely on that - the directory case is filtered out before this is ever
   called. */
int dos_delete(char *path)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x41\n"
      "int 0x21\n"
      "sbb ax, ax");
}

unsigned int dos_getdrive(void)
{
  asm("mov ah, 0x19\n"
      "int 0x21\n"
      "mov ah, 0");
}

int dos_getcwd(unsigned int drive, char *buf)
{
  asm("mov dl, [bp+4]\n"
      "mov si, [bp+6]\n"
      "mov ah, 0x47\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* AX = sectors/cluster (0xFFFF on error); *availClus, *bytesPerSec,
   *totalClus are filled from BX/CX/DX. */
unsigned int dos_diskfree(unsigned int drive, unsigned int *availClus,
                           unsigned int *bytesPerSec, unsigned int *totalClus)
{
  asm("mov dl, [bp+4]\n"
      "mov ah, 0x36\n"
      "int 0x21\n"
      "push ax\n"
      "mov si, [bp+6]\n"
      "mov [si], bx\n"
      "mov si, [bp+8]\n"
      "mov [si], cx\n"
      "mov si, [bp+10]\n"
      "mov [si], dx\n"
      "pop ax");
}

/* write len bytes starting at buf to stdout (handle 1) via INT 21h AH=40h.
   Used instead of stdio so screen writes are explicit, single-shot DOS
   calls rather than a line-buffered stream that never sees a newline. */
void dos_write(char *buf, unsigned int len)
{
  asm("mov dx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov bx, 1\n"
      "mov ah, 0x40\n"
      "int 0x21");
}

/* ---- 32-bit arithmetic helpers (SmallerC 16-bit mode has no long) ---- */

/* returns a*b (low word); *hiOut gets the high word */
unsigned int umul32(unsigned int a, unsigned int b, unsigned int *hiOut)
{
  asm("mov ax, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mul cx\n"
      "mov bx, [bp+8]\n"
      "mov [bx], dx");
}

/* (*hiP:*loP) /= 10, returns the remainder (0-9); updates *hiP/*loP in place */
unsigned int divmod10(unsigned int *hiP, unsigned int *loP)
{
  asm("mov bx, [bp+4]\n"
      "mov ax, [bx]\n"
      "xor dx, dx\n"
      "mov cx, 10\n"
      "div cx\n"
      "mov [bx], ax\n"
      "mov bx, [bp+6]\n"
      "mov ax, [bx]\n"
      "div cx\n"
      "mov [bx], ax\n"
      "mov ax, dx");
}

/* (hi:lo) * val16, truncated to 32 bits; returns low word, *outHi = high word */
unsigned int mul32x16(unsigned int hi, unsigned int lo, unsigned int val16,
                       unsigned int *outHi)
{
  unsigned int t1hi;
  unsigned int t1lo;
  unsigned int t2;

  t1lo = umul32(lo, val16, &t1hi);
  t2 = hi * val16; /* 16x16 unsigned multiply; high bits deliberately dropped */
  *outHi = t1hi + t2;
  return t1lo;
}

void sub32(unsigned int aHi, unsigned int aLo, unsigned int bHi,
           unsigned int bLo, unsigned int *outHi, unsigned int *outLo)
{
  unsigned int lo;
  unsigned int hi;
  unsigned int borrow;

  lo = aLo - bLo;
  borrow = (aLo < bLo) ? 1 : 0;
  hi = aHi - bHi - borrow;
  *outHi = hi;
  *outLo = lo;
}

/* formats a 32-bit unsigned value as decimal with ',' every 3 digits */
void format_u32(unsigned int hi, unsigned int lo, char *out)
{
  char digits[12];
  int n;
  int i;
  int j;
  unsigned int rem;

  n = 0;
  if (hi == 0 && lo == 0) {
    out[0] = '0';
    out[1] = 0;
    return;
  }
  while (hi != 0 || lo != 0) {
    rem = divmod10(&hi, &lo);
    digits[n] = (char)('0' + rem);
    n++;
  }
  j = 0;
  for (i = n - 1; i >= 0; i--) {
    out[j] = digits[i];
    j++;
    /* digits[] is stored least-significant-first (digits[0] = units), so
       index i is the distance of this digit from the units digit. A comma
       belongs right after this digit whenever it completes a group of 3
       counting from the units digit, i.e. i is a positive multiple of 3. */
    if (i > 0 && (i % 3) == 0) {
      out[j] = ',';
      j++;
    }
  }
  out[j] = 0;
}

/* same as format_u32() but without ',' separators; used for the plain
   (unseparated) file-list size column - see docs/, only the header totals
   use comma grouping there. */
void format_u32_plain(unsigned int hi, unsigned int lo, char *out)
{
  char digits[12];
  int n;
  int i;
  unsigned int rem;

  n = 0;
  if (hi == 0 && lo == 0) {
    out[0] = '0';
    out[1] = 0;
    return;
  }
  while (hi != 0 || lo != 0) {
    rem = divmod10(&hi, &lo);
    digits[n] = (char)('0' + rem);
    n++;
  }
  for (i = 0; i < n; i++) {
    out[i] = digits[n - 1 - i];
  }
  out[n] = 0;
}

/* ---- date/time formatting -------------------------------------------- */

void format_date(unsigned int d, char *out)
{
  int year;
  int month;
  int day;

  year = (80 + ((d >> 9) & 0x7F)) % 100;
  month = (d >> 5) & 0x0F;
  day = d & 0x1F;
  out[0] = (char)('0' + year / 10);
  out[1] = (char)('0' + year % 10);
  out[2] = '-';
  out[3] = (char)('0' + month / 10);
  out[4] = (char)('0' + month % 10);
  out[5] = '-';
  out[6] = (char)('0' + day / 10);
  out[7] = (char)('0' + day % 10);
  out[8] = 0;
}

void format_time(unsigned int t, char *out)
{
  int hour;
  int minute;

  hour = (t >> 11) & 0x1F;
  minute = (t >> 5) & 0x3F;
  out[0] = (hour / 10 == 0) ? ' ' : (char)('0' + hour / 10);
  out[1] = (char)('0' + hour % 10);
  out[2] = ':';
  out[3] = (char)('0' + minute / 10);
  out[4] = (char)('0' + minute % 10);
  out[5] = 0;
}

/* attribute string "R H S A", '_' where the bit is not set */
void format_attr(unsigned char attr, char *out)
{
  out[0] = (attr & ATTR_RDONLY) ? 'R' : '_';
  out[1] = ' ';
  out[2] = (attr & ATTR_HIDDEN) ? 'H' : '_';
  out[3] = ' ';
  out[4] = (attr & ATTR_SYSTEM) ? 'S' : '_';
  out[5] = ' ';
  out[6] = (attr & ATTR_ARCHIVE) ? 'A' : '_';
  out[7] = 0;
}

/* ---- small text helpers ------------------------------------------------ */

void put_str_n(char *buf, int col, char *s, int maxlen)
{
  int i;

  i = 0;
  while (s[i] != 0 && i < maxlen) {
    buf[col + i] = s[i];
    i++;
  }
}

/* display width in screen cells: SJIS lead bytes count as 2, everything
   else (ASCII / trail bytes are skipped along with their lead byte) as 1.
   All column alignment must go through this, not strlen(), so Japanese
   full-width text lines up the same way ASCII text does. */
int text_width(char *s)
{
  int w;
  int i;
  unsigned char c;

  w = 0;
  i = 0;
  while (s[i] != 0) {
    c = (unsigned char)s[i];
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
      if (s[i + 1] != 0) {
        w += 2;
        i += 2;
      } else {
        w += 1; /* truncated lead byte at end of string; count as 1 */
        i += 1;
      }
    } else {
      w += 1;
      i += 1;
    }
  }
  return w;
}

void right_justify(char *buf, int col, int width, char *s)
{
  int len;
  int pad;
  int i;

  len = text_width(s);
  if (len >= width) {
    put_str_n(buf, col, s + (len - width), width);
  } else {
    pad = width - len;
    for (i = 0; i < pad; i++) buf[col + i] = ' ';
    put_str_n(buf, col + pad, s, len);
  }
}

/* appends the NUL-terminated string s to dst at byte offset *lenp, then
   re-terminates dst; *lenp is advanced past the appended bytes. Used to
   assemble row content (label + number + label + ...) into a plain
   buffer before it is placed into a header box row.
   'cap' is the total size (in bytes, including room for the NUL) of the
   dst buffer as declared by the caller; sappend() never writes at or
   past dst[cap-1] other than the terminating NUL, and never writes only
   half of a 2-byte SJIS/box-drawing character - if the remaining room is
   not enough for both bytes of such a character, that character (and
   everything after it in s) is silently dropped and dst stays a valid,
   NUL-terminated C string. */
void sappend(char *dst, int *lenp, char *s, int cap)
{
  int i;
  int lp;
  unsigned char c;
  int chBytes;

  lp = *lenp;
  i = 0;
  while (s[i] != 0) {
    c = (unsigned char)s[i];
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
      chBytes = (s[i + 1] != 0) ? 2 : 1; /* truncated lead byte: treat as 1 */
    } else {
      chBytes = 1;
    }
    if (lp + chBytes > cap - 1) break; /* leave room for the NUL */
    dst[lp] = s[i];
    lp++;
    i++;
    if (chBytes == 2) {
      dst[lp] = s[i];
      lp++;
      i++;
    }
  }
  dst[lp] = 0;
  *lenp = lp;
}

/* same as sappend(), but for an unsigned int formatted as plain decimal;
   'cap' has the same meaning as in sappend(). Digits are ASCII (1 byte
   each), so no multi-byte character can ever be split here, but each
   digit is still checked against the remaining room before it is
   written. */
void sappend_uint(char *dst, int *lenp, unsigned int v, int cap)
{
  char tmp[6];
  int n;
  int lp;

  n = 0;
  if (v == 0) {
    sappend(dst, lenp, "0", cap);
    return;
  }
  while (v > 0) {
    tmp[n] = (char)('0' + (v % 10));
    v = v / 10;
    n++;
  }
  lp = *lenp;
  while (n > 0) {
    n--;
    if (lp + 1 > cap - 1) break;
    dst[lp] = tmp[n];
    lp++;
  }
  dst[lp] = 0;
  *lenp = lp;
}

/* copies s into buf starting at cell offset col, at most 'width' screen
   cells (as measured by text_width's SJIS-aware rules), silently
   truncating on the right without ever splitting a multi-byte character.
   Unlike put_str_n, the limit here is in cells, not bytes. Does not pad;
   caller must pre-fill buf with spaces for a fixed-width field. */
void put_str_cells(char *buf, int col, char *s, int width)
{
  int i;
  int cell;
  int outpos;
  unsigned char c;

  i = 0;
  cell = 0;
  outpos = col;
  while (s[i] != 0) {
    c = (unsigned char)s[i];
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
      if (s[i + 1] != 0) {
        if (cell + 2 > width) break;
        buf[outpos] = s[i];
        buf[outpos + 1] = s[i + 1];
        outpos += 2;
        cell += 2;
        i += 2;
      } else {
        if (cell + 1 > width) break;
        buf[outpos] = s[i];
        outpos++;
        cell++;
        i++;
      }
    } else {
      if (cell + 1 > width) break;
      buf[outpos] = s[i];
      outpos++;
      cell++;
      i++;
    }
  }
}

/* draws one row of the header box: left border char(s) + content
   (space-padded/truncated to BOX_WIDTH cells) + right border char(s).
   'content' may be shorter than BOX_WIDTH (padded with spaces) or exactly
   BOX_WIDTH (e.g. an all-dashes separator row); it is never split mid
   multi-byte character. */
void box_row(int row, char *lb, char *rb, char *content)
{
  char cell[BOX_WIDTH + 1];
  int i;

  for (i = 0; i < BOX_WIDTH; i++) cell[i] = ' ';
  cell[BOX_WIDTH] = 0;
  put_str_cells(cell, 0, content, BOX_WIDTH);

  buf_goto(row, 0);
  buf_puts(lb);
  buf_puts(cell);
  buf_puts(rb);
}

/* fills out[] with exactly BOX_WIDTH cells (2*BOX_WIDTH... no, BOX_WIDTH
   is even, so BOX_WIDTH bytes) of the horizontal line character; used for
   the header box's plain separator/border rows. 'cap' is the total size
   of the out[] buffer as declared by the caller (see sappend()); it
   bounds the writes here too, defensively, in case BOX_WIDTH is ever
   changed without the caller's buffer growing to match. */
void build_dash_row(char *out, int cap)
{
  int p;
  int i;

  p = 0;
  for (i = 0; i < BOX_WIDTH / 2; i++) {
    sappend(out, &p, BOXCH_H, cap);
  }
}

/* builds the top border row's content: horizontal line, the title
   message centered-ish, horizontal line, filling exactly BOX_WIDTH
   cells. Width is computed in cells via text_width(), never assumed, so
   this adapts to either language table without hardcoding a length.
   'cap' is the total size of the out[] buffer as declared by the caller
   (see sappend()); if the title message is too long to fit, sappend()
   truncates it (without splitting a multi-byte character) rather than
   overflowing out[], and the fill/border logic below still runs against
   the (possibly negative, clamped to 0) remaining cell count so the
   fixed-width box_row() caller always gets a validly NUL-terminated
   string back. */
void build_title_row(char *out, int cap)
{
  char *title;
  int p;
  int fillCells;
  int fillPairs;
  int i;

  title = MSG(MSG_TITLE);

  p = 0;
  sappend(out, &p, BOXCH_H, cap);
  sappend(out, &p, BOXCH_H, cap);
  sappend(out, &p, " ", cap);
  sappend(out, &p, title, cap);
  sappend(out, &p, " ", cap);

  /* byte offset == cell offset here: every string appended above is
     either 1-byte ASCII or a 2-byte SJIS/box char, so p already equals
     the number of cells used (as long as none of it got truncated by
     sappend(); if it did, fillCells below simply clamps to 0). */
  fillCells = BOX_WIDTH - p;
  if (fillCells < 0) fillCells = 0; /* defensive: title too wide to fit */
  fillPairs = fillCells / 2;
  for (i = 0; i < fillPairs; i++) sappend(out, &p, BOXCH_H, cap);
  if ((fillCells % 2) == 1) sappend(out, &p, " ", cap);
}

/* ---- screen frame buffer (replaces stdio for all screen output) ------- */

void buf_reset(void)
{
  g_scrlen = 0;
}

void buf_putc(char c)
{
  g_scrbuf[g_scrlen] = c;
  g_scrlen++;
}

void buf_puts(char *s)
{
  int i;

  i = 0;
  while (s[i] != 0) {
    buf_putc(s[i]);
    i++;
  }
}

void buf_putuint(unsigned int v)
{
  char tmp[6];
  int n;

  n = 0;
  if (v == 0) {
    buf_putc('0');
    return;
  }
  while (v > 0) {
    tmp[n] = (char)('0' + (v % 10));
    v = v / 10;
    n++;
  }
  while (n > 0) {
    n--;
    buf_putc(tmp[n]);
  }
}

void buf_goto(int row, int col)
{
  buf_puts("\x1b[");
  buf_putuint((unsigned int)(row + 1));
  buf_putc(';');
  buf_putuint((unsigned int)(col + 1));
  buf_putc('H');
}

void buf_color(int code)
{
  buf_puts("\x1b[");
  buf_putuint((unsigned int)code);
  buf_putc('m');
}

void buf_clear(void)
{
  buf_puts("\x1b[2J");
}

void buf_flush(void)
{
  dos_write(g_scrbuf, g_scrlen);
}

/* immediate one-off write, used only for the pre/post-frame terminal mode
   escapes in main() (not part of a screen frame) */
void write_str(char *s)
{
  dos_write(s, (unsigned int)strlen(s));
}

/* ---- directory scanning ------------------------------------------------ */

void read_dir(void)
{
  int ok;
  int idx;
  int dot;
  int i;

  g_count = 0;
  g_truncated = 0;

  strcpy(g_search, g_path);
  strcat(g_search, "*.*");

  dos_setdta(g_dta);
  ok = dos_findfirst(g_search, ATTR_RDONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR);
  while (ok == 0) {
    unsigned char attr = g_dta[21];
    if (!(attr & ATTR_VOLLABEL)) {
      if (g_count < MAX_ENTRIES) {
        idx = g_count;
        strcpy(&g_name[idx * NAME_LEN], (char *)&g_dta[30]);
        g_attr[idx] = attr;
        g_time[idx] = (unsigned int)g_dta[22] | ((unsigned int)g_dta[23] << 8);
        g_date[idx] = (unsigned int)g_dta[24] | ((unsigned int)g_dta[25] << 8);
        g_sizeLo[idx] = (unsigned int)g_dta[26] | ((unsigned int)g_dta[27] << 8);
        g_sizeHi[idx] = (unsigned int)g_dta[28] | ((unsigned int)g_dta[29] << 8);
        g_count++;
      } else {
        g_truncated = 1;
        break;
      }
    }
    ok = dos_findnext();
  }

  if (g_cursor >= g_count) g_cursor = (g_count > 0) ? g_count - 1 : 0;
}

void read_path(void)
{
  unsigned int drive;
  char cwdbuf[80];
  int ok;

  drive = dos_getdrive();
  ok = dos_getcwd(drive + 1, cwdbuf);
  if (ok != 0) cwdbuf[0] = 0;

  g_path[0] = (char)('A' + drive);
  g_path[1] = ':';
  g_path[2] = '\\';
  g_path[3] = 0;
  strcat(g_path, cwdbuf);
  if (cwdbuf[0] != 0) strcat(g_path, "\\");
}

/* ---- marking ----------------------------------------------------------- */

/* directories are never markable - measured against the original: of 32
   entries only the 18 non-directory files could be marked, the 14
   directories could not. */
int is_dir_entry(int idx)
{
  return (g_attr[idx] & ATTR_DIR) ? 1 : 0;
}

int count_marked(void)
{
  int i;
  int n;

  n = 0;
  for (i = 0; i < g_count; i++) {
    if (g_marked[i]) n++;
  }
  return n;
}

void clear_marks(void)
{
  int i;

  for (i = 0; i < MAX_ENTRIES; i++) g_marked[i] = 0;
}

/* SPACE/TAB: sets the mark on the entry under the cursor (does not
   toggle it back off - only HOME toggles). No-op on a directory and
   no-op when the list is empty. */
void mark_cursor(void)
{
  if (g_count == 0) return;
  if (is_dir_entry(g_cursor)) return;
  g_marked[g_cursor] = 1;
}

/* HOME: if any entry is marked, clears every mark; otherwise marks every
   markable (non-directory) entry. */
void toggle_all_marks(void)
{
  int i;

  if (count_marked() > 0) {
    clear_marks();
  } else {
    for (i = 0; i < g_count; i++) {
      if (!is_dir_entry(i)) g_marked[i] = 1;
    }
  }
}

/* ---- directory navigation ----------------------------------------------- */

/* builds g_path + name into out[]; 'cap' is out[]'s declared size and is
   enforced the same way sappend() enforces it elsewhere - truncates
   rather than overflowing, never splits a multi-byte character. */
void build_full_path(char *out, int cap, char *name)
{
  int p;

  p = 0;
  sappend(out, &p, g_path, cap);
  sappend(out, &p, name, cap);
}

/* Enter on a directory entry: "." (stay), ".." (go up one level, "." and
   ".." are listed like the original and not hidden), or a subdirectory
   name (go into it). g_path always ends with '\\'; both branches keep
   that invariant. All appends are bounds-checked against sizeof(g_path)
   via sappend(), the same discipline as everywhere else that builds a
   fixed-size string in this file. */
void enter_selected(void)
{
  char *name;
  int len;
  int p;

  if (g_count == 0) return;
  if (!is_dir_entry(g_cursor)) return;

  name = &g_name[g_cursor * NAME_LEN];

  if (strcmp(name, ".") == 0) {
    return;
  }

  if (strcmp(name, "..") == 0) {
    len = strlen(g_path);
    if (len > 0 && g_path[len - 1] == '\\') len--;
    while (len > 0 && g_path[len - 1] != '\\') len--;
    if (len < 3) len = 3; /* keep the drive root "X:\\" intact */
    g_path[len] = 0;
  } else {
    p = strlen(g_path);
    sappend(g_path, &p, name, sizeof(g_path));
    sappend(g_path, &p, "\\", sizeof(g_path));
  }

  read_dir();
  g_cursor = 0;
  clear_marks();
  draw_screen();
}

/* ---- delete -------------------------------------------------------------- */

/* draws the base screen, then overlays a small modal dialog box (2 lines:
   the prompt, and optionally an error line under it) and flushes once.
   This is the "no dedicated bottom status line; errors appear inside the
   dialog, prompt stays up" behaviour measured from the original - see the
   milestone doc. errmsg may be 0 for "no error line". */
#define DIALOG_WIDTH  60
#define DIALOG_ROW    10
#define DIALOG_COL    8

void draw_dialog(char *msg, char *errmsg)
{
  char cell[DIALOG_WIDTH + 1];
  int i;
  int row;

  draw_screen_frame();

  row = DIALOG_ROW;
  buf_goto(row, DIALOG_COL);
  buf_puts(BOXCH_TL);
  for (i = 0; i < DIALOG_WIDTH / 2; i++) buf_puts(BOXCH_H);
  buf_puts(BOXCH_TR);
  row++;

  for (i = 0; i < DIALOG_WIDTH; i++) cell[i] = ' ';
  cell[DIALOG_WIDTH] = 0;
  put_str_cells(cell, 0, msg, DIALOG_WIDTH);
  buf_goto(row, DIALOG_COL);
  buf_puts(BOXCH_V);
  buf_puts(cell);
  buf_puts(BOXCH_V);
  row++;

  if (errmsg != 0) {
    for (i = 0; i < DIALOG_WIDTH; i++) cell[i] = ' ';
    cell[DIALOG_WIDTH] = 0;
    put_str_cells(cell, 0, errmsg, DIALOG_WIDTH);
    buf_goto(row, DIALOG_COL);
    buf_puts(BOXCH_V);
    buf_puts(cell);
    buf_puts(BOXCH_V);
    row++;
  }

  buf_goto(row, DIALOG_COL);
  buf_puts(BOXCH_BL);
  for (i = 0; i < DIALOG_WIDTH / 2; i++) buf_puts(BOXCH_H);
  buf_puts(BOXCH_BR);

  buf_flush();
}

/* D/d: deletes the marked files, or (when nothing is marked) the single
   file under the cursor. The target rule is one rule, not two cases that
   can disagree: "marked set if non-empty, else the cursor entry" - see
   the milestone doc.
   Directories are never in the marked set (mark_cursor() refuses them),
   so the only way a directory can be "the target" is the no-mark,
   cursor-on-a-directory case; that is caught up front and refused with
   an explicit error dialog rather than silently skipped or silently
   deleting something else. No "delete completed" message is shown on
   success, matching the original; the list is simply shorter afterward. */
void do_delete(void)
{
  int i;
  int idx;
  int key;
  int ok;
  int anyFail;
  int p;
  char path[96];
  char msg[128];

  if (count_marked() == 0) {
    if (g_count == 0) return;
    idx = g_cursor;

    if (is_dir_entry(idx)) {
      draw_dialog(MSG(MSG_DEL_ERR_ISDIR), 0);
      dos_getch();
      draw_screen();
      return;
    }

    p = 0;
    sappend(msg, &p, MSG(MSG_DEL_CONFIRM_ONE_PRE), sizeof(msg));
    sappend(msg, &p, &g_name[idx * NAME_LEN], sizeof(msg));
    sappend(msg, &p, MSG(MSG_DEL_CONFIRM_ONE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
    key = dos_getch();
    if (key != 'y' && key != 'Y') {
      draw_screen();
      return;
    }

    build_full_path(path, sizeof(path), &g_name[idx * NAME_LEN]);
    ok = dos_delete(path);
    if (ok != 0) {
      draw_dialog(MSG(MSG_DEL_ERR_FAILED), 0);
      dos_getch();
    }

    read_dir();
    if (g_cursor >= g_count) g_cursor = (g_count > 0) ? g_count - 1 : 0;
    draw_screen();
    return;
  }

  /* marked set: guaranteed to contain no directories (mark_cursor()
     never marks one), so no per-entry directory check is needed here. */
  p = 0;
  sappend(msg, &p, MSG(MSG_DEL_CONFIRM_MARK_PRE), sizeof(msg));
  sappend_uint(msg, &p, (unsigned int)count_marked(), sizeof(msg));
  sappend(msg, &p, MSG(MSG_DEL_CONFIRM_MARK_SUF), sizeof(msg));
  draw_dialog(msg, 0);
  key = dos_getch();
  if (key != 'y' && key != 'Y') {
    draw_screen();
    return;
  }

  anyFail = 0;
  for (i = 0; i < g_count; i++) {
    if (g_marked[i]) {
      build_full_path(path, sizeof(path), &g_name[i * NAME_LEN]);
      ok = dos_delete(path);
      if (ok != 0) anyFail = 1;
    }
  }
  if (anyFail) {
    draw_dialog(MSG(MSG_DEL_ERR_FAILED), 0);
    dos_getch();
  }

  read_dir();
  clear_marks();
  if (g_cursor >= g_count) g_cursor = (g_count > 0) ? g_count - 1 : 0;
  draw_screen();
}

/* ---- screen drawing ------------------------------------------------------ */

void build_entry_text(int idx, char *buf)
{
  int i;
  int dot;
  int namelen;
  int extlen;
  char *rawname;
  char sizebuf[16];
  char datebuf[9];
  char timebuf[6];

  for (i = 0; i < 39; i++) buf[i] = ' ';
  buf[39] = 0;
  /* no '.' by default; only shown when there is an actual extension */

  /* mark indicator: '*' at column 0, plain text (no color/reverse - the
     real product's mark is a plain character too, confirmed by pixel
     measurement). Directories are never marked, so this can never fire
     for a directory row. */
  if (g_marked[idx]) buf[0] = '*';

  rawname = &g_name[idx * NAME_LEN];
  dot = -1;
  for (i = 0; rawname[i] != 0; i++) {
    if (rawname[i] == '.') { dot = i; break; }
  }
  if (dot < 0) {
    namelen = strlen(rawname);
    if (namelen > 8) namelen = 8;
    put_str_n(buf, 2, rawname, namelen);
  } else {
    namelen = dot;
    if (namelen > 8) namelen = 8;
    put_str_n(buf, 2, rawname, namelen);
    extlen = strlen(rawname + dot + 1);
    if (extlen > 3) extlen = 3;
    if (extlen > 0) {
      buf[10] = '.';
      put_str_n(buf, 11, rawname + dot + 1, extlen);
    }
  }

  /* file-list size column: no ',' grouping in the real product, only the
     header totals (draw_disk_line) are comma-grouped */
  if (g_attr[idx] & ATTR_DIR) {
    strcpy(sizebuf, "<DIR>");
  } else {
    format_u32_plain(g_sizeHi[idx], g_sizeLo[idx], sizebuf);
  }
  right_justify(buf, 14, 9, sizebuf);

  format_date(g_date[idx], datebuf);
  put_str_n(buf, 24, datebuf, 8);

  format_time(g_time[idx], timebuf);
  put_str_n(buf, 34, timebuf, 5);
}

/* builds the disk total/used/free line (header box row ROW_DISK) into a
   plain buffer for box_row(); comma-grouped, unlike the file-list sizes -
   this is one of the header totals lines. */
void draw_disk_line(void)
{
  unsigned int drive;
  unsigned int secPerClus;
  unsigned int availClus;
  unsigned int bytesPerSec;
  unsigned int totalClus;
  unsigned int cLo, cHi;
  unsigned int totLo, totHi;
  unsigned int freeLo, freeHi;
  unsigned int usedLo, usedHi;
  char totalBuf[16];
  char usedBuf[16];
  char freeBuf[16];
  char row[128];
  int p;

  drive = dos_getdrive();
  secPerClus = dos_diskfree(drive + 1, &availClus, &bytesPerSec, &totalClus);

  p = 0;
  if (secPerClus == 0xFFFF) {
    sappend(row, &p, MSG(MSG_DISK_UNAVAIL), sizeof(row));
    box_row(ROW_DISK, BOXCH_V, BOXCH_V, row);
    return;
  }

  cLo = umul32(secPerClus, bytesPerSec, &cHi);            /* bytes/cluster */
  totLo = mul32x16(cHi, cLo, totalClus, &totHi);          /* total bytes   */
  freeLo = mul32x16(cHi, cLo, availClus, &freeHi);        /* free bytes    */
  sub32(totHi, totLo, freeHi, freeLo, &usedHi, &usedLo);  /* used bytes    */

  format_u32(totHi, totLo, totalBuf);
  format_u32(usedHi, usedLo, usedBuf);
  format_u32(freeHi, freeLo, freeBuf);

  sappend(row, &p, MSG(MSG_DISK_TOTAL), sizeof(row));
  sappend(row, &p, totalBuf, sizeof(row));
  sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));
  sappend(row, &p, " ", sizeof(row));
  sappend(row, &p, MSG(MSG_DISK_USED), sizeof(row));
  sappend(row, &p, usedBuf, sizeof(row));
  sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));
  sappend(row, &p, " ", sizeof(row));
  sappend(row, &p, MSG(MSG_DISK_FREE), sizeof(row));
  sappend(row, &p, freeBuf, sizeof(row));
  sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));

  box_row(ROW_DISK, BOXCH_V, BOXCH_V, row);
}

/* builds the current-path line (header box row ROW_PATH) into a plain
   buffer for box_row(). Split out of draw_screen() so all header-box
   content assembly follows the same "build a string, then box_row()"
   shape. */
void draw_path_line(void)
{
  char row[256];
  int p;

  p = 0;
  sappend(row, &p, MSG(MSG_PATH_PREFIX), sizeof(row));
  sappend(row, &p, g_path, sizeof(row));
  if (g_truncated) {
    sappend(row, &p, MSG(MSG_TRUNC_PREFIX), sizeof(row));
    sappend_uint(row, &p, (unsigned int)MAX_ENTRIES, sizeof(row));
    sappend(row, &p, MSG(MSG_TRUNC_SUFFIX), sizeof(row));
  }
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, MSG(MSG_MARKED_LABEL), sizeof(row));
  sappend_uint(row, &p, (unsigned int)count_marked(), sizeof(row));

  box_row(ROW_PATH, BOXCH_V, BOXCH_V, row);
}

/* builds the selected-entry info line (header box row ROW_INFO); keeps
   comma-grouped sizes like draw_disk_line() - it is part of the same
   header box, not the file-list grid. */
void draw_info_line(int visibleCount)
{
  char entrybuf[40];
  char attrbuf[8];
  char sizebuf[16];
  char datebuf[9];
  char timebuf[6];
  char row[128];
  int p;

  p = 0;
  if (visibleCount == 0) {
    sappend(row, &p, MSG(MSG_INFO_PREFIX), sizeof(row));
    sappend(row, &p, MSG(MSG_INFO_EMPTY), sizeof(row));
    box_row(ROW_INFO, BOXCH_V, BOXCH_V, row);
    return;
  }

  build_entry_text(g_cursor, entrybuf);
  format_attr(g_attr[g_cursor], attrbuf);

  if (g_attr[g_cursor] & ATTR_DIR) {
    strcpy(sizebuf, "<DIR>");
  } else {
    format_u32(g_sizeHi[g_cursor], g_sizeLo[g_cursor], sizebuf);
  }
  format_date(g_date[g_cursor], datebuf);
  format_time(g_time[g_cursor], timebuf);

  sappend(row, &p, MSG(MSG_INFO_PREFIX), sizeof(row));
  sappend(row, &p, &g_name[g_cursor * NAME_LEN], sizeof(row));
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, sizebuf, sizeof(row));
  sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, datebuf, sizeof(row));
  sappend(row, &p, " ", sizeof(row));
  sappend(row, &p, timebuf, sizeof(row));
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, MSG(MSG_ATTR_LABEL), sizeof(row));
  sappend(row, &p, attrbuf, sizeof(row));

  box_row(ROW_INFO, BOXCH_V, BOXCH_V, row);
}

void draw_screen_frame(void)
{
  int leftCount;
  int rightCount;
  int visibleCount;
  int row;
  int leftIdx;
  int rightIdx;
  char entrybuf[40];
  char titleRow[BOX_WIDTH + 1];
  char dashRow[BOX_WIDTH + 1];
  int i;

  visibleCount = g_count;
  if (visibleCount > VISIBLE_MAX) visibleCount = VISIBLE_MAX;
  leftCount = (visibleCount > LEFT_ROWS) ? LEFT_ROWS : visibleCount;
  rightCount = visibleCount - leftCount;

  buf_reset();
  buf_clear();

  /* header box, rows 0-5: full-width box-drawing borders around the
     title/disk/path/info lines (see BOXCH_* above for why full-width
     chars rather than the half-width single-line set). */
  build_title_row(titleRow, sizeof(titleRow));
  box_row(ROW_TITLE, BOXCH_TL, BOXCH_TR, titleRow);

  draw_disk_line();

  build_dash_row(dashRow, sizeof(dashRow));
  box_row(ROW_SEP1, BOXCH_LT, BOXCH_RT, dashRow);

  draw_path_line();

  draw_info_line(visibleCount);

  box_row(ROW_SEP2, BOXCH_BL, BOXCH_BR, dashRow);

  for (row = 0; row < LEFT_ROWS; row++) {
    leftIdx = row;
    rightIdx = LEFT_ROWS + row;

    if (leftIdx < leftCount) {
      build_entry_text(leftIdx, entrybuf);
      buf_color(leftIdx == g_cursor ? 33 : 37);
      buf_goto(ROW_LIST_TOP + row, COL_LEFT);
      buf_puts(entrybuf);
    }
    if (rightIdx < visibleCount) {
      build_entry_text(rightIdx, entrybuf);
      buf_color(rightIdx == g_cursor ? 33 : 37);
      buf_goto(ROW_LIST_TOP + row, COL_RIGHT);
      buf_puts(entrybuf);
    }
  }
  buf_color(37);

  buf_goto(ROW_CMD, 0);
  {
    char cmdline[CMDLINE_WIDTH + 1];
    int ci;

    /* zero-fill first so put_str_cells (which never writes past the
       cells it truncates to and never appends a NUL itself) leaves a
       valid, NUL-terminated string no matter how much of MSG_CMDLINE
       fit. Truncation never splits a multi-byte character - see
       put_str_cells(). */
    for (ci = 0; ci <= CMDLINE_WIDTH; ci++) cmdline[ci] = 0;
    put_str_cells(cmdline, 0, MSG(MSG_CMDLINE), CMDLINE_WIDTH);
    buf_puts(cmdline);
  }
}

void draw_screen(void)
{
  draw_screen_frame();
  buf_flush();
}

/* ---- input / cursor movement -------------------------------------------- */

void move_cursor(int dir)
{
  int leftCount;
  int rightCount;
  int visibleCount;
  int row;

  visibleCount = g_count;
  if (visibleCount > VISIBLE_MAX) visibleCount = VISIBLE_MAX;
  if (visibleCount == 0) return;
  leftCount = (visibleCount > LEFT_ROWS) ? LEFT_ROWS : visibleCount;
  rightCount = visibleCount - leftCount;

  if (g_cursor < leftCount) {
    row = g_cursor;
    if (dir == DIR_UP) { if (row > 0) g_cursor = row - 1; }
    else if (dir == DIR_DOWN) { if (row < leftCount - 1) g_cursor = row + 1; }
    else if (dir == DIR_RIGHT) { if (row < rightCount) g_cursor = leftCount + row; }
    /* left: already in left column, nothing to do */
  } else {
    row = g_cursor - leftCount;
    if (dir == DIR_UP) { if (row > 0) g_cursor = leftCount + row - 1; }
    else if (dir == DIR_DOWN) { if (row < rightCount - 1) g_cursor = leftCount + row + 1; }
    else if (dir == DIR_LEFT) { g_cursor = row; }
    /* right: already in right column, nothing to do */
  }
}

/* ---- command line switches ----------------------------------------------- */

void parse_args(int argc, char *argv[])
{
  int i;

  g_lang = LANG_JA; /* default, per docs/i18n-design.md */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "/E") == 0 || strcmp(argv[i], "/e") == 0) {
      g_lang = LANG_EN;
    } else if (strcmp(argv[i], "/J") == 0 || strcmp(argv[i], "/j") == 0) {
      g_lang = LANG_JA;
    }
  }
}

/* ---- main ---------------------------------------------------------------- */

int main(int argc, char *argv[])
{
  int key;
  int running;

  if (!msg_selftest()) {
    write_str("FILER: message table error (JA/EN mismatch)\r\n");
    return 1;
  }

  parse_args(argc, argv);

  write_str("\x1b[>1h"); /* release the bottom function-key line */
  write_str("\x1b[>5h"); /* hide the text cursor */

  read_path();
  read_dir();
  g_cursor = 0;
  draw_screen();

  running = 1;
  while (running) {
    key = dos_getch();
    if (key == KEY_UP || key == KEY_CTRL_E) {
      move_cursor(DIR_UP);
      draw_screen();
    } else if (key == KEY_DOWN || key == KEY_CTRL_X) {
      move_cursor(DIR_DOWN);
      draw_screen();
    } else if (key == KEY_LEFT) {
      move_cursor(DIR_LEFT);
      draw_screen();
    } else if (key == KEY_RIGHT) {
      move_cursor(DIR_RIGHT);
      draw_screen();
    } else if (key == KEY_SPACE) {
      mark_cursor();
      move_cursor(DIR_DOWN);
      draw_screen();
    } else if (key == KEY_TAB) {
      mark_cursor();
      draw_screen();
    } else if (key == KEY_HOME) {
      toggle_all_marks();
      draw_screen();
    } else if (key == KEY_ENTER) {
      enter_selected();
    } else if (key == 'd' || key == 'D') {
      do_delete();
    } else if (key == 'q' || key == 'Q' || key == KEY_ESC) {
      running = 0;
    }
  }

  write_str("\x1b[2J");
  write_str("\x1b[>5l"); /* show the text cursor again */
  write_str("\x1b[>1l"); /* restore the function-key line */

  return 0;
}
