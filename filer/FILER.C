/*
 * FILER.C - PC-98 / FreeDOS(98) directory browser (milestone 1)
 *
 * Independent, from-scratch implementation. See README.md in this
 * directory for the independence declaration. All on-screen text below
 * is original wording written for this project; it does not reproduce
 * any text from any existing product.
 *
 * Milestone 1 scope: directory listing, cursor movement, quit only.
 * No file operations (copy/delete/rename/mkdir/...) are implemented yet.
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

#define COL_LEFT      0
#define COL_RIGHT     40

#define SCRBUF_SIZE   8192

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

const char *g_msgJA[] = {
  "PC98Guest ファイラ - ディレクトリビューア（マイルストーン1・開発中）",
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
  "矢印キー:移動　Q/ESC:終了"
};

const char *g_msgEN[] = {
  "PC98Guest Filer - directory viewer (milestone 1, work in progress)",
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
  "Arrows: move   Q/ESC: quit"
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
    if (i > 0 && (i % 3) == 2) {
      out[j] = ',';
      j++;
    }
  }
  out[j] = 0;
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
  out[0] = (char)('0' + hour / 10);
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
  buf[10] = '.';

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
    put_str_n(buf, 11, rawname + dot + 1, extlen);
  }

  if (g_attr[idx] & ATTR_DIR) {
    strcpy(sizebuf, "<DIR>");
  } else {
    format_u32(g_sizeHi[idx], g_sizeLo[idx], sizebuf);
  }
  right_justify(buf, 14, 9, sizebuf);

  format_date(g_date[idx], datebuf);
  put_str_n(buf, 24, datebuf, 8);

  format_time(g_time[idx], timebuf);
  put_str_n(buf, 34, timebuf, 5);
}

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

  drive = dos_getdrive();
  secPerClus = dos_diskfree(drive + 1, &availClus, &bytesPerSec, &totalClus);

  buf_goto(ROW_DISK, 0);
  if (secPerClus == 0xFFFF) {
    buf_puts(MSG(MSG_DISK_UNAVAIL));
    return;
  }

  cLo = umul32(secPerClus, bytesPerSec, &cHi);            /* bytes/cluster */
  totLo = mul32x16(cHi, cLo, totalClus, &totHi);          /* total bytes   */
  freeLo = mul32x16(cHi, cLo, availClus, &freeHi);        /* free bytes    */
  sub32(totHi, totLo, freeHi, freeLo, &usedHi, &usedLo);  /* used bytes    */

  format_u32(totHi, totLo, totalBuf);
  format_u32(usedHi, usedLo, usedBuf);
  format_u32(freeHi, freeLo, freeBuf);

  buf_puts(MSG(MSG_DISK_TOTAL));
  buf_puts(totalBuf);
  buf_puts(MSG(MSG_BYTES_SUFFIX));
  buf_puts("  ");
  buf_puts(MSG(MSG_DISK_USED));
  buf_puts(usedBuf);
  buf_puts(MSG(MSG_BYTES_SUFFIX));
  buf_puts("  ");
  buf_puts(MSG(MSG_DISK_FREE));
  buf_puts(freeBuf);
  buf_puts(MSG(MSG_BYTES_SUFFIX));
}

void draw_info_line(int visibleCount)
{
  char entrybuf[40];
  char attrbuf[8];
  char sizebuf[16];
  char datebuf[9];
  char timebuf[6];

  buf_goto(ROW_INFO, 0);
  if (visibleCount == 0) {
    buf_puts(MSG(MSG_INFO_PREFIX));
    buf_puts(MSG(MSG_INFO_EMPTY));
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

  buf_puts(MSG(MSG_INFO_PREFIX));
  buf_puts(&g_name[g_cursor * NAME_LEN]);
  buf_puts("  ");
  buf_puts(sizebuf);
  buf_puts(MSG(MSG_BYTES_SUFFIX));
  buf_puts("  ");
  buf_puts(datebuf);
  buf_puts(" ");
  buf_puts(timebuf);
  buf_puts("  ");
  buf_puts(MSG(MSG_ATTR_LABEL));
  buf_puts(attrbuf);
}

void draw_screen(void)
{
  int leftCount;
  int rightCount;
  int visibleCount;
  int row;
  int leftIdx;
  int rightIdx;
  char entrybuf[40];
  int i;

  visibleCount = g_count;
  if (visibleCount > VISIBLE_MAX) visibleCount = VISIBLE_MAX;
  leftCount = (visibleCount > LEFT_ROWS) ? LEFT_ROWS : visibleCount;
  rightCount = visibleCount - leftCount;

  buf_reset();
  buf_clear();

  buf_goto(ROW_TITLE, 0);
  buf_puts(MSG(MSG_TITLE));

  draw_disk_line();

  buf_goto(ROW_SEP1, 0);
  for (i = 0; i < 79; i++) buf_putc('-');

  buf_goto(ROW_PATH, 0);
  buf_puts(MSG(MSG_PATH_PREFIX));
  buf_puts(g_path);
  if (g_truncated) {
    buf_puts(MSG(MSG_TRUNC_PREFIX));
    buf_putuint((unsigned int)MAX_ENTRIES);
    buf_puts(MSG(MSG_TRUNC_SUFFIX));
  }

  draw_info_line(visibleCount);

  buf_goto(ROW_SEP2, 0);
  for (i = 0; i < 79; i++) buf_putc('-');

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
  buf_puts(MSG(MSG_CMDLINE));

  buf_flush();
}

/* ---- input / cursor movement -------------------------------------------- */

void move_cursor(int key2)
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
    if (key2 == 0x48) { if (row > 0) g_cursor = row - 1; }             /* up */
    else if (key2 == 0x50) { if (row < leftCount - 1) g_cursor = row + 1; } /* down */
    else if (key2 == 0x4d) { if (row < rightCount) g_cursor = leftCount + row; } /* right */
    /* left: already in left column, nothing to do */
  } else {
    row = g_cursor - leftCount;
    if (key2 == 0x48) { if (row > 0) g_cursor = leftCount + row - 1; }
    else if (key2 == 0x50) { if (row < rightCount - 1) g_cursor = leftCount + row + 1; }
    else if (key2 == 0x4b) { g_cursor = row; } /* left */
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
  int key2;
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
    if (key == 0) {
      key2 = dos_getch();
      move_cursor(key2);
      draw_screen();
    } else if (key == 'q' || key == 'Q' || key == 0x1b) {
      running = 0;
    }
  }

  write_str("\x1b[2J");
  write_str("\x1b[>5l"); /* show the text cursor again */
  write_str("\x1b[>1l"); /* restore the function-key line */

  return 0;
}
