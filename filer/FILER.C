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
 * are never deleted by this command.
 * Milestone 3 adds: a reusable modal text-input dialog (input_dialog()),
 * used by Rename (R, single entry under the cursor) and mKdir (K, create
 * a directory in the current path). Both re-use the same dialog and show
 * a failure message inline in the same box rather than a separate line,
 * keeping the prompt and the partially-typed text on screen.
 * Milestone 4 adds: Copy (C) and Move (M), using the same "marked set if
 * non-empty, else the cursor entry" target rule as Delete, with a
 * per-file Y/N/ESC overwrite prompt (the original refuses same-name
 * copies outright with no prompt - see README's independence notes for
 * why this implementation asks instead). It also replaces the bottom
 * command line: instead of one long bilingual "key:description" message
 * capped at CMDLINE_WIDTH cells, it now shows English command words with
 * their key letter in reverse video, matching the original's own
 * measured convention (see g_cmdWords[]/cmdline_put_word()).
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
#define INPUT_MAXLEN  12     /* max chars (not counting NUL) enterable in
                                 input_dialog(): an 8.3 name, same limit as
                                 NAME_LEN-1; see do_rename()/do_mkdir() */
#define DEST_MAXLEN   40     /* max chars enterable for a Copy/Move
                                 destination directory (e.g. "B:\\SUBDIR"),
                                 a different (longer) limit than
                                 INPUT_MAXLEN because this is a path, not
                                 an 8.3 name; see do_copy()/do_move() */
#define COPY_BUF_SIZE 4096   /* size of the single global file-copy
                                 buffer g_copybuf; see its declaration */
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
#define MSG_MARKED_LABEL       12
#define MSG_DEL_CONFIRM_ONE_PRE   13
#define MSG_DEL_CONFIRM_ONE_SUF   14
#define MSG_DEL_CONFIRM_MARK_PRE  15
#define MSG_DEL_CONFIRM_MARK_SUF  16
#define MSG_DEL_ERR_ISDIR      17
#define MSG_DEL_ERR_FAILED     18
#define MSG_RENAME_PROMPT       19
#define MSG_RENAME_ERR_EMPTY    20
#define MSG_RENAME_ERR_FAILED   21
#define MSG_MKDIR_PROMPT        22
#define MSG_MKDIR_ERR_EMPTY     23
#define MSG_MKDIR_ERR_FAILED    24
#define MSG_CM_ERR_HASDIR       25
#define MSG_COPY_PROMPT         26
#define MSG_MOVE_PROMPT         27
#define MSG_CM_ERR_EMPTY        28
#define MSG_COPY_ERR_PRE        29
#define MSG_MOVE_ERR_PRE        30
#define MSG_OVERWRITE_PRE       31
#define MSG_OVERWRITE_SUF       32
#define MSG_COPY_DONE_PRE       33
#define MSG_COPY_DONE_SUF       34
#define MSG_MOVE_DONE_PRE       35
#define MSG_MOVE_DONE_SUF       36

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
  "マーク:",
  "このファイルを削除しますか: ",
  " (Y/N)",
  "マークした ",
  " 件を削除します (Y/N)",
  "ディレクトリは削除できません（何かキーを押してください）",
  "削除に失敗しました（読み取り専用？）キーを押してください",
  "新しい名前: ",
  "名前を入力してください",
  "リネームに失敗しました（同名がある？）",
  "新しいディレクトリ名: ",
  "名前を入力してください",
  "作成に失敗しました（同名がある？）",
  "ディレクトリは対象にできません（何かキーを押してください）",
  "コピー先: ",
  "移動先: ",
  "フォルダ名を入力してください",
  "コピーに失敗しました: ",
  "移動に失敗しました: ",
  "上書きしますか: ",
  " (Y/N/ESC)",
  "コピー完了: ",
  " 件",
  "移動完了: ",
  " 件"
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
  "Marked:",
  "Delete this file: ",
  " (Y/N)",
  "Delete the ",
  " marked file(s)? (Y/N)",
  "Cannot delete a directory (press any key)",
  "Delete failed (read-only?) - press any key",
  "New name: ",
  "Enter a name",
  "Rename failed (name already exists?)",
  "New directory name: ",
  "Enter a name",
  "Create failed (name already exists?)",
  "Cannot copy/move a directory (press any key)",
  "Copy to: ",
  "Move to: ",
  "Enter a folder name",
  "Copy failed: ",
  "Move failed: ",
  "Overwrite: ",
  " (Y/N/ESC)",
  "Copied ",
  " file(s)",
  "Moved ",
  " file(s)"
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

/* ---- bottom command line (language-independent) ------------------------
 * Milestone 4 replaces the old "key:description" MSG_CMDLINE line (which
 * was pinned to CMDLINE_WIDTH cells and could not fit more than a
 * handful of keys once written out in full words) with the original's
 * own convention, measured from the real product: command *names* in
 * English, with the letter that is the actual key shown in reverse video
 * (see cmdline_put_word()), so many more commands fit on one line. This
 * line is the same in both languages by design - the command words and
 * the short movement/mark/open legend are plain English abbreviations,
 * not translated text, so they live here as ordinary C data rather than
 * in g_msgJA/g_msgEN.
 * g_cmdWords[0] is the plain (non-highlighted) legend text; g_cmdHiPos[0]
 * is -1 to mark it as "no highlight". Every other g_cmdWords[i] is a
 * command name whose key is g_cmdWords[i][g_cmdHiPos[i]] - e.g. mKdir's
 * key is 'K', the character at index 1, matching the 'k'/'K' dispatch in
 * main(). check.py reconstructs this exact same line (prefix, then a
 * single space plus each word) to verify it never exceeds CMDLINE_WIDTH
 * screen cells - see draw_screen_frame()'s cmdline block below, which
 * must keep assembling it the same way. */
char *g_cmdWords[] = {
  "Arrow:move SP/Tab:mark HOME:all Enter:open",
  "Copy",
  "Move",
  "Delete",
  "Rename",
  "mKdir",
  "Quit"
};
int g_cmdHiPos[] = { -1, 0, 0, 0, 0, 1, 0 };

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

char g_copybuf[COPY_BUF_SIZE]; /* single shared Copy/Move I/O buffer -
                                    see COPY_BUF_SIZE's comment; never put
                                    a buffer this size on the stack in a
                                    16-bit small-model program. */

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

/* create a directory (INT 21h AH=39h, DS:DX = ASCIZ path). Returns 0 on
   success, nonzero on failure (e.g. name already exists). */
int dos_mkdir(char *path)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x39\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* rename/move a file or directory (INT 21h AH=56h, DS:DX = old ASCIZ
   path, ES:DI = new ASCIZ path). ES is not guaranteed to already equal
   DS in the general case, so it is saved/set/restored here rather than
   assumed. Returns 0 on success, nonzero on failure (e.g. a name that
   already exists at the destination). */
int dos_rename(char *oldPath, char *newPath)
{
  asm("mov dx, [bp+4]\n"
      "mov di, [bp+6]\n"
      "push es\n"
      "mov ax, ds\n"
      "mov es, ax\n"
      "mov ah, 0x56\n"
      "int 0x21\n"
      "pop es\n"
      "sbb ax, ax");
}

/* ---- file I/O (Copy/Move; INT 21h AH=3Dh/3Ch/3Fh/40h/3Eh/57h) ----------
 * These use the same "jnc LABEL / mov ax,-1 / LABEL:" idiom instead of
 * the "sbb ax,ax" trick used above, because their success value is not
 * simply 0 - it is a handle (dos_open/dos_create) or a byte count
 * (dos_read/dos_wfile) that must be returned as-is from AX when CF is
 * clear. Each label name below is unique across this whole file: this
 * inline asm is emitted verbatim into one shared assembly output (see
 * smlrc.md - "output verbatim"), so two functions reusing the same
 * label would collide at assemble time. ------------------------------ */

/* open an existing file (mode 0 = read-only, matching this program's
   only use: reading a Copy/Move source). Returns a handle >= 0, or -1
   on failure. */
int dos_open(char *path, unsigned int mode)
{
  asm("mov dx, [bp+4]\n"
      "mov al, [bp+6]\n"
      "mov ah, 0x3d\n"
      "int 0x21\n"
      "jnc L_dos_open_ok\n"
      "mov ax, -1\n"
      "L_dos_open_ok:");
}

/* create (or truncate, if it already exists) a file for writing, with
   normal attributes. Returns a handle >= 0, or -1 on failure. */
int dos_create(char *path, unsigned int attr)
{
  asm("mov dx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov ah, 0x3c\n"
      "int 0x21\n"
      "jnc L_dos_create_ok\n"
      "mov ax, -1\n"
      "L_dos_create_ok:");
}

/* closes a handle from dos_open()/dos_create(). Returns 0 on success,
   nonzero on failure. */
int dos_close(unsigned int handle)
{
  asm("mov bx, [bp+4]\n"
      "mov ah, 0x3e\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* reads up to len bytes into buf. Returns the number of bytes actually
   read (0 means end of file, not an error), or -1 on failure. */
int dos_read(unsigned int handle, char *buf, unsigned int len)
{
  asm("mov bx, [bp+4]\n"
      "mov dx, [bp+6]\n"
      "mov cx, [bp+8]\n"
      "mov ah, 0x3f\n"
      "int 0x21\n"
      "jnc L_dos_read_ok\n"
      "mov ax, -1\n"
      "L_dos_read_ok:");
}

/* writes len bytes from buf to an open handle (unlike dos_write() above,
   which is hardcoded to handle 1 for screen output). Returns the number
   of bytes actually written, or -1 on failure; the caller compares this
   against the requested len to catch a short write (e.g. disk full). */
int dos_wfile(unsigned int handle, char *buf, unsigned int len)
{
  asm("mov bx, [bp+4]\n"
      "mov dx, [bp+6]\n"
      "mov cx, [bp+8]\n"
      "mov ah, 0x40\n"
      "int 0x21\n"
      "jnc L_dos_wfile_ok\n"
      "mov ax, -1\n"
      "L_dos_wfile_ok:");
}

/* AH=57h AL=0: reads an open handle's file date/time into *timeOut/
   *dateOut (packed DOS format, the same encoding format_date()/
   format_time() already decode elsewhere in this file). Returns 0 on
   success, nonzero on failure. */
int dos_getftime(unsigned int handle, unsigned int *timeOut, unsigned int *dateOut)
{
  asm("mov bx, [bp+4]\n"
      "mov al, 0\n"
      "mov ah, 0x57\n"
      "int 0x21\n"
      "jc L_dos_getftime_err\n"
      "mov si, [bp+6]\n"
      "mov [si], cx\n"
      "mov si, [bp+8]\n"
      "mov [si], dx\n"
      "mov ax, 0\n"
      "jmp L_dos_getftime_done\n"
      "L_dos_getftime_err:\n"
      "mov ax, -1\n"
      "L_dos_getftime_done:");
}

/* AH=57h AL=1: sets an open handle's file date/time (packed DOS format).
   Returns 0 on success, nonzero on failure. */
int dos_setftime(unsigned int handle, unsigned int time, unsigned int date)
{
  asm("mov bx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov dx, [bp+8]\n"
      "mov al, 1\n"
      "mov ah, 0x57\n"
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

/* writes one command-line word, byte by byte, highlighting exactly the
   character at cell index hlPos (its key) in reverse video. Measured on
   real hardware: ESC[7m is reverse video (attribute b2) and works;
   ESC[1m (bold/highlight) does not change anything visible; ESC[4m
   (underline) shifts the following glyph 4 dots to the right, a real
   rendering bug, so neither of those is used here. ESC[0m resets *all*
   attributes, including color, not just reverse video - so color 37 (the
   line's base color, set by the caller before this is used, matching
   draw_screen_frame()'s buf_color(37) before the whole entries loop) is
   re-applied right after every ESC[0m, keeping the rest of the word (and
   whatever this program draws next) at the intended color rather than
   whatever "no color set" defaults to.
   Command words are plain ASCII (see g_cmdWords[]), so this indexes by
   byte, not by text_width() cell - unlike the JA/EN message text
   elsewhere in this file, no SJIS multi-byte handling is needed here.
   hlPos == -1 (used for g_cmdWords[0], the plain legend text) disables
   the highlight entirely. */
void cmdline_put_word(char *word, int hlPos)
{
  int i;

  for (i = 0; word[i] != 0; i++) {
    if (i == hlPos) {
      buf_puts("\x1b[7m");
      buf_putc(word[i]);
      buf_puts("\x1b[0m");
      buf_color(37);
    } else {
      buf_putc(word[i]);
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

/* ---- reusable text-input dialog (Rename / mKdir) -----------------------
 * Draws the same kind of box as draw_dialog() (base screen still visible
 * underneath - only the dialog area is overwritten) but with one editable
 * field: a prompt fragment followed by the text being typed. Used by both
 * do_rename() and do_mkdir() so the box, the edit keys, and the "error
 * shown inline, prompt stays up, keep typing" behaviour exist in exactly
 * one place.
 * ------------------------------------------------------------------- */

/* renders one frame of the input dialog: base screen, box border, the
   prompt+typed-text line, an optional error line below it, and leaves
   the (temporarily visible) text cursor positioned right after the last
   typed character so the user can see where they are. 'buf' holds
   'len' already-typed characters (not necessarily NUL-terminated at
   'len' yet - the caller NUL-terminates on return, not per frame).
   'line' is sized generously: prompt fragments here are short JA/EN
   labels and 'buf' is at most INPUT_MAXLEN ASCII bytes, so this never
   comes close to overflowing; sappend() bounds it defensively anyway,
   the same discipline as every other row-building function above. */
void draw_input_box(char *prompt, char *buf, int len, char *errmsg)
{
  char cell[DIALOG_WIDTH + 1];
  char line[DIALOG_WIDTH * 2 + NAME_LEN + 4];
  int i;
  int row;
  int fieldRow;
  int fieldCol;
  int p;
  int promptCells;

  draw_screen_frame();

  row = DIALOG_ROW;
  buf_goto(row, DIALOG_COL);
  buf_puts(BOXCH_TL);
  for (i = 0; i < DIALOG_WIDTH / 2; i++) buf_puts(BOXCH_H);
  buf_puts(BOXCH_TR);
  row++;

  p = 0;
  sappend(line, &p, prompt, sizeof(line));
  promptCells = text_width(line);
  buf[len] = 0; /* line/sappend below need a NUL-terminated C string */
  sappend(line, &p, buf, sizeof(line));

  for (i = 0; i < DIALOG_WIDTH; i++) cell[i] = ' ';
  cell[DIALOG_WIDTH] = 0;
  put_str_cells(cell, 0, line, DIALOG_WIDTH);
  buf_goto(row, DIALOG_COL);
  buf_puts(BOXCH_V);
  buf_puts(cell);
  buf_puts(BOXCH_V);
  fieldRow = row;
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

  /* BOXCH_V is a 2-cell full-width character (see BOXCH_* comments
     above), so the field text starts 2 cells past the border column. */
  fieldCol = DIALOG_COL + 2 + promptCells + len;
  buf_goto(fieldRow, fieldCol);

  buf_flush();
}

/* runs the modal edit loop for one text field. 'buf' holds the initial
   value on entry (may be empty; must already be NUL-terminated) and
   receives the edited text (also NUL-terminated) on return, whether
   confirmed or cancelled. 'buf' must be declared with at least
   maxlen+1 bytes - see INPUT_MAXLEN, the limit every caller in this
   file uses (an 8.3 DOS name). 'errmsg' may be 0 for "no error line to
   start"; passing a non-0 errmsg lets a caller re-enter this loop after
   a failed DOS operation with the error still showing next to the same
   prompt and the same typed text, rather than losing the prompt behind
   a separate "press any key" dialog.
   Editing: BS (0x08 - the same code KEY_LEFT uses outside this dialog,
   but here it is always backspace, never "move left") deletes the last
   character; printable ASCII (0x20-0x7E) is appended if there is still
   room under maxlen; anything else is ignored. Any edit keypress clears
   a currently-shown error, since the user is now acting on it.
   The text cursor is hidden everywhere else in this program (see
   main()'s "\x1b[>5h" at start), so it is shown just for the lifetime
   of this loop and hidden again before returning, on every exit path.
   Returns 1 if Enter confirmed the input, 0 if ESC cancelled it. */
int input_dialog(char *prompt, char *buf, int maxlen, char *errmsg)
{
  int len;
  int key;

  len = strlen(buf);
  if (len > maxlen) len = maxlen; /* defensive */

  write_str("\x1b[>5l"); /* show the text cursor while editing */

  for (;;) {
    draw_input_box(prompt, buf, len, errmsg);
    key = dos_getch();

    if (key == KEY_ESC) {
      buf[len] = 0;
      write_str("\x1b[>5h");
      return 0;
    }
    if (key == KEY_ENTER) {
      buf[len] = 0;
      write_str("\x1b[>5h");
      return 1;
    }
    if (key == 0x08) { /* BS; same code as KEY_LEFT, treated as BS here */
      if (len > 0) len--;
      errmsg = 0;
      continue;
    }
    if (key >= 0x20 && key <= 0x7e) {
      if (len < maxlen) {
        buf[len] = (char)key;
        len++;
      }
      errmsg = 0;
      continue;
    }
    /* any other key (arrows, TAB, HOME, ...): ignored in this dialog */
  }
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

/* R/r: renames the single entry under the cursor (marks are not used -
   the target is always the cursor entry, unlike D which prefers the
   marked set). "." and ".." are not renameable and are silently
   ignored, matching enter_selected()'s treatment of "." as a no-op.
   The input dialog is pre-filled with the current name; on confirm,
   dos_rename() is attempted and, on failure, the dialog re-opens with
   an error line and the text the user typed still in the field so they
   can correct it without retyping everything. */
void do_rename(void)
{
  char buf[INPUT_MAXLEN + 1];
  char oldPath[96];
  char newPath[96];
  char *name;
  char *err;
  int ok;
  int idx;
  int confirmed;

  if (g_count == 0) return;
  idx = g_cursor;
  name = &g_name[idx * NAME_LEN];
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return;

  strcpy(buf, name); /* name is at most NAME_LEN-1 = INPUT_MAXLEN chars */

  err = 0;
  for (;;) {
    confirmed = input_dialog(MSG(MSG_RENAME_PROMPT), buf, INPUT_MAXLEN, err);
    if (!confirmed) {
      draw_screen();
      return;
    }
    if (buf[0] == 0) {
      err = MSG(MSG_RENAME_ERR_EMPTY);
      continue;
    }

    build_full_path(oldPath, sizeof(oldPath), name);
    build_full_path(newPath, sizeof(newPath), buf);
    ok = dos_rename(oldPath, newPath);
    if (ok == 0) break;
    err = MSG(MSG_RENAME_ERR_FAILED);
  }

  read_dir();
  draw_screen();
}

/* K/k: creates a new directory in the current path (g_path). The input
   dialog starts empty; on confirm, dos_mkdir() is attempted and, on
   failure (e.g. a name that already exists), the dialog re-opens with
   an error line, same shape as do_rename() above. */
void do_mkdir(void)
{
  char buf[INPUT_MAXLEN + 1];
  char newPath[96];
  char *err;
  int ok;
  int confirmed;

  buf[0] = 0;
  err = 0;
  for (;;) {
    confirmed = input_dialog(MSG(MSG_MKDIR_PROMPT), buf, INPUT_MAXLEN, err);
    if (!confirmed) {
      draw_screen();
      return;
    }
    if (buf[0] == 0) {
      err = MSG(MSG_MKDIR_ERR_EMPTY);
      continue;
    }

    build_full_path(newPath, sizeof(newPath), buf);
    ok = dos_mkdir(newPath);
    if (ok == 0) break;
    err = MSG(MSG_MKDIR_ERR_FAILED);
  }

  read_dir();
  draw_screen();
}

/* ---- copy / move --------------------------------------------------------- */

/* builds destDir + "\" + name into out[] (unlike build_full_path(), which
   always joins against g_path - Copy/Move destinations are a directory
   the user just typed, not the current directory). Adds the separating
   backslash only if destDir doesn't already end with one, so both
   "B:\SUBDIR" and "B:\SUBDIR\" work as input. Same bounds discipline as
   build_full_path()/sappend() everywhere else in this file. */
void join_dir_name(char *out, int cap, char *destDir, char *name)
{
  int p;
  int dlen;

  p = 0;
  sappend(out, &p, destDir, cap);
  dlen = strlen(destDir);
  if (dlen == 0 || destDir[dlen - 1] != '\\') {
    sappend(out, &p, "\\", cap);
  }
  sappend(out, &p, name, cap);
}

/* true if a (non-directory) file already exists at path. Reuses
   dos_setdta()/dos_findfirst() as a one-off lookup; this is safe even
   though read_dir() also uses g_dta for its own FindFirst/FindNext scan,
   because the two never run at the same time - read_dir() always calls
   dos_setdta() again itself the next time it runs. The search attribute
   mask intentionally omits ATTR_DIR: DOS's FindFirst always matches
   plain files regardless of the mask, but only matches a directory if
   ATTR_DIR is set in it, so this looks for "a file with this name",
   which is exactly the overwrite question Copy/Move need answered. */
int file_exists(char *path)
{
  int ok;

  dos_setdta(g_dta);
  ok = dos_findfirst(path, ATTR_RDONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE);
  return (ok == 0) ? 1 : 0;
}

/* the destination drive letter implied by a typed destDir string: its
   own "X:" prefix if it has one, otherwise the current directory's drive
   (g_path[0], always an uppercase letter - see read_path()). Used by
   do_move() to decide same-drive rename vs. cross-drive copy+delete. */
char dest_drive(char *destDir)
{
  char c;

  if (destDir[0] != 0 && destDir[1] == ':') {
    c = destDir[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return c;
  }
  return g_path[0];
}

/* case-insensitive path comparison (no stricmp() in this program's
   string.h subset). Used only to guard against a Move whose destination
   turns out to be identical to its source (e.g. the user retyped the
   current directory) - without this check, move_confirm_and_move()'s
   overwrite handling would delete the file and then fail to recreate
   it. */
int path_eq(char *a, char *b)
{
  int i;
  char ca;
  char cb;

  i = 0;
  for (;;) {
    ca = a[i];
    cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
    if (ca == 0) return 1;
    i++;
  }
}

/* copies one file from srcPath to dstPath (INT 21h AH=3Dh open / 3Fh
   read / 3Ch create / 40h write / 3Eh close), then copies the source's
   timestamp onto the destination with AH=57h - get it from the still-open
   source handle, set it on the still-open destination handle, both
   before either is closed. Uses the single global g_copybuf (see its
   declaration) in COPY_BUF_SIZE-byte chunks rather than a stack buffer:
   SmallerC's small model keeps all data in one 64KB segment, and this
   program's directory-listing arrays (g_name et al., sized by
   MAX_ENTRIES) already claim a large share of it, so a second
   already-large buffer must not also be duplicated on the stack.
   Returns 0 on success, nonzero on failure. On failure, a partially
   written destination file is deliberately left as-is rather than
   deleted - do_move() relies on a nonzero return here to mean "the
   source must NOT be deleted", and guessing at cleanup here would risk
   destroying data instead. */
int copy_one_file(char *srcPath, char *dstPath)
{
  int srcH;
  int dstH;
  int n;
  int ok;
  unsigned int ftime;
  unsigned int fdate;

  srcH = dos_open(srcPath, 0);
  if (srcH < 0) return -1;

  dstH = dos_create(dstPath, 0);
  if (dstH < 0) {
    dos_close((unsigned int)srcH);
    return -1;
  }

  ok = 0;
  for (;;) {
    n = dos_read((unsigned int)srcH, g_copybuf, COPY_BUF_SIZE);
    if (n < 0) { ok = -1; break; }
    if (n == 0) break; /* end of file */
    if (dos_wfile((unsigned int)dstH, g_copybuf, (unsigned int)n) != n) {
      ok = -1;
      break;
    }
  }

  if (ok == 0) {
    if (dos_getftime((unsigned int)srcH, &ftime, &fdate) == 0) {
      dos_setftime((unsigned int)dstH, ftime, fdate);
    }
  }

  dos_close((unsigned int)srcH);
  dos_close((unsigned int)dstH);
  return ok;
}

/* per-file Copy step, shared by the "cursor only" and "marked set" paths
   in do_copy(): builds the source/destination paths, asks Y/N/ESC before
   clobbering an existing destination file (the original refuses same-
   name copies outright with no prompt at all - see do_copy()'s header
   comment for why this implementation asks instead), then copies.
   Returns 1 = copied, 0 = skipped (N to the overwrite prompt; not an
   error), -1 = failed (an error dialog was already shown), -2 = the user
   pressed ESC at the overwrite prompt, meaning "abort the whole
   operation", not just this one file. */
int copy_confirm_and_copy(int idx, char *destDir)
{
  char srcPath[96];
  char dstPath[96];
  char msg[128];
  char *name;
  int p;
  int key;

  name = &g_name[idx * NAME_LEN];
  build_full_path(srcPath, sizeof(srcPath), name);
  join_dir_name(dstPath, sizeof(dstPath), destDir, name);

  if (file_exists(dstPath)) {
    p = 0;
    sappend(msg, &p, MSG(MSG_OVERWRITE_PRE), sizeof(msg));
    sappend(msg, &p, name, sizeof(msg));
    sappend(msg, &p, MSG(MSG_OVERWRITE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
    key = dos_getch();
    if (key == KEY_ESC) return -2;
    if (key != 'y' && key != 'Y') return 0;
  }

  if (copy_one_file(srcPath, dstPath) != 0) {
    p = 0;
    sappend(msg, &p, MSG(MSG_COPY_ERR_PRE), sizeof(msg));
    sappend(msg, &p, name, sizeof(msg));
    draw_dialog(msg, 0);
    dos_getch();
    return -1;
  }
  return 1;
}

/* per-file Move step - see copy_confirm_and_copy() for the return-value
   meanings, which are the same here. sameDrive is computed once by
   do_move() (destDir is the same for every file in one Move operation,
   so its implied drive letter does not need recomputing per file). On
   the same drive, INT 21h AH=56h renames in place - no data is copied at
   all. Across drives, this falls back to copy_one_file() and only
   deletes the source once the copy has actually succeeded, so a failed
   cross-drive Move never loses the original file. */
int move_confirm_and_move(int idx, char *destDir, int sameDrive)
{
  char srcPath[96];
  char dstPath[96];
  char msg[128];
  char *name;
  int p;
  int key;
  int ok;

  name = &g_name[idx * NAME_LEN];
  build_full_path(srcPath, sizeof(srcPath), name);
  join_dir_name(dstPath, sizeof(dstPath), destDir, name);

  if (path_eq(srcPath, dstPath)) return 1; /* already there; nothing to do */

  if (file_exists(dstPath)) {
    p = 0;
    sappend(msg, &p, MSG(MSG_OVERWRITE_PRE), sizeof(msg));
    sappend(msg, &p, name, sizeof(msg));
    sappend(msg, &p, MSG(MSG_OVERWRITE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
    key = dos_getch();
    if (key == KEY_ESC) return -2;
    if (key != 'y' && key != 'Y') return 0;
    /* AH=56h (rename) fails if the destination name already exists, so
       a confirmed same-drive overwrite must clear it first. The
       cross-drive path doesn't need this - dos_create() inside
       copy_one_file() overwrites an existing file on its own. */
    if (sameDrive) dos_delete(dstPath);
  }

  if (sameDrive) {
    ok = dos_rename(srcPath, dstPath);
  } else {
    ok = copy_one_file(srcPath, dstPath);
    if (ok == 0) dos_delete(srcPath);
  }

  if (ok != 0) {
    p = 0;
    sappend(msg, &p, MSG(MSG_MOVE_ERR_PRE), sizeof(msg));
    sappend(msg, &p, name, sizeof(msg));
    draw_dialog(msg, 0);
    dos_getch();
    return -1;
  }
  return 1;
}

/* C/c: copies the marked files, or (when nothing is marked) the single
   file under the cursor - the same target rule as do_delete(): "marked
   set if non-empty, else the cursor entry", not two cases that can
   disagree. Directories are never in the marked set (mark_cursor()
   refuses them), so the only way one can be "the target" is the
   no-mark/cursor-on-a-directory case; that is caught up front and
   refused with an explicit error dialog, the same as do_delete().
   Unlike the original filer, which refuses a same-name copy outright
   with no confirmation at all (measured; see README's independence
   notes), this asks a Y/N/ESC overwrite question per colliding file
   instead - silently overwriting or silently skipping are both worse
   than asking. A completion dialog is shown on success (also unlike
   Delete, which shows nothing - matching the original's behaviour for
   each of those two commands respectively). */
void do_copy(void)
{
  int i;
  int r;
  int did;
  int aborted;
  char destDir[DEST_MAXLEN + 1];
  char *err;
  int confirmed;
  char msg[64];
  int p;

  if (count_marked() == 0) {
    if (g_count == 0) return;
    if (is_dir_entry(g_cursor)) {
      draw_dialog(MSG(MSG_CM_ERR_HASDIR), 0);
      dos_getch();
      draw_screen();
      return;
    }
  }

  destDir[0] = 0;
  err = 0;
  for (;;) {
    confirmed = input_dialog(MSG(MSG_COPY_PROMPT), destDir, DEST_MAXLEN, err);
    if (!confirmed) { draw_screen(); return; }
    if (destDir[0] == 0) { err = MSG(MSG_CM_ERR_EMPTY); continue; }
    break;
  }

  did = 0;
  aborted = 0;

  if (count_marked() == 0) {
    r = copy_confirm_and_copy(g_cursor, destDir);
    if (r == 1) did = 1;
    if (r == -2) aborted = 1;
  } else {
    for (i = 0; i < g_count; i++) {
      if (!g_marked[i]) continue;
      r = copy_confirm_and_copy(i, destDir);
      if (r == 1) did++;
      if (r == -2) { aborted = 1; break; }
    }
  }

  if (!aborted && did > 0) {
    p = 0;
    sappend(msg, &p, MSG(MSG_COPY_DONE_PRE), sizeof(msg));
    sappend_uint(msg, &p, (unsigned int)did, sizeof(msg));
    sappend(msg, &p, MSG(MSG_COPY_DONE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
    dos_getch();
  }

  read_dir();
  clear_marks();
  if (g_cursor >= g_count) g_cursor = (g_count > 0) ? g_count - 1 : 0;
  draw_screen();
}

/* M/m: moves the marked files, or (when nothing is marked) the single
   file under the cursor - same target rule, same up-front directory
   refusal, same per-file Y/N/ESC overwrite prompt, and same completion
   dialog as do_copy() (see its header comment); the only difference is
   move_confirm_and_move()'s same-drive-rename vs. cross-drive-copy+
   delete choice. Timestamps are always the original file's, never
   "now" - on the same drive that falls out of AH=56h renaming in place;
   across drives, copy_one_file() explicitly carries it over. */
void do_move(void)
{
  int i;
  int r;
  int did;
  int aborted;
  int sameDrive;
  char destDir[DEST_MAXLEN + 1];
  char *err;
  int confirmed;
  char msg[64];
  int p;

  if (count_marked() == 0) {
    if (g_count == 0) return;
    if (is_dir_entry(g_cursor)) {
      draw_dialog(MSG(MSG_CM_ERR_HASDIR), 0);
      dos_getch();
      draw_screen();
      return;
    }
  }

  destDir[0] = 0;
  err = 0;
  for (;;) {
    confirmed = input_dialog(MSG(MSG_MOVE_PROMPT), destDir, DEST_MAXLEN, err);
    if (!confirmed) { draw_screen(); return; }
    if (destDir[0] == 0) { err = MSG(MSG_CM_ERR_EMPTY); continue; }
    break;
  }

  sameDrive = (dest_drive(destDir) == g_path[0]) ? 1 : 0;

  did = 0;
  aborted = 0;

  if (count_marked() == 0) {
    r = move_confirm_and_move(g_cursor, destDir, sameDrive);
    if (r == 1) did = 1;
    if (r == -2) aborted = 1;
  } else {
    for (i = 0; i < g_count; i++) {
      if (!g_marked[i]) continue;
      r = move_confirm_and_move(i, destDir, sameDrive);
      if (r == 1) did++;
      if (r == -2) { aborted = 1; break; }
    }
  }

  if (!aborted && did > 0) {
    p = 0;
    sappend(msg, &p, MSG(MSG_MOVE_DONE_PRE), sizeof(msg));
    sappend_uint(msg, &p, (unsigned int)did, sizeof(msg));
    sappend(msg, &p, MSG(MSG_MOVE_DONE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
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

  /* "." and ".." are not 8.3 name+extension pairs - splitting them on
     '.' puts an empty name and a "." extension, landing ".." at column
     10-11 (the extension slot) with the name column left blank. The
     original shows them unsplit in the name column instead, so they are
     special-cased here before the normal dot search. */
  if (strcmp(rawname, ".") == 0 || strcmp(rawname, "..") == 0) {
    put_str_n(buf, 2, rawname, strlen(rawname));
  } else {
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
  /* "<DIR>" is not a byte count, so the bytes-suffix label ("bytes"/
     "バイト") is only appended for an actual file, matching the
     size column in the file list (build_entry_text) which never shows a
     unit at all next to "<DIR>". */
  if (!(g_attr[g_cursor] & ATTR_DIR)) {
    sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));
  }
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
  buf_color(37);
  {
    int ci;
    int wordCount;

    /* language-independent: see g_cmdWords[]/g_cmdHiPos[] above. The
       visible text this assembles (ignoring the ESC[...]m sequences
       cmdline_put_word() adds around each highlighted key, which are
       never counted as cells - see text_width()/check.py) is verified
       by check.py to stay within CMDLINE_WIDTH cells; it is built here
       exactly the same way check.py reconstructs it: g_cmdWords[0] as
       is, then a single space plus each remaining word. */
    wordCount = sizeof(g_cmdWords) / sizeof(g_cmdWords[0]);
    buf_puts(g_cmdWords[0]);
    for (ci = 1; ci < wordCount; ci++) {
      buf_putc(' ');
      cmdline_put_word(g_cmdWords[ci], g_cmdHiPos[ci]);
    }
  }
  buf_color(37);
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
    } else if (key == 'r' || key == 'R') {
      do_rename();
    } else if (key == 'k' || key == 'K') {
      do_mkdir();
    } else if (key == 'c' || key == 'C') {
      do_copy();
    } else if (key == 'm' || key == 'M') {
      do_move();
    } else if (key == 'q' || key == 'Q' || key == KEY_ESC) {
      running = 0;
    }
  }

  write_str("\x1b[2J");
  write_str("\x1b[>5l"); /* show the text cursor again */
  write_str("\x1b[>1l"); /* restore the function-key line */

  return 0;
}
