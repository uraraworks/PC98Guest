/*
 * FILER.C - PC-98 / FreeDOS(98) directory browser (milestone 6)
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
 * their key letter in reverse video.
 * Milestone 5 replaces the DOS-console/ANSI screen writer with direct
 * text-VRAM writes (see below), draws the header/dialog frames with the
 * original's half-width line-drawing codes instead of full-width ones
 * (BOX_WIDTH grows from 76 to 78 cells accordingly), reproduces the
 * cursor-row/command-key highlight with the VRAM attribute byte's
 * reverse-video bit instead of ESC[7m, and only ever writes the cells
 * that actually changed between frames instead of clearing the whole
 * screen first.
 *
 * Built with SmallerC (C89-ish subset, 16-bit small-model MZ EXE).
 * Milestone 5 moves all screen *content* drawing off the DOS console and
 * onto direct text-VRAM writes (see the vram_* functions below and
 * docs/tvram-measure-01.md / docs/filer-measure-03.md) - this fixes a
 * header frame that measured as invisible (this font has no glyph for
 * the full-width box-drawing characters milestones 1-4 used), a
 * flickering per-frame ESC[2J, and lets the original's own half-width
 * line-drawing codes be used, which the DOS console cannot pass through
 * (it treats 0x81-0x9F as Shift_JIS lead bytes). A few small, purely
 * ASCII pieces (showing/hiding the hardware text cursor, positioning it
 * during input_dialog(), releasing/restoring the bottom function-key
 * line) still go through DOS console ANSI-style escape sequences
 * (measured against WebNP2/FreeDOS(98); see docs/escape-measure-01.md) -
 * none of that carries SJIS text, so none of it is affected by the
 * problem VRAM writes solve. Directory access uses plain DOS INT 21h
 * services (AH=1Ah/4Eh/4Fh/47h/19h/36h), which are documented, generic
 * DOS APIs and not derived from any particular program's source code.
 *
 * Screen output never goes through stdio - printf()/puts()/putchar()
 * must not be used for it.
 *
 * Screen text is bilingual (Japanese / English); see the message
 * table below. Select with the /J (Japanese, default) or /E (English)
 * command line switch.
 *
 * Milestone 6 replaces the milestone-4 command line with the measured
 * real-product bottom row: ten fixed function-key fields (see
 * g_fkeyLabel[]/g_fkeyCol[]/draw_cmdline() and docs/filer-measure-05.md),
 * with F3/F4/F5 kept at the original's own Copy/Delete/Rename positions
 * and the rest of this program's commands placed in unused slots; slots
 * for commands this program does not implement (Logdsk/eXec/Sort/Find/
 * Tree/...) are left blank rather than named. Every command remains
 * reachable by its plain letter key exactly as before - the function
 * keys are an additional entry point. Reading a function key means
 * telling its 0x1B first byte apart from a bare ESC keypress; see
 * dos_kbhit() and main()'s KEY_ESC handling.
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

/* second byte of a PC-98 function-key code (see KEY_ESC above and
 * dos_kbhit() below): DOS INT 21h AH=08h returns a function key as two
 * bytes, 0x1B (ESC) followed by one of these - measured on real hardware
 * with a probe program; see docs/filer-measure-05.md. An ESC keypress by
 * itself is only ever the single byte 0x1B (also measured), so the two
 * cases are told apart by checking dos_kbhit() right after reading
 * 0x1B, never by looking at the second byte's value alone. */
#define FKEY_CODE_F1  'S'
#define FKEY_CODE_F2  'T'
#define FKEY_CODE_F3  'U'
#define FKEY_CODE_F4  'V'
#define FKEY_CODE_F5  'W'
#define FKEY_CODE_F6  'E'
#define FKEY_CODE_F7  'J'
#define FKEY_CODE_F8  'P'
#define FKEY_CODE_F9  'Q'
#define FKEY_CODE_F10 'Z'

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

/* ---- header box (rows 0-5) drawn with half-width box-drawing chars ----
 * Milestone 5: screen output moved to direct text-VRAM writes (see the
 * vram_* functions below), so the half-width single-line box characters
 * (0x9C-0x9F etc.) can be used directly - it was only the DOS console
 * output path (AH=40h through ANSI.SYS) that mangled them by treating
 * 0x81-0x9F as Shift_JIS lead bytes (measured; see
 * docs/filer-measure-03.md). These are ANK (1 screen cell) codes, not
 * SJIS text, so they are plain byte constants here, not C strings - they
 * must never be handed to a CP932-aware function like vram_puts_cells()
 * (which would misparse 0x9C.. as an SJIS lead byte needing a trail
 * byte). All width math below is in screen cells via text_width() for
 * real message text, and by direct column counting for these.
 * ------------------------------------------------------------------- */
#define BOX_WIDTH     78   /* interior width in cells, between the borders */

#define BOXCH_TL      0x9c  /* topleft corner  */
#define BOXCH_TR      0x9d  /* topright corner */
#define BOXCH_BL      0x9e  /* bottomleft corner  */
#define BOXCH_BR      0x9f  /* bottomright corner */
#define BOXCH_H       0x95  /* horizontal line */
#define BOXCH_V       0x96  /* vertical line   */
#define BOXCH_LT      0x93  /* left T (mid separator, left end)  */
#define BOXCH_RT      0x92  /* right T (mid separator, right end) */

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

/* ---- bottom row: PC-98 function-key assignment (language-independent) --
 * Milestone 6 replaces the milestone-4 "key:description" command line
 * with the original's own bottom-row convention, measured directly off a
 * real FD 3.13(98) unit (see docs/filer-measure-05.md): ten fixed 6-cell
 * fields, one per function key F1..F10 (g_fkeyLabel[i] is F(i+1)'s
 * label), left-aligned at the columns measured off the real attribute
 * plane in g_fkeyCol[]. This line is the same in both languages by
 * design - command names are plain English abbreviations, not translated
 * text - so it lives here as ordinary C data, not in g_msgJA/g_msgEN.
 * The old movement/mark/open legend ("Arrow:move SP/Tab:mark ...") is
 * gone: the measured original does not show operation help on this row
 * at all, only command locations, and those operations remain available
 * exactly as before (see main()'s key dispatch) - only where they were
 * *described on screen* changed.
 * An empty g_fkeyLabel[i] ("") means that function key is not
 * implemented yet by this program (e.g. the original's Logdsk/eXec/Sort/
 * Find/Tree/... slots) - its position is still reserved (drawn blank,
 * same as the gaps between fields) rather than reused, so a later
 * milestone can add the command without moving anything else and without
 * this program claiming to offer a command it does not have.
 * g_fkeyHiPos[i] is the index into g_fkeyLabel[i] of the character that
 * is the actual dispatch key in main() - e.g. mKdir's is 'K' at index 1,
 * matching the 'k'/'K' case there; -1 disables the highlight (used only
 * together with an empty label). Every non-empty label's key here is
 * also still reachable as a plain letter key, unchanged from milestone
 * 4/5 - the function keys are an additional entry point, not a
 * replacement. */
#define FKEY_COUNT       10
#define FKEY_FIELD_WIDTH 6

int g_fkeyCol[] = { 4, 11, 18, 25, 32, 42, 49, 56, 63, 70 };

char *g_fkeyLabel[] = {
  "", "", "Copy", "Delete", "Rename", "Move", "mKdir", "", "", "Quit"
};
int g_fkeyHiPos[] = { -1, -1, 0, 0, 0, 0, 1, -1, -1, 0 };

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

/* ---- low level DOS calls (inline asm; see doc/smlrc.md "asm()") ------- */

int dos_getch(void)
{
  asm("mov ah, 8\n"
      "int 0x21\n"
      "mov ah, 0");
}

/* AH=0Bh (DOS console input status): returns AL=0xFF if a character is
   already waiting in the keyboard buffer, AL=0x00 if not (measured on
   real hardware; see docs/filer-measure-05.md). Used right after reading
   a 0x1B (KEY_ESC) from dos_getch() to tell a bare ESC keypress (which
   never has a second byte queued) apart from the first byte of a
   function-key code (whose second byte is measured to already be
   sitting in the buffer at that point - never call this after any other
   keyboard read or delay, or the second byte may already be gone). */
int dos_kbhit(void)
{
  asm("mov ah, 0x0b\n"
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

/* ---- text VRAM (direct screen writes; milestone 5) --------------------
 * Screen output no longer goes through the DOS console (AH=40h + ANSI.SYS
 * escapes) at all for content - it writes straight to the two PC-98 text
 * VRAM planes, char plane at 0xA0000 and attribute plane at 0xA2000, both
 * 80x25 cells x 2 bytes/cell, laid out identically (measured; see
 * docs/tvram-measure-01.md). This fixes three things measured against
 * the real console path: (1) the full-width box-drawing glyphs this
 * program used to draw a header frame with have no glyph in the font
 * used here and drew nothing at all; (2) every redraw sent ESC[2J first,
 * clearing the whole screen before repainting it, which flickers; (3)
 * the original's own half-width line-drawing codes (0x9C-0x9F etc., see
 * BOXCH_* above) cannot be sent through the DOS console because
 * 0x81-0x9F are Shift_JIS lead bytes there - direct VRAM writes have no
 * such interpretation, so they can be used exactly as the original does.
 *
 * ANK (half-width) cells: high byte 0x00, low byte = the character code
 * (this is also how the half-width box-drawing codes above are stored -
 * they are ANK codes, not SJIS). Zenkaku (full-width) cells: the source
 * text here is CP932 (Shift_JIS); VRAM wants JIS X 0208 instead, so
 * sjis_to_jis() converts each pair of SJIS bytes first, then the cell's
 * low byte = (JIS first byte - 0x20), high byte = JIS second byte
 * (measured; see docs/tvram-measure-01.md). A zenkaku character occupies
 * two screen cells; the right-hand cell's content does not affect
 * rendering (also measured), but it is still given a definite value (a
 * blank ANK space) rather than left stale, so a later redraw that only
 * changes what is in the left-hand cell cannot leave old data sitting in
 * the right-hand one where something might later read it.
 *
 * No full-screen clear happens on every redraw. Instead, g_curChar[]/
 * g_curAttr[] mirror what is currently actually sitting in VRAM for each
 * of the 2000 cells; every put-a-cell call compares the new value against
 * that mirror first and only touches hardware (and updates the mirror)
 * when something actually changed. draw_screen_frame() (and everything
 * under it - draw_dialog(), draw_input_box()) always regenerates the
 * *entire* frame's content on every call, cell by cell, through these
 * functions, so this comparison alone is what keeps a redraw from
 * flickering or blanking anything: unchanged cells are simply never
 * written again, and changed ones are updated in place with nothing in
 * between ever going blank.
 * ------------------------------------------------------------------- */

#define VRAM_ROWS  25
#define VRAM_COLS  80
#define VRAM_CELLS (VRAM_ROWS * VRAM_COLS)

/* attribute byte (see docs/tvram-measure-01.md): bits 7-5 = color (GRB),
   b3 = underline (never used - measured rendering bug, shifts the glyph
   4 dots right), b2 = reverse video, b0 = character displayed at all
   (must stay set or the cell goes blank). ATTR_BASE is plain white;
   ATTR_REV is the same color with the cell reversed, used for the
   cursor row and for highlighting a command letter - see README. */
#define ATTR_BASE  0xE1
#define ATTR_REV   0xE5

/* bottom function-key row attributes (row 24 only) - measured off the
 * real product's attribute plane, a different convention from ATTR_REV
 * above: the whole label is reversed AND uses a non-white color, and the
 * single character that is the actual key gets its own (yellow) color on
 * top of that same reverse bit - it is not simply "the key letter
 * reversed, rest plain" the way the old command line worked. See
 * docs/filer-measure-05.md. */
#define ATTR_FKEY_LABEL 0xA5  /* reversed, color 101 - the label text */
#define ATTR_FKEY_KEY   0xC5  /* reversed, color 110 (yellow) - the key char only */

unsigned int g_curChar[VRAM_CELLS];  /* mirrors what is actually in VRAM */
unsigned char g_curAttr[VRAM_CELLS];

/* the only function that actually touches hardware. offset is a byte
   offset into either plane (0, 2, 4, ... 3998 - i.e. cellIndex*2);
   chWord is stored as a 16-bit word (low byte at offset, high byte at
   offset+1, i.e. the plain x86 little-endian store this is), attr's low
   byte is stored as a single byte at the same offset in the attribute
   plane. Segments are loaded through a general register (cx) because
   x86 cannot move an immediate directly into a segment register; es is
   saved/restored around this the same way dos_rename() above saves/
   restores it around int 21h, since the caller cannot be assumed to not
   care what es holds afterward. */
void vram_put_raw(unsigned int offset, unsigned int chWord, unsigned int attr)
{
  asm("mov di, [bp+4]\n"
      "mov ax, [bp+6]\n"
      "push es\n"
      "mov cx, 0xa000\n"
      "mov es, cx\n"
      "mov [es:di], ax\n"
      "mov cx, 0xa200\n"
      "mov es, cx\n"
      "mov al, [bp+8]\n"
      "mov [es:di], al\n"
      "pop es");
}

/* CP932 (Shift_JIS) -> JIS X 0208, one character. Verified against a
   table of 16 kanji/kana measured from the real screen (see the
   milestone 5 commit message / docs) before this was ever used to draw
   anything - not derived from reading any existing conversion table.
   s1/s2 must be a valid SJIS lead/trail byte pair (0x81-0x9F or
   0xE0-0xFC lead, 0x40-0xFC trail excluding 0x7F - the same range
   text_width()/sappend() already assume elsewhere in this file). */
void sjis_to_jis(unsigned char s1, unsigned char s2, unsigned char *j1Out, unsigned char *j2Out)
{
  unsigned char t1;
  unsigned char t2;

  t1 = (s1 <= 0x9f) ? (unsigned char)(s1 - 0x71) : (unsigned char)(s1 - 0xb1);
  t1 = (unsigned char)(t1 * 2 + 1);
  if (s2 >= 0x9f) {
    t2 = (unsigned char)(s2 - 0x7e);
    t1 = (unsigned char)(t1 + 1);
  } else if (s2 >= 0x7f) {
    t2 = (unsigned char)(s2 - 0x20);
  } else {
    t2 = (unsigned char)(s2 - 0x1f);
  }
  *j1Out = t1;
  *j2Out = t2;
}

/* clears the mirror to a state that cannot match any real cell content
   this program ever writes (chWord 0 only ever occurs as a would-be ANK
   NUL, which is never written - see vram_ank()/vram_zenkaku()), so the
   very first frame always writes every cell it touches instead of
   trusting stale BSS zero-init. Called once at startup. */
void vram_shadow_init(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    g_curChar[i] = 0;
    g_curAttr[i] = 0;
  }
}

/* writes one cell if (and only if) it differs from what the mirror says
   is already there. row/col are cell coordinates (0-24 / 0-79); out of
   range is silently ignored (defensive - every caller below already
   stays in range, but a fixed-width field computed from a message that
   somehow ran long must never be allowed to index past VRAM_CELLS). */
void vram_set_cell(int row, int col, unsigned int chWord, unsigned int attr)
{
  unsigned int idx;

  if (row < 0 || row >= VRAM_ROWS || col < 0 || col >= VRAM_COLS) return;
  idx = (unsigned int)(row * VRAM_COLS + col);
  if (g_curChar[idx] == chWord && g_curAttr[idx] == (unsigned char)attr) return;
  vram_put_raw((unsigned int)(idx * 2), chWord, attr);
  g_curChar[idx] = chWord;
  g_curAttr[idx] = (unsigned char)attr;
}

/* one ANK (half-width) cell: high byte 0x00, low byte = code as-is. Used
   both for real ANK text and for the half-width box-drawing codes
   (BOXCH_* above), which are plain byte constants, never C strings. */
void vram_ank(int row, int col, unsigned char code, unsigned int attr)
{
  vram_set_cell(row, col, (unsigned int)code, attr);
}

/* one zenkaku (full-width) character spanning two cells at (row,col) and
   (row,col+1); s1/s2 is the raw CP932 byte pair. See the file-header
   comment above for the cell encoding and why the right-hand cell is
   written too. */
void vram_zenkaku(int row, int col, unsigned char s1, unsigned char s2, unsigned int attr)
{
  unsigned char j1;
  unsigned char j2;
  unsigned int chWord;

  sjis_to_jis(s1, s2, &j1, &j2);
  chWord = (unsigned int)(((unsigned int)j2 << 8) | (unsigned int)(j1 - 0x20));
  vram_set_cell(row, col, chWord, attr);
  vram_ank(row, col + 1, 0x20, attr);
}

/* places a NUL-terminated CP932 string at (row,col), at most 'width'
   screen cells (same SJIS lead-byte rule as text_width()/sappend()
   elsewhere in this file), then pads whatever is left of 'width' with
   blanks - replaces put_str_cells() (which built a byte buffer for the
   old ANSI writer) now that every cell is written straight through this.
   Padding the remainder is required now that there is no more per-frame
   ESC[2J: without it, a shorter string would leave older, longer
   content sitting in the cells past its end. */
void vram_puts_cells(int row, int col, char *s, unsigned int attr, int width)
{
  int i;
  int cell;
  unsigned char c;

  i = 0;
  cell = 0;
  while (s[i] != 0 && cell < width) {
    c = (unsigned char)s[i];
    if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
      if (s[i + 1] != 0) {
        if (cell + 2 > width) break;
        vram_zenkaku(row, col + cell, c, (unsigned char)s[i + 1], attr);
        cell += 2;
        i += 2;
      } else {
        vram_ank(row, col + cell, c, attr); /* truncated lead byte: ANK fallback */
        cell += 1;
        i += 1;
      }
    } else {
      vram_ank(row, col + cell, c, attr);
      cell += 1;
      i += 1;
    }
  }
  while (cell < width) {
    vram_ank(row, col + cell, 0x20, attr);
    cell++;
  }
}

/* blanks every one of the 2000 cells to a plain white space - used once
   at startup (to get rid of whatever the boot/DOS prompt left behind
   before the first frame is drawn) and once at exit (to leave the
   screen in a state DOS can use again). Both go through vram_set_cell(),
   so like everything else here this only actually writes the cells that
   need it. */
void vram_clear_all(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    vram_set_cell(i / VRAM_COLS, i % VRAM_COLS, 0x0020, ATTR_BASE);
  }
}

/* positions the DOS/BIOS text cursor (the blinking hardware cursor,
   shown only during input_dialog() - see its "\x1b[>5l"/"\x1b[>5h"
   calls) via a plain ANSI CUP escape. This is unrelated to the SJIS/
   half-width-box problem the vram_* functions above solve - it carries
   no text, only ASCII digits - so it is still sent through the DOS
   console exactly like write_str() below, rather than through VRAM. */
void ansi_goto(int row, int col)
{
  char out[16];
  char numbuf[6];
  int p;
  int i;

  p = 0;
  out[p++] = 0x1b;
  out[p++] = '[';
  format_u32_plain(0, (unsigned int)(row + 1), numbuf);
  for (i = 0; numbuf[i] != 0; i++) out[p++] = numbuf[i];
  out[p++] = ';';
  format_u32_plain(0, (unsigned int)(col + 1), numbuf);
  for (i = 0; numbuf[i] != 0; i++) out[p++] = numbuf[i];
  out[p++] = 'H';
  out[p] = 0;
  write_str(out);
}

/* draws one row of the header box: left border char + content
   (space-padded/truncated to BOX_WIDTH cells) + right border char. */
void box_row(int row, unsigned char lb, unsigned char rb, char *content)
{
  vram_ank(row, 0, lb, ATTR_BASE);
  vram_puts_cells(row, 1, content, ATTR_BASE, BOX_WIDTH);
  vram_ank(row, 1 + BOX_WIDTH, rb, ATTR_BASE);
}

/* draws a header-box separator row: border char, BOX_WIDTH horizontal
   line cells, border char. Replaces build_dash_row() + box_row(), which
   used to build a BOX_WIDTH-cell string of the (2-cell, full-width)
   horizontal line character first - now that the line character is a
   1-cell ANK code, and ANK codes cannot be handed to vram_puts_cells()
   (see the file-header comment above), the cells are written directly. */
void box_dash_row(int row, unsigned char lb, unsigned char rb)
{
  int i;

  vram_ank(row, 0, lb, ATTR_BASE);
  for (i = 0; i < BOX_WIDTH; i++) vram_ank(row, 1 + i, BOXCH_H, ATTR_BASE);
  vram_ank(row, 1 + BOX_WIDTH, rb, ATTR_BASE);
}

/* draws the header box's top border row (row 0): corners, horizontal
   line, the title message, horizontal line, filling exactly BOX_WIDTH
   interior cells. Width is computed in cells via text_width(), never
   assumed, so this adapts to either language table without hardcoding a
   length. Replaces build_title_row() + box_row() for the same reason as
   box_dash_row() above - BOXCH_H is now an ANK code, not SJIS text, so
   it cannot be mixed into a plain string with the (real, CP932) title
   text and handed to vram_puts_cells() in one call. */
void draw_title_row(void)
{
  char *title;
  int titleCells;
  int used;
  int fillCells;
  int fillPairs;
  int i;
  int col;

  vram_ank(ROW_TITLE, 0, BOXCH_TL, ATTR_BASE);
  vram_ank(ROW_TITLE, 1 + BOX_WIDTH, BOXCH_TR, ATTR_BASE);

  title = MSG(MSG_TITLE);
  titleCells = text_width(title);
  used = 2 + 1 + titleCells + 1; /* "--" + " " + title + " " */
  fillCells = BOX_WIDTH - used;
  if (fillCells < 0) fillCells = 0; /* defensive: title too wide to fit */
  fillPairs = fillCells / 2;

  col = 1;
  vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BASE); col++;
  vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BASE); col++;
  vram_ank(ROW_TITLE, col, ' ', ATTR_BASE); col++;
  vram_puts_cells(ROW_TITLE, col, title, ATTR_BASE, titleCells);
  col += titleCells;
  vram_ank(ROW_TITLE, col, ' ', ATTR_BASE); col++;
  for (i = 0; i < fillPairs; i++) { vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BASE); col++; }
  if ((fillCells % 2) == 1) { vram_ank(ROW_TITLE, col, ' ', ATTR_BASE); col++; }
}

/* immediate one-off write, used only for the pre/post-frame terminal mode
   escapes in main() (not part of a screen frame) and by ansi_goto()
   above. */
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
   the prompt, and optionally an error line under it). Every cell this
   touches goes straight through vram_set_cell()'s "skip if unchanged"
   check (see the vram_* section above), so this never blanks anything -
   the underlying list stays visible right up until the exact cells the
   dialog box occupies change. This is the "no dedicated bottom status
   line; errors appear inside the dialog, prompt stays up" behaviour
   measured from the original - see the milestone doc. errmsg may be 0
   for "no error line". */
#define DIALOG_WIDTH  60
#define DIALOG_ROW    10
#define DIALOG_COL    8

void draw_dialog(char *msg, char *errmsg)
{
  int i;
  int row;

  draw_screen_frame();

  row = DIALOG_ROW;
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BASE);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BASE);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BASE);
  row++;

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BASE);
  vram_puts_cells(row, DIALOG_COL + 1, msg, ATTR_BASE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BASE);
  row++;

  if (errmsg != 0) {
    vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BASE);
    vram_puts_cells(row, DIALOG_COL + 1, errmsg, ATTR_BASE, DIALOG_WIDTH);
    vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BASE);
    row++;
  }

  vram_ank(row, DIALOG_COL, BOXCH_BL, ATTR_BASE);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BASE);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_BR, ATTR_BASE);
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
  char line[DIALOG_WIDTH * 2 + NAME_LEN + 4];
  int i;
  int row;
  int fieldRow;
  int fieldCol;
  int p;
  int promptCells;

  draw_screen_frame();

  row = DIALOG_ROW;
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BASE);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BASE);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BASE);
  row++;

  p = 0;
  sappend(line, &p, prompt, sizeof(line));
  promptCells = text_width(line);
  buf[len] = 0; /* line/sappend below need a NUL-terminated C string */
  sappend(line, &p, buf, sizeof(line));

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BASE);
  vram_puts_cells(row, DIALOG_COL + 1, line, ATTR_BASE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BASE);
  fieldRow = row;
  row++;

  if (errmsg != 0) {
    vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BASE);
    vram_puts_cells(row, DIALOG_COL + 1, errmsg, ATTR_BASE, DIALOG_WIDTH);
    vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BASE);
    row++;
  }

  vram_ank(row, DIALOG_COL, BOXCH_BL, ATTR_BASE);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BASE);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_BR, ATTR_BASE);

  /* BOXCH_V is now a 1-cell ANK border character, so the field text
     starts 1 cell past the border column (this used to be 2, when the
     border was a 2-cell full-width character). */
  fieldCol = DIALOG_COL + 1 + promptCells + len;
  ansi_goto(fieldRow, fieldCol);
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

/* draws the bottom row (row 24) as the measured PC-98 function-key
   assignment: the whole row is blanked to ATTR_BASE first (this is what
   the 1-cell gaps between fields, the 4-cell F5/F6 gap, and any reserved
   (unimplemented) field end up showing - see docs/filer-measure-05.md's
   0xE1 "outside a field" measurement), then each non-empty g_fkeyLabel[i]
   is placed at g_fkeyCol[i], roughly centered within FKEY_FIELD_WIDTH
   cells (left-biased when the label's length is odd, same as an integer
   divide - the original's own centering is not exact either). Every cell
   of the label uses ATTR_FKEY_LABEL (reversed) except the single
   character at g_fkeyHiPos[i], which uses ATTR_FKEY_KEY instead (still
   reversed, different color) - matching the measured "reverse the whole
   label, recolor only the key letter" convention, not the milestone 4/5
   "reverse only the key letter" one. Labels are plain ASCII (see
   g_fkeyLabel[]), so this indexes by byte, not by a SJIS-aware cell
   count. check.py verifies every g_fkeyLabel[] entry fits
   FKEY_FIELD_WIDTH cells and that every g_fkeyCol[]/FKEY_FIELD_WIDTH
   field stays inside VRAM_COLS. */
void draw_cmdline(void)
{
  int i;
  int col;
  int len;
  int pad;
  int c;
  char *label;

  for (col = 0; col < VRAM_COLS; col++) {
    vram_ank(ROW_CMD, col, ' ', ATTR_BASE);
  }

  for (i = 0; i < FKEY_COUNT; i++) {
    label = g_fkeyLabel[i];
    len = (int)strlen(label);
    if (len == 0) continue; /* reserved (not implemented yet): leave blank */
    pad = (FKEY_FIELD_WIDTH - len) / 2;
    if (pad < 0) pad = 0;
    for (c = 0; c < len && (pad + c) < FKEY_FIELD_WIDTH; c++) {
      vram_ank(ROW_CMD, g_fkeyCol[i] + pad + c, (unsigned char)label[c],
                (c == g_fkeyHiPos[i]) ? ATTR_FKEY_KEY : ATTR_FKEY_LABEL);
    }
  }
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

  visibleCount = g_count;
  if (visibleCount > VISIBLE_MAX) visibleCount = VISIBLE_MAX;
  leftCount = (visibleCount > LEFT_ROWS) ? LEFT_ROWS : visibleCount;
  rightCount = visibleCount - leftCount;

  /* No full-screen clear here (see the vram_* section above): every row
     below is fully regenerated on every call, so leftover content is
     only ever a stale value in a cell this frame does *not* revisit -
     which the two "else" branches in the entries loop, and the
     always-run trailing fill in draw_cmdline(), exist specifically to
     rule out. header box, rows 0-5: half-width box-drawing borders. */
  draw_title_row();

  draw_disk_line();

  box_dash_row(ROW_SEP1, BOXCH_LT, BOXCH_RT);

  draw_path_line();

  draw_info_line(visibleCount);

  box_dash_row(ROW_SEP2, BOXCH_BL, BOXCH_BR);

  for (row = 0; row < LEFT_ROWS; row++) {
    leftIdx = row;
    rightIdx = LEFT_ROWS + row;

    if (leftIdx < leftCount) {
      build_entry_text(leftIdx, entrybuf);
      vram_puts_cells(ROW_LIST_TOP + row, COL_LEFT, entrybuf,
                       (leftIdx == g_cursor) ? ATTR_REV : ATTR_BASE, 39);
    } else {
      /* nothing here now - blank it explicitly; a shrunk directory
         listing (after Delete, or moving into a smaller directory) must
         not leave a previous frame's row sitting here, now that there is
         no per-frame ESC[2J to have done that for free. */
      vram_puts_cells(ROW_LIST_TOP + row, COL_LEFT, "", ATTR_BASE, 39);
    }
    if (rightIdx < visibleCount) {
      build_entry_text(rightIdx, entrybuf);
      vram_puts_cells(ROW_LIST_TOP + row, COL_RIGHT, entrybuf,
                       (rightIdx == g_cursor) ? ATTR_REV : ATTR_BASE, 39);
    } else {
      vram_puts_cells(ROW_LIST_TOP + row, COL_RIGHT, "", ATTR_BASE, 39);
    }
  }

  draw_cmdline();
}

void draw_screen(void)
{
  draw_screen_frame();
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

  vram_shadow_init();
  vram_clear_all(); /* one-time clear of whatever the boot/DOS prompt left
                        behind; every redraw after this only ever touches
                        the cells that actually change - see the vram_*
                        section above. */

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
    } else if (key == 'q' || key == 'Q') {
      running = 0;
    } else if (key == KEY_ESC) {
      /* 0x1B alone is ESC; 0x1B immediately followed by a queued second
         byte is a function key - see dos_kbhit()'s comment and
         docs/filer-measure-05.md. Must check dos_kbhit() before any
         other keyboard read. */
      if (dos_kbhit()) {
        key = dos_getch();
        if (key == FKEY_CODE_F3) {
          do_copy();
        } else if (key == FKEY_CODE_F4) {
          do_delete();
        } else if (key == FKEY_CODE_F5) {
          do_rename();
        } else if (key == FKEY_CODE_F6) {
          do_move();
        } else if (key == FKEY_CODE_F7) {
          do_mkdir();
        } else if (key == FKEY_CODE_F10) {
          running = 0;
        }
        /* F1/F2/F8/F9 (and anything else): no command assigned yet -
           see g_fkeyLabel[]'s reserved ("") entries - so ignored. */
      } else {
        running = 0; /* bare ESC: quit, same as Q */
      }
    }
  }

  /* leave the screen in a state DOS can use again: clear it via the same
     direct VRAM write everything else in this program uses (not
     ESC[2J/ANSI.SYS - this program never set the DOS console's own
     notion of the current color, so asking it to clear "with the
     current attribute" would be relying on state nothing here ever set)
     and re-home the cursor before showing it. */
  vram_clear_all();
  ansi_goto(0, 0);
  write_str("\x1b[>5l"); /* show the text cursor again */
  write_str("\x1b[>1l"); /* restore the function-key line */

  return 0;
}
