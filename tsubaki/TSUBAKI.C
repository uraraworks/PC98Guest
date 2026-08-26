/*
 * TSUBAKI.C - PC-98 / FreeDOS(98) 用テキストエディタ（第0マイルストーン v0）
 *
 * ゼロから独立に実装したもの。独立性についての宣言はこのディレクトリの
 * README.md を参照。画面表示文字列はすべてこのプロジェクトのために
 * 書き下ろしたオリジナルの文言であり、既存製品のいかなる文字列も再現
 * していない。
 *
 * 設計は docs/tsubaki-spec-01.md（唯一の仕様）に従う。カーソル移動の
 * 実測根拠は docs/editor-measure-01.md / docs/editor-measure-02.md。
 *
 * v0 の範囲：ファイルを開く・新規・保存・名前を変えて保存・終了
 * （未保存確認つき）、カーソル移動（文字単位。全角の2セル目には
 * 止まらない）、文字入力（ANKとSJIS2バイト）・改行・BS・DEL・行削除、
 * 挿入／上書きの切替、横スクロール、日英切替（/J /E）。
 * 入れないもの（v1以降）：検索・置換、Undo、ブロック編集／クリップ
 * ボード、ウィンドウ分割、引数なし起動時のファイル選択、BAK、XMS/EMS
 * 退避、オートインデント、CFG。
 *
 * DOS/BIOS呼び出し・VRAM直書き層・ダイアログ機構・メッセージテーブル
 * 機構は、同じプロジェクトの guest/sumire/SUMIRE.C（ファイラ）から
 * 土台部分（下記参照）をコピーして流用している（同一プロジェクト・
 * 同一MITライセンスのため）。ファイラ固有の関数（ディレクトリ走査・
 * コピー／移動・実行など）はコピーしていない。エディタ本体（バッファ・
 * 行表・カーソル・描画・キー処理・ファイル入出力）はすべて新規に
 * 書き下ろした。
 *
 * 画面出力は一切 stdio を経由しない。printf()/puts()/putchar() を
 * 画面表示に使ってはならない。文字コード入出力もSUMIRE.C同様に
 * バイト単位のDOSファイルI/Oで行う。
 *
 * SmallerC（C89寄りのサブセット、16ビット・スモールモデルの MZ EXE）で
 * ビルド。
 */

#include <string.h>

/* ---- 定数 ------------------------------------------------------ */

#define TEXT_MAX    24576  /* g_text[] のバイト数上限（24KB）。docs/tsubaki-spec-01.md 2章 */
#define MAX_LINES   1500   /* g_lineStart[] の要素数上限（3KB） */
#define IOBUF_SIZE  4096   /* ファイル読み書き用の共有バッファ */
#define FILENAME_MAX 60    /* g_filename[] に入れられる最大文字数（NULを含まない） */
#define TAB_WIDTH   8      /* タブストップ幅。未実測の暫定既定値（設計書8章のTODO参照） */

/* ---- 画面レイアウト（25行80桁・全セルを覆う） -------------------- */
#define VRAM_ROWS   25
#define VRAM_COLS   80
#define VRAM_CELLS  (VRAM_ROWS * VRAM_COLS)

#define ROW_STATUS  0      /* ステータス行（ファイル名もここへ統合し、白帯で80セル全部を覆う） */
#define BODY_TOP    1       /* 本文の最初の行（ファイル名専用行を廃止して1行繰り上げた） */
#define BODY_ROWS   23      /* 本文の行数（1～23行目） */
#define ROW_FKEY    24      /* ファンクションキー行 */

/* ステータス行（行0）の内訳。左から並べた幅の合計がちょうど
   VRAM_COLS(80)になるようにする（check.pyで検査）：
     STATUS_SHARED_WIDTH(48) … ファイル名欄と通知欄を共有する1つの
                                 フィールド。通知が空なら「ファイル名
                                 （＋変更マーク'*'）＋ STATUS_SUFFIX_
                                 WIDTH(10)セルの" - Tsubaki"」を、通知
                                 があれば通知g_noticeを描く（利用者の
                                 提案：プログラム名は固定なので、変わる
                                 ファイル名を先に出し" - Tsubaki"で
                                 締める並びにした）。" - Tsubaki"は必ず
                                 全体を表示するため、ファイル名に使える
                                 幅はSTATUS_SHARED_WIDTHから
                                 STATUS_SUFFIX_WIDTH(10)ぶん差し引いた
                                 残りになる。ファイル名が収まらないとき
                                 は先頭を'<'に置き換えて末尾を残す
                                 （省略するのはファイル名部分だけで、
                                 " - Tsubaki"は削らない）。全角文字の
                                 途中では区切らない。既存の最長通知
                                 メッセージ（全角14文字=28セル）は
                                 48セルに余裕で収まる
   + STATUS_RIGHT_WIDTH(32)  … 行:桁／モード／タブ幅（従来と同じ）
   = 80 */
#define STATUS_SUFFIX_TITLE_WIDTH 7   /* " - Tsubaki"のうちプログラム名「Tsubaki」部分の幅 */
#define STATUS_SUFFIX_WIDTH       10  /* " - " (3) + STATUS_SUFFIX_TITLE_WIDTH (7) = 10 */
#define STATUS_SHARED_WIDTH 48
#define STATUS_RIGHT_WIDTH  32
#define STATUS_SHARED_COL 0

#define DIALOG_WIDTH 60
#define DIALOG_ROW   10
#define DIALOG_COL   8

#define FKEY_COUNT       10
#define FKEY_FIELD_WIDTH 6

/* ---- キーコード（下の key_read() が返す値） -----------------------
 * INT 18h AH=00hはAH=スキャンコード、AL=文字コードを返し、文字を
 * 持たないキーではAL=0になる（すみれで実測済み。
 * docs/key-measure-01.md、docs/key-measure-02.md、
 * docs/bios-key-measure-01.md参照）。key_read()自身がAL／スキャンコードの
 * 変換を行い、次のように返す：
 *   - ALが0でなければALをそのまま返す（下のBS/TAB/ENTER/ESC/各種
 *     ^キーは、この経路で来る素のASCII制御コード）。
 *   - ALが0（文字を持たない）のときは、スキャンコードから変換した
 *     KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT/KEY_HOME/KEY_ROLLUP/
 *     KEY_ROLLDOWN/KEY_F1..KEY_F10のいずれかを返す。
 * すみれのKEY_LEFTは0x08（生のBSと同じ値）に定義していたため、BSキーと
 * 左矢印キーがdos_getch()の戻り値レベルで区別できなかった（実測済み。
 * docs/tsubaki-spec-01.md 5章参照）。つばきは本文編集中にBS（前1文字
 * 削除）と左矢印（カーソル移動）を区別する必要があるため、この
 * ファイルでは意図的にすみれと異なる割り当てにする：スキャンコード
 * 由来の疑似コードはすべて0xFFを超える値にし、生のALバイト値
 * （0x00-0xFF）と絶対に衝突しないようにした。 */
#define KEY_UP       0x100
#define KEY_DOWN     0x101
#define KEY_LEFT     0x102
#define KEY_RIGHT    0x103
#define KEY_HOME     0x104
#define KEY_ROLLUP   0x105  /* 次画面。スキャンコード0x36（実測。docs/bios-key-measure-01.md） */
#define KEY_ROLLDOWN 0x106  /* 前画面。スキャンコード0x37（同上） */
#define KEY_F1       0x107
#define KEY_F2       0x108
#define KEY_F3       0x109
#define KEY_F4       0x10a
#define KEY_F5       0x10b
#define KEY_F6       0x10c
#define KEY_F7       0x10d
#define KEY_F8       0x10e
#define KEY_F9       0x10f
#define KEY_F10      0x110

#define KEY_BS       0x08
#define KEY_TAB      0x09
#define KEY_ENTER    0x0d
#define KEY_ESC      0x1b

/* Ctrlキー各種。DEL専用のスキャンコードは未実測（設計書8章のTODO）
 * のため、DEL相当の操作は^Dのみに割り当てる。 */
#define KEY_CTRL_A   0x01  /* 行頭 */
#define KEY_CTRL_B   0x02  /* 文末 */
#define KEY_CTRL_D   0x04  /* カーソル位置の1文字削除（DEL相当） */
#define KEY_CTRL_E   0x05  /* 行末 */
#define KEY_CTRL_G   0x07  /* 指定行番号へ */
#define KEY_CTRL_Q   0x11  /* 終了 */
#define KEY_CTRL_T   0x14  /* 文頭 */
#define KEY_CTRL_V   0x16  /* 挿入／上書き切替 */
#define KEY_CTRL_Y   0x19  /* カーソル行削除 */

/* ---- VRAM属性（すみれで実機実測。docs/tvram-measure-01.md / filer-measure-06.md参照） -- */
#define ATTR_BASE   0xE1
#define ATTR_BORDER 0xA1
#define ATTR_TITLE  0xC1
#define ATTR_VALUE  0xE1

#define ATTR_FKEY_LABEL 0xA5  /* 反転、色101（すみれのファンクションキー行と同じ慣習） */
#define ATTR_FKEY_KEY   0xC5  /* 反転、色110（黄色） */

/* ステータス行（行0）専用：白地に黒文字の帯（すみれのATTR_REVと同じ
   0xE5＝色111(白)+反転ビット。実機実測：行0は白255,254,255が10240画素中
   8897画素(87%)を占める白帯だった。ファイル名行はこの行に統合したため、
   行0の80セル全部にこの属性を使う。行1以降の本文はATTR_BASEを使う）。 */
#define ATTR_STATUS 0xE5

#define BOXCH_V  0x96  /* 垂直線（半角罫線コード。ANKでありSJISではない） */
#define BOXCH_TL 0x9c
#define BOXCH_TR 0x9d
#define BOXCH_BL 0x9e
#define BOXCH_BR 0x9f
#define BOXCH_H  0x95

/* ---- 低レベルなDOS/BIOS呼び出し（インラインアセンブラ） ----------------
 * ここから dos_write() までは guest/sumire/SUMIRE.C の同名関数
 * （bios_getch_raw～dos_write、行621付近～1013行付近）をそのまま流用した。
 * ファイラ固有の関数（dos_findfirst/dos_findnext/dos_delete/dos_mkdir/
 * dos_rename/dos_exec/get_ds/dos_diskfree/dos_getdrive/dos_getcwd/
 * dos_getdate/dos_gettime等）はこのプログラムでは使わないためコピー
 * していない。 */

int bios_getch_raw(int *scan)
{
  asm("mov ah, 0\n"
      "int 0x18\n"
      "mov bl, ah\n"
      "mov bh, 0\n"
      "mov si, [bp+4]\n"
      "mov [si], bx\n"
      "mov ah, 0");
}

void dos_setdta(unsigned char *dta)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x1a\n"
      "int 0x21");
}

/* ---- ファイルI/O（INT 21h AH=3Dh/3Ch/3Fh/40h/3Eh）。すみれのCopy/Move用
 * ラッパをそのまま流用。"jnc LABEL / mov ax,-1 / LABEL:" の形を使うのは
 * 成功時の戻り値が単純な0ではなく、ハンドルやバイト数だから。各ラベル名は
 * このファイル全体で重複が無いようにしてある。 ------------------------- */

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

int dos_close(unsigned int handle)
{
  asm("mov bx, [bp+4]\n"
      "mov ah, 0x3e\n"
      "int 0x21\n"
      "sbb ax, ax");
}

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

/* buf から始まる len バイトを INT 21h AH=40h で標準出力（ハンドル1）へ
   書き込む。画面への一時的な直接書き込み（ANSI風エスケープ）専用。 */
void dos_write(char *buf, unsigned int len)
{
  asm("mov dx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov bx, 1\n"
      "mov ah, 0x40\n"
      "int 0x21");
}

/* その場限りの即時書き込み。カーソル表示切替とansi_goto()だけで使う。 */
void write_str(char *s)
{
  dos_write(s, (unsigned int)strlen(s));
}

/* INT 18h AH=01hはここでは使わない（つばきのkey_read()は常にブロック
   待ちでよく、すみれのようなアイドル中の時計更新が無いため）。 */

/* AL != 0 のときはそのまま返し、AL == 0 のときはスキャンコードから
   本ファイル独自の疑似コード（KEY_UP等、上のコメント参照）へ変換する。 */
int key_read(void)
{
  int c;
  int scan;

  c = bios_getch_raw(&scan);
  if (c != 0) return c;

  if (scan == 0x3a) return KEY_UP;
  if (scan == 0x3d) return KEY_DOWN;
  if (scan == 0x3b) return KEY_LEFT;
  if (scan == 0x3c) return KEY_RIGHT;
  if (scan == 0x3e) return KEY_HOME;
  if (scan == 0x36) return KEY_ROLLUP;
  if (scan == 0x37) return KEY_ROLLDOWN;
  if (scan == 0x62) return KEY_F1;
  if (scan == 0x63) return KEY_F2;
  if (scan == 0x64) return KEY_F3;
  if (scan == 0x65) return KEY_F4;
  if (scan == 0x66) return KEY_F5;
  if (scan == 0x67) return KEY_F6;
  if (scan == 0x68) return KEY_F7;
  if (scan == 0x69) return KEY_F8;
  if (scan == 0x6a) return KEY_F9;
  if (scan == 0x6b) return KEY_F10;
  return 0; /* 未割当（HELP/INS/DELなど）。DELのスキャンコードは未実測 */
}

/* ---- 小さなテキストヘルパー（SUMIRE.Cから流用。行1014～1433付近の
   一部）。umul32/divmod10/mul32x16/sub32/format_u32/format_u32_plain/
   format_date/format_time/format_attr/sappend_field_rj/put_str_n は
   このプログラムでは使わないためコピーしていない。 ------------------- */

/* 画面セル単位での表示幅：SJISの先頭バイトは2、それ以外は1として数える。 */
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
        w += 1;
        i += 1;
      }
    } else {
      w += 1;
      i += 1;
    }
  }
  return w;
}

void put_str_n(char *buf, int col, char *s, int maxlen)
{
  int i;

  i = 0;
  while (s[i] != 0 && i < maxlen) {
    buf[col + i] = s[i];
    i++;
  }
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
  buf[col + width] = 0;
}

/* NUL終端された文字列sをdstのバイトオフセット*lenpに追記し、改めて
   終端する。2バイトのSJIS文字を半分だけ書くことは決してしない。 */
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
      chBytes = (s[i + 1] != 0) ? 2 : 1;
    } else {
      chBytes = 1;
    }
    if (lp + chBytes > cap - 1) break;
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

/* ---- テキストVRAM（画面への直接書き込み。SUMIRE.Cから流用。
   行1435～1741付近） -------------------------------------------------- */

unsigned int g_curChar[VRAM_CELLS];
unsigned char g_curAttr[VRAM_CELLS];

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

/* CP932（Shift_JIS）→JIS X 0208、1文字ぶん。すみれで実機実測済みの
   変換（docs/tvram-measure-01.md参照）をそのまま流用。 */
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

void vram_shadow_init(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    g_curChar[i] = 0;
    g_curAttr[i] = 0;
  }
}

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

void vram_ank(int row, int col, unsigned char code, unsigned int attr)
{
  vram_set_cell(row, col, (unsigned int)code, attr);
}

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
        vram_ank(row, col + cell, c, attr);
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

void vram_clear_all(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    vram_set_cell(i / VRAM_COLS, i % VRAM_COLS, 0x0020, ATTR_BASE);
  }
}

/* DOS/BIOSのテキストカーソルを普通のANSI CUPエスケープで位置決めする。
   すみれのinput_dialog()と異なり、つばきは編集中つねにカーソルを
   表示したままにする（main()参照）ので、ここでは表示/非表示の切替は
   行わない。 */
void ansi_goto(int row, int col)
{
  char out[16];
  char numbuf[6];
  int p;
  int i;

  p = 0;
  out[p++] = 0x1b;
  out[p++] = '[';
  numbuf[0] = 0;
  {
    int lp;
    lp = 0;
    sappend_uint(numbuf, &lp, (unsigned int)(row + 1), sizeof(numbuf));
  }
  for (i = 0; numbuf[i] != 0; i++) out[p++] = numbuf[i];
  out[p++] = ';';
  {
    int lp;
    lp = 0;
    sappend_uint(numbuf, &lp, (unsigned int)(col + 1), sizeof(numbuf));
  }
  for (i = 0; numbuf[i] != 0; i++) out[p++] = numbuf[i];
  out[p++] = 'H';
  out[p] = 0;
  write_str(out);
}

/* ---- 言語／メッセージテーブル -----------------------------------------
 * 画面表示文字列はすべてここに集約し、インデックスで引く。すみれと
 * 同じ設計（docs/i18n-design.md参照）。 */

#define LANG_JA 0
#define LANG_EN 1

#define MSG_UNTITLED      0  /* ファイル名未設定時の表示 */
#define MSG_MODE_INS      1  /* ステータス行：挿入モード */
#define MSG_MODE_OVR      2  /* ステータス行：上書きモード */
#define MSG_GOTO_PROMPT   3  /* ^G：行番号入力ダイアログのプロンプト */
#define MSG_SAVEAS_PROMPT 4  /* F3：名前を変えて保存のプロンプト */
#define MSG_QUIT_CONFIRM  5  /* F10/^Q：未保存の変更がある終了確認 */
#define MSG_SAVE_FAIL     6  /* 保存失敗ダイアログ */
#define MSG_LOAD_TOO_LARGE 7 /* 起動時の読み込み：容量超過 */
#define MSG_LOAD_TOO_MANY_LINES 8 /* 起動時の読み込み：行数超過 */
#define MSG_LIMIT_NOTICE  9  /* 編集中に容量／行数上限へ達したときのステータス行通知 */
#define MSG_SAVEAS_ERR_EMPTY   10 /* F3：保存名が空のときのエラー（同じダイアログへ戻って打ち直させる） */
#define MSG_SAVEAS_ERR_INVALID 11 /* F3：保存名がDOSの8.3形式に収まらない、または禁止文字を含むときのエラー */
#define MSG_SAVEAS_ERR_FAILED  12 /* F3：save_file()失敗時のエラー（原因は断定しない――書込禁止とは限らないため） */

const char *g_msgJA[] = {
  "無題",
  "挿入",
  "上書き",
  "行番号: ",
  "保存名: ",
  "保存されていない変更があります。破棄して終了しますか (Y/N)",
  "保存に失敗しました（何かキーを押してください）",
  "ファイルが大きすぎます（何かキーを押してください）",
  "行数が多すぎます（何かキーを押してください）",
  "上限のため入力を無視しました",
  "名前が空です",
  "名前が不正です",
  "保存に失敗しました"
};

const char *g_msgEN[] = {
  "Untitled",
  "INS",
  "OVR",
  "Line No: ",
  "Save as: ",
  "Unsaved changes. Discard and quit? (Y/N)",
  "Save failed (press any key)",
  "File is too large (press any key)",
  "Too many lines (press any key)",
  "Ignored: limit reached",
  "Name is empty",
  "Invalid name",
  "Save failed"
};

const char **g_msgTables[2] = { g_msgJA, g_msgEN };

int g_lang = LANG_JA;

#define MSG(id) (g_msgTables[g_lang][id])

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

/* タイトル（製品名。ステータス行の"ファイル名 - Tsubaki"の末尾）：
   g_msgJA/g_msgENの外に置く言語非依存の固定文字列。すみれのg_titleと
   同じ扱い。STATUS_SUFFIX_TITLE_WIDTH(7)しか使えないため、版表記は
   付けず"Tsubaki"の7文字ちょうどにした。 */
char *g_title = "Tsubaki";

/* ---- 最下段：ファンクションキー割り当て（言語非依存） --------------
 * すみれのg_fkeyLabel[]/g_fkeyCol[]/g_fkeyHiPos[]/draw_cmdline()と
 * 同じ仕組みを流用（コマンド名は英語の略称であって翻訳対象の文言
 * ではないという設計）。桁位置はすみれと同じ配置を再利用している
 * （同一プロジェクト内の見た目の統一のためであり、実機の実測に
 * 基づくものではない）。空文字列のフィールドはこのプログラムが
 * まだ実装していないキー（F1/F4-F9）で、将来のマイルストーンのために
 * 予約してある。 */
int g_fkeyCol[] = { 4, 11, 18, 25, 32, 42, 49, 56, 63, 70 };

char *g_fkeyLabel[] = {
  "", "Save", "SaveAs", "", "", "", "", "", "", "Quit"
};
int g_fkeyHiPos[] = { -1, 0, 0, -1, -1, -1, -1, -1, -1, 0 };

/* 桁colがどのファンクションキー枠（g_fkeyCol[i]～+FKEY_FIELD_WIDTH-1）に
   属するかを求める。属する枠が無ければ-1。枠同士が重ならないことは
   check.pyが検査済み。 */
int fkey_field_at(int col)
{
  int i;

  for (i = 0; i < FKEY_COUNT; i++) {
    if (col >= g_fkeyCol[i] && col < g_fkeyCol[i] + FKEY_FIELD_WIDTH) return i;
  }
  return -1;
}

/* すみれのdraw_title_row()第7マイルストーンと同じ理由（関数コメント
   参照）で、「まず行全体を空白で塗ってからラベルを上書きする」形は
   採らない。塗りとラベル描画を2回に分けると、差分更新のシャドウ越し
   でも1セルにつき2回書き込みが発生し、行24が1～2フレームだけ暗転して
   見える（実測：カーソルキー4回で108フレーム中dipが6回発生し、
   キーを押さないときは92フレーム中0回だった）。ここでは各セルを
   1フレームに1回だけ書くため、桁ごとにどの枠に属するかを先に判定し、
   枠内ならラベル文字、枠外ならATTR_BASEの空白を、それぞれ1回の
   vram_ank()呼び出しで書く。 */
void draw_fkey_row(void)
{
  int col;
  int i;
  int len;
  int pad;
  int c;
  char *label;
  unsigned char ch;
  unsigned int attr;

  for (col = 0; col < VRAM_COLS; col++) {
    i = fkey_field_at(col);
    if (i < 0) {
      vram_ank(ROW_FKEY, col, ' ', ATTR_BASE);
      continue;
    }

    label = g_fkeyLabel[i];
    len = (int)strlen(label);
    c = col - g_fkeyCol[i];
    pad = (FKEY_FIELD_WIDTH - len) / 2;
    if (pad < 0) pad = 0;

    if (len > 0 && c >= pad && c < pad + len) {
      ch = (unsigned char)label[c - pad];
      attr = ((c - pad) == g_fkeyHiPos[i]) ? ATTR_FKEY_KEY : ATTR_FKEY_LABEL;
    } else if (len > 0) {
      ch = ' ';
      attr = ATTR_FKEY_LABEL;
    } else {
      /* 空文字列のフィールド（未実装のキー）は枠の外と同じ扱い。 */
      ch = ' ';
      attr = ATTR_BASE;
    }
    vram_ank(ROW_FKEY, col, ch, attr);
  }
}

/* ---- エディタの状態 ------------------------------------------------ */

char g_text[TEXT_MAX + 1];
unsigned int g_lineStart[MAX_LINES];
int g_textLen;
int g_lineCount;

int g_curLine;   /* 0始まり */
int g_curByte;   /* 行頭からのバイトオフセット */
int g_goalCol;   /* 縦移動で保つ目標セル桁 */
int g_topLine;   /* 画面最上段に表示されている行 */
int g_leftCol;   /* 画面左端のセル桁（横スクロール） */

int g_insertMode; /* 1=挿入、0=上書き */
int g_modified;

char g_filename[FILENAME_MAX + 1];
char g_notice[48]; /* ステータス行の一時通知（毎キー入力で既定はクリア） */

char g_iobuf[IOBUF_SIZE];

int g_lineOverflow; /* rebuild_lines()が最後に数えた行数がMAX_LINESを
                        超えていたら1。g_lineCount自体はMAX_LINESで
                        丸めてしまうため、超過の有無はこのフラグでしか
                        分からない（load_file()参照）。 */

/* ---- 行表・バッファ操作 -------------------------------------------- */

int is_lead_byte(unsigned char c)
{
  return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

/* g_text[0..g_textLen)全体を'\n'で分割し直し、g_lineStart[]/g_lineCountを
   再構築する。挿入・削除のたびに毎回全体を再構築する（設計書2章：
   「24KBの走査は正しさを優先して許容する」）。ファイルが'\n'で終わって
   いれば末尾に空行が1つ増える（split()と同じ挙動）。保存時にN行を
   N-1個の'\n'で連結し直すことで、この空行が元の「末尾に改行がある」
   状態を正確に復元する（save_file()参照）。
   実際の行数がMAX_LINESを超える場合、g_lineStart[]へこれ以上書き込む
   余地が無いのでMAX_LINESで打ち切るが、そのことをg_lineOverflowへ
   記録する。呼び出し元（load_file()）はこれを見て「黙って切り捨てず、
   読み込みを中止する」（設計書2章）を実行する。 */
void rebuild_lines(void)
{
  int i;
  int count;

  g_lineStart[0] = 0;
  count = 1;
  g_lineOverflow = 0;
  for (i = 0; i < g_textLen; i++) {
    if (g_text[i] == '\n') {
      if (count < MAX_LINES) {
        g_lineStart[count] = (unsigned int)(i + 1);
      } else {
        g_lineOverflow = 1;
      }
      count++;
    }
  }
  if (count > MAX_LINES) {
    g_lineOverflow = 1;
    count = MAX_LINES;
  }
  g_lineCount = count;
}

/* 行iの内容バイト数（末尾の'\n'を含まない）。次の行の開始位置から
   逆算するので、行表はオフセットだけを持てば足りる（設計書2章）。 */
int line_len(int i)
{
  if (i + 1 < g_lineCount) return (int)g_lineStart[i + 1] - 1 - (int)g_lineStart[i];
  return g_textLen - (int)g_lineStart[i];
}

/* 行lineIdxの中で、バイトオフセットbyteOffにある文字のバイト長
   （1か2）。byteOffが行の終端（改行の位置）にあるときは0を返す。 */
int char_len_at(int lineIdx, int byteOff)
{
  int start;
  int len;
  unsigned char c;

  start = (int)g_lineStart[lineIdx];
  len = line_len(lineIdx);
  if (byteOff >= len) return 0;
  c = (unsigned char)g_text[start + byteOff];
  if (is_lead_byte(c) && byteOff + 1 < len) return 2;
  return 1;
}

/* byteOffの直前の文字境界のバイトオフセット。byteOff自身が有効な
   文字境界であることが前提（呼び出し元はカーソル位置からのみ呼ぶ）。 */
int prev_char_start(int lineIdx, int byteOff)
{
  int i;
  int prev;

  i = 0;
  prev = 0;
  while (i < byteOff) {
    prev = i;
    i += char_len_at(lineIdx, i);
  }
  return prev;
}

/* 行lineIdxの先頭からbyteOffまでの表示セル数。タブは次のタブストップ
   （TAB_WIDTH桁ごと）まで進む。byteOffは文字境界であること。 */
int col_at_byte(int lineIdx, int byteOff)
{
  int start;
  int len;
  int i;
  int cellCol;
  unsigned char c;

  start = (int)g_lineStart[lineIdx];
  len = line_len(lineIdx);
  i = 0;
  cellCol = 0;
  while (i < byteOff) {
    c = (unsigned char)g_text[start + i];
    if (c == 0x09) {
      cellCol += TAB_WIDTH - (cellCol % TAB_WIDTH);
      i++;
    } else if (is_lead_byte(c) && i + 1 < len) {
      cellCol += 2;
      i += 2;
    } else {
      cellCol++;
      i++;
    }
  }
  return cellCol;
}

/* col_at_byte()の逆：targetCol以下でもっとも近い文字境界のバイト
   オフセットを返す（全角の途中には決して着地しない。設計書3章）。 */
int byte_at_col(int lineIdx, int targetCol)
{
  int start;
  int len;
  int i;
  int cellCol;
  int w;
  unsigned char c;

  start = (int)g_lineStart[lineIdx];
  len = line_len(lineIdx);
  i = 0;
  cellCol = 0;
  while (i < len) {
    c = (unsigned char)g_text[start + i];
    if (c == 0x09) {
      w = TAB_WIDTH - (cellCol % TAB_WIDTH);
    } else if (is_lead_byte(c) && i + 1 < len) {
      w = 2;
    } else {
      w = 1;
    }
    if (cellCol + w > targetCol) break;
    cellCol += w;
    if (c == 0x09) i++;
    else if (is_lead_byte(c) && i + 1 < len) i += 2;
    else i++;
  }
  return i;
}

/* g_curLine/g_curByteを有効な範囲に丸める（rebuild_lines()で行数が
   減った直後など）。 */
void clamp_cursor(void)
{
  int len;

  if (g_curLine >= g_lineCount) g_curLine = g_lineCount - 1;
  if (g_curLine < 0) g_curLine = 0;
  len = line_len(g_curLine);
  if (g_curByte > len) g_curByte = len;
  if (g_curByte < 0) g_curByte = 0;
}

/* カーソル移動・編集の後に必ず呼ぶ：カーソルを丸め、縦横スクロール
   位置（g_topLine/g_leftCol）を最小量だけ動かしてカーソルを画面内に
   収める（設計書4章）。 */
void ensure_visible(void)
{
  int cellCol;

  clamp_cursor();
  if (g_curLine < g_topLine) g_topLine = g_curLine;
  if (g_curLine >= g_topLine + BODY_ROWS) g_topLine = g_curLine - BODY_ROWS + 1;
  if (g_topLine < 0) g_topLine = 0;

  cellCol = col_at_byte(g_curLine, g_curByte);
  if (cellCol < g_leftCol) g_leftCol = cellCol;
  if (cellCol >= g_leftCol + VRAM_COLS) g_leftCol = cellCol - VRAM_COLS + 1;
  if (g_leftCol < 0) g_leftCol = 0;
}

/* atOffsetAbsの位置にbytes[0..nBytes)を挿入する。newlineCountは挿入する
   '\n'の個数（呼び出し元が事前に数える）で、行数の上限チェックに使う。
   容量／行数どちらかの上限を超える場合は何もせず0を返す（設計書2章：
   「容量オーバーは黙って切り捨てず、編集中に上限へ達した入力は無視し、
   ステータス行へ通知する」）。 */
int buffer_insert(int atOffsetAbs, char *bytes, int nBytes, int newlineCount)
{
  if (g_textLen + nBytes > TEXT_MAX) return 0;
  if (g_lineCount + newlineCount > MAX_LINES) return 0;
  memmove(g_text + atOffsetAbs + nBytes, g_text + atOffsetAbs, (unsigned)(g_textLen - atOffsetAbs));
  memcpy(g_text + atOffsetAbs, bytes, (unsigned)nBytes);
  g_textLen += nBytes;
  rebuild_lines();
  g_modified = 1;
  return 1;
}

void buffer_delete(int atOffsetAbs, int nBytes)
{
  if (nBytes <= 0) return;
  memmove(g_text + atOffsetAbs, g_text + atOffsetAbs + nBytes, (unsigned)(g_textLen - atOffsetAbs - nBytes));
  g_textLen -= nBytes;
  rebuild_lines();
  g_modified = 1;
}

/* ---- カーソル移動（設計書3章：文字単位。行末には止まれる。
   縦移動はg_goalCol以下でもっとも近い文字境界へ着地する） ------------- */

void cursor_left(void)
{
  if (g_curByte > 0) {
    g_curByte = prev_char_start(g_curLine, g_curByte);
  } else if (g_curLine > 0) {
    g_curLine--;
    g_curByte = line_len(g_curLine);
  }
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void cursor_right(void)
{
  int len;

  len = line_len(g_curLine);
  if (g_curByte < len) {
    g_curByte += char_len_at(g_curLine, g_curByte);
  } else if (g_curLine + 1 < g_lineCount) {
    g_curLine++;
    g_curByte = 0;
  }
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void cursor_up(void)
{
  if (g_curLine > 0) {
    g_curLine--;
    g_curByte = byte_at_col(g_curLine, g_goalCol);
  }
  ensure_visible();
}

void cursor_down(void)
{
  if (g_curLine + 1 < g_lineCount) {
    g_curLine++;
    g_curByte = byte_at_col(g_curLine, g_goalCol);
  }
  ensure_visible();
}

void cursor_home(void)
{
  g_curByte = 0;
  g_goalCol = 0;
  ensure_visible();
}

void cursor_line_end(void)
{
  g_curByte = line_len(g_curLine);
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void cursor_doc_home(void)
{
  g_curLine = 0;
  g_curByte = 0;
  g_goalCol = 0;
  ensure_visible();
}

void cursor_doc_end(void)
{
  g_curLine = g_lineCount - 1;
  g_curByte = line_len(g_curLine);
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void page_move(int delta)
{
  g_curLine += delta;
  if (g_curLine < 0) g_curLine = 0;
  if (g_curLine >= g_lineCount) g_curLine = g_lineCount - 1;
  g_curByte = byte_at_col(g_curLine, g_goalCol);
  ensure_visible();
}

/* ---- 編集操作 -------------------------------------------------------- */

void insert_char_at_cursor(unsigned char b1, unsigned char b2, int nb)
{
  int abspos;
  char tmp[2];
  int len;
  int oldLen;
  int newTextLen;

  abspos = (int)g_lineStart[g_curLine] + g_curByte;
  tmp[0] = (char)b1;
  if (nb == 2) tmp[1] = (char)b2;

  oldLen = 0;
  if (!g_insertMode) {
    len = line_len(g_curLine);
    if (g_curByte < len) oldLen = char_len_at(g_curLine, g_curByte);
  }

  /* 上書きモードで置き換える既存文字がある場合、先に「削除してから
     挿入」の結果サイズが容量に収まるかを確かめてから、初めて
     buffer_delete()する。削除を先にしてから挿入が失敗すると、元の
     文字だけが消えて戻せなくなるため（レビュー指摘：上限到達時に
     文字が消える不具合）。newlineCountは常に0（この関数は改行を
     挿入しない）なので、この容量チェックさえ通ればbuffer_insert()は
     行数上限で失敗することもない。 */
  newTextLen = g_textLen - oldLen + nb;
  if (newTextLen > TEXT_MAX) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_LIMIT_NOTICE));
    return;
  }

  if (oldLen > 0) buffer_delete(abspos, oldLen);

  if (!buffer_insert(abspos, tmp, nb, 0)) {
    /* 上のチェックにより通常ここには来ない。防御的に通知だけ出す。 */
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_LIMIT_NOTICE));
    return;
  }
  g_curByte += nb;
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void insert_tab(void)
{
  int abspos;
  char t;

  abspos = (int)g_lineStart[g_curLine] + g_curByte;
  t = 0x09;
  if (!buffer_insert(abspos, &t, 1, 0)) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_LIMIT_NOTICE));
    return;
  }
  g_curByte += 1;
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void insert_newline(void)
{
  int abspos;
  char nl;

  if (g_lineCount + 1 > MAX_LINES) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_LIMIT_NOTICE));
    return;
  }
  abspos = (int)g_lineStart[g_curLine] + g_curByte;
  nl = 0x0a;
  if (!buffer_insert(abspos, &nl, 1, 1)) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_LIMIT_NOTICE));
    return;
  }
  g_curLine++;
  g_curByte = 0;
  g_goalCol = 0;
  ensure_visible();
}

void delete_before_cursor(void)
{
  int prevByte;
  int delLen;
  int abspos;
  int prevLen;

  if (g_curByte > 0) {
    prevByte = prev_char_start(g_curLine, g_curByte);
    delLen = g_curByte - prevByte;
    abspos = (int)g_lineStart[g_curLine] + prevByte;
    buffer_delete(abspos, delLen);
    g_curByte = prevByte;
  } else if (g_curLine > 0) {
    prevLen = line_len(g_curLine - 1);
    abspos = (int)g_lineStart[g_curLine - 1] + prevLen;
    buffer_delete(abspos, 1); /* 前の行末の'\n'を消して2行を連結する */
    g_curLine--;
    g_curByte = prevLen;
  }
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void delete_at_cursor(void)
{
  int len;
  int delLen;
  int abspos;

  len = line_len(g_curLine);
  if (g_curByte < len) {
    delLen = char_len_at(g_curLine, g_curByte);
    abspos = (int)g_lineStart[g_curLine] + g_curByte;
    buffer_delete(abspos, delLen);
  } else if (g_curLine + 1 < g_lineCount) {
    abspos = (int)g_lineStart[g_curLine] + len;
    buffer_delete(abspos, 1); /* 次の行との間の'\n'を消して連結する */
  }
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

void delete_current_line(void)
{
  int start;
  int endExclusive;
  int delLen;

  start = (int)g_lineStart[g_curLine];
  if (g_curLine + 1 < g_lineCount) {
    endExclusive = (int)g_lineStart[g_curLine + 1];
  } else {
    endExclusive = g_textLen;
  }
  delLen = endExclusive - start;
  if (delLen > 0) buffer_delete(start, delLen);
  g_curByte = 0;
  g_goalCol = 0;
  ensure_visible();
}

/* sappend()はcap引数を取るが、MSG()の戻り値はconst char*で長さが
   まちまちなので、g_notice[]への単純なコピーに使う小さなラッパ。 */
void sappend_copy(char *dst, int cap, char *s)
{
  int lp;

  lp = 0;
  sappend(dst, &lp, s, cap);
}

/* ---- ファイル読み込み・保存 ------------------------------------------
 * 改行はバッファ内では常にLF(0x0A)1バイトに正規化する：CRLF→LF、
 * 単独のCRもLFとして扱う（設計書2章）。保存時はLF→CRLFへ戻す。
 * 末尾行に改行の無いファイルは、rebuild_lines()の「N行はN-1個の
 * '\n'で区切られる」という不変条件により自然に保たれる（save_file()の
 * コメント参照）。 ------------------------------------------------------ */

#define LOAD_OK        0
#define LOAD_NOTFOUND  (-1)
#define LOAD_TOO_LARGE 1
#define LOAD_TOO_MANY_LINES 2

int load_file(char *path)
{
  int h;
  int n;
  int i;
  int pendingCR;
  unsigned char b;

  h = dos_open(path, 0);
  if (h < 0) return LOAD_NOTFOUND;

  g_textLen = 0;
  pendingCR = 0;
  for (;;) {
    n = dos_read((unsigned int)h, g_iobuf, IOBUF_SIZE);
    if (n <= 0) break;
    for (i = 0; i < n; i++) {
      b = (unsigned char)g_iobuf[i];
      if (pendingCR) {
        pendingCR = 0;
        if (b == 0x0a) continue; /* CRLFの対、CRの時点で既にLFを出力済み */
      }
      if (b == 0x0d) {
        pendingCR = 1;
        if (g_textLen >= TEXT_MAX) { dos_close((unsigned int)h); return LOAD_TOO_LARGE; }
        g_text[g_textLen] = 0x0a;
        g_textLen++;
        continue;
      }
      if (g_textLen >= TEXT_MAX) { dos_close((unsigned int)h); return LOAD_TOO_LARGE; }
      g_text[g_textLen] = (char)b;
      g_textLen++;
    }
  }
  dos_close((unsigned int)h);
  rebuild_lines();
  if (g_lineOverflow) return LOAD_TOO_MANY_LINES;
  return LOAD_OK;
}

int save_file(char *path)
{
  int h;
  int i;
  int outLen;
  unsigned char c;

  h = dos_create(path, 0);
  if (h < 0) return -1;

  outLen = 0;
  for (i = 0; i < g_textLen; i++) {
    c = (unsigned char)g_text[i];
    if (c == 0x0a) {
      if (outLen + 2 > IOBUF_SIZE) {
        if (dos_wfile((unsigned int)h, g_iobuf, (unsigned int)outLen) != (unsigned int)outLen) { dos_close((unsigned int)h); return -1; }
        outLen = 0;
      }
      g_iobuf[outLen] = 0x0d; outLen++;
      g_iobuf[outLen] = 0x0a; outLen++;
    } else {
      if (outLen + 1 > IOBUF_SIZE) {
        if (dos_wfile((unsigned int)h, g_iobuf, (unsigned int)outLen) != (unsigned int)outLen) { dos_close((unsigned int)h); return -1; }
        outLen = 0;
      }
      g_iobuf[outLen] = (char)c; outLen++;
    }
  }
  if (outLen > 0) {
    if (dos_wfile((unsigned int)h, g_iobuf, (unsigned int)outLen) != (unsigned int)outLen) { dos_close((unsigned int)h); return -1; }
  }
  dos_close((unsigned int)h);
  return 0;
}

/* ---- 画面描画 --------------------------------------------------------- */

void draw_screen(void); /* draw_dialog()/draw_input_box()から前方参照する */

/* nameの表示幅がavailCellsセルを超える場合、末尾を残して先頭を省略し、
   省略した印として先頭に'<'を1つ置く（全角文字の途中では区切らない）。
   dstの*lenpバイト目に追記し、続けてsappend()できるよう更新する（規約は
   sappend()と同じ）。ファイル名は末尾のほうが情報量が多い（拡張子や
   ディレクトリの深い側）ため、先頭でなく末尾を残す。 */
void sappend_tail_cells(char *dst, int *lenp, char *name, int availCells, int cap)
{
  int totalW;
  int boundaries[FILENAME_MAX + 1];
  int widths[FILENAME_MAX + 1];
  int n;
  int i;
  unsigned char c;
  int k;
  int suffix;
  int target;
  int start;

  totalW = text_width(name);
  if (totalW <= availCells) {
    sappend(dst, lenp, name, cap);
    return;
  }

  /* 先頭の'<'に1セル使うため、名前本体に使える幅は1少ない */
  target = availCells - 1;
  if (target < 0) target = 0;

  /* 文字境界（バイトオフセット）と各文字の表示幅を先頭から列挙する */
  n = 0;
  i = 0;
  while (name[i] != 0 && n < FILENAME_MAX) {
    boundaries[n] = i;
    c = (unsigned char)name[i];
    if (is_lead_byte(c) && name[i + 1] != 0) {
      widths[n] = 2;
      i += 2;
    } else {
      widths[n] = 1;
      i += 1;
    }
    n++;
  }
  boundaries[n] = i; /* 文字列末尾のバイトオフセット */

  /* 末尾の文字から順に幅を積み上げ、targetを超えない最大範囲を探す。
     見つからなければ（1文字も入らなければ）空文字列のまま。 */
  start = boundaries[n];
  suffix = 0;
  for (k = n - 1; k >= 0; k--) {
    if (suffix + widths[k] > target) break;
    suffix += widths[k];
    start = boundaries[k];
  }

  sappend(dst, lenp, "<", cap);
  sappend(dst, lenp, name + start, cap);
}

/* ステータス行（行0）を描画する。行1のファイル名専用行は廃止し、
   ファイル名と変更マークをこの行に統合した（利用者の指摘：
   「ファイル名だけなら1行目に入りそう」）。さらに左端固定の
   "Tsubaki"欄は廃止し、プログラム名はファイル名の後ろへ" - Tsubaki"
   として移した（利用者の提案：変わるのはファイル名のほうなので、
   固定のプログラム名を先頭に置く理由がない）。フィールド幅の内訳は
   STATUS_*_WIDTHの定義部コメント参照。 */
void draw_status_line(void)
{
  /* '<'(1) + ファイル名 + '*'(1) + " - Tsubaki"(STATUS_SUFFIX_WIDTH) + NUL の余裕 */
  char fileBuf[FILENAME_MAX + STATUS_SUFFIX_WIDTH + 4];
  char right[STATUS_RIGHT_WIDTH + 1];
  char rightRaw[40];
  char *name;
  int fileAvail;
  int p;
  int lineNo;
  int colNo;

  /* ファイル名欄と通知欄は同じフィールド（STATUS_SHARED_WIDTH）を
     共有する。通知g_noticeが出ている間はそちらを優先して描き、これ
     までどおり同じ欄を占める（通知は一時的で次のキー入力で消える
     ため、その間ファイル名や" - Tsubaki"が隠れても差し支えない）。
     空なら「ファイル名（＋変更マーク'*'、ファイル名の直後）
     + " - Tsubaki"」を描く。" - Tsubaki"は必ず全体を表示するので、
     ファイル名に使える幅はSTATUS_SHARED_WIDTHからSTATUS_SUFFIX_WIDTH
     ぶん差し引いた残りになる（省略が要るのはファイル名部分だけ）。 */
  if (g_notice[0] != 0) {
    vram_puts_cells(ROW_STATUS, STATUS_SHARED_COL, g_notice, ATTR_STATUS, STATUS_SHARED_WIDTH);
  } else {
    name = (g_filename[0] == 0) ? MSG(MSG_UNTITLED) : g_filename;
    fileAvail = STATUS_SHARED_WIDTH - STATUS_SUFFIX_WIDTH - (g_modified ? 1 : 0);
    p = 0;
    fileBuf[0] = 0;
    sappend_tail_cells(fileBuf, &p, name, fileAvail, sizeof(fileBuf));
    if (g_modified) sappend(fileBuf, &p, "*", sizeof(fileBuf));
    sappend(fileBuf, &p, " - ", sizeof(fileBuf));
    sappend(fileBuf, &p, g_title, sizeof(fileBuf));
    vram_puts_cells(ROW_STATUS, STATUS_SHARED_COL, fileBuf, ATTR_STATUS, STATUS_SHARED_WIDTH);
  }

  /* 右：行:桁 [挿入/上書き] TAB=幅（変更なし） */
  lineNo = g_curLine + 1;
  colNo = col_at_byte(g_curLine, g_curByte) + 1;
  p = 0;
  sappend_uint(rightRaw, &p, (unsigned int)lineNo, sizeof(rightRaw));
  sappend(rightRaw, &p, ":", sizeof(rightRaw));
  sappend_uint(rightRaw, &p, (unsigned int)colNo, sizeof(rightRaw));
  sappend(rightRaw, &p, "  [", sizeof(rightRaw));
  sappend(rightRaw, &p, g_insertMode ? MSG(MSG_MODE_INS) : MSG(MSG_MODE_OVR), sizeof(rightRaw));
  sappend(rightRaw, &p, "]  TAB=", sizeof(rightRaw));
  sappend_uint(rightRaw, &p, (unsigned int)TAB_WIDTH, sizeof(rightRaw));

  right_justify(right, 0, STATUS_RIGHT_WIDTH, rightRaw);
  vram_puts_cells(ROW_STATUS, VRAM_COLS - STATUS_RIGHT_WIDTH, right, ATTR_STATUS, STATUS_RIGHT_WIDTH);
}

/* 本文の1行を、横スクロール位置g_leftColを反映して描画する。全角
   文字が画面端で半分だけになる場合は空白にして半分だけ描かない
   （設計書4章）。 */
void draw_body_line(int bodyRow, int lineIdx)
{
  int start;
  int len;
  int i;
  int cellCol;
  int w;
  int row;
  int j;
  unsigned char c;
  unsigned char c2;
  int bothVisible;

  row = BODY_TOP + bodyRow;
  start = (int)g_lineStart[lineIdx];
  len = line_len(lineIdx);
  i = 0;
  cellCol = 0;

  while (i < len) {
    c = (unsigned char)g_text[start + i];
    if (c == 0x09) {
      w = TAB_WIDTH - (cellCol % TAB_WIDTH);
      for (j = 0; j < w; j++) {
        if (cellCol >= g_leftCol && cellCol < g_leftCol + VRAM_COLS) {
          vram_ank(row, cellCol - g_leftCol, ' ', ATTR_BASE);
        }
        cellCol++;
      }
      i++;
    } else if (is_lead_byte(c) && i + 1 < len) {
      c2 = (unsigned char)g_text[start + i + 1];
      bothVisible = (cellCol >= g_leftCol) && (cellCol + 1 <= g_leftCol + VRAM_COLS - 1);
      if (bothVisible) {
        vram_zenkaku(row, cellCol - g_leftCol, c, c2, ATTR_BASE);
      } else {
        if (cellCol >= g_leftCol && cellCol < g_leftCol + VRAM_COLS) {
          vram_ank(row, cellCol - g_leftCol, ' ', ATTR_BASE);
        }
        if (cellCol + 1 >= g_leftCol && cellCol + 1 < g_leftCol + VRAM_COLS) {
          vram_ank(row, cellCol + 1 - g_leftCol, ' ', ATTR_BASE);
        }
      }
      cellCol += 2;
      i += 2;
    } else {
      if (cellCol >= g_leftCol && cellCol < g_leftCol + VRAM_COLS) {
        vram_ank(row, cellCol - g_leftCol, c, ATTR_BASE);
      }
      cellCol++;
      i++;
    }
  }
  while (cellCol < g_leftCol + VRAM_COLS) {
    if (cellCol >= g_leftCol) vram_ank(row, cellCol - g_leftCol, ' ', ATTR_BASE);
    cellCol++;
  }
}

void draw_body(void)
{
  int row;
  int lineIdx;
  int c;

  for (row = 0; row < BODY_ROWS; row++) {
    lineIdx = g_topLine + row;
    if (lineIdx < g_lineCount) {
      draw_body_line(row, lineIdx);
    } else {
      for (c = 0; c < VRAM_COLS; c++) vram_ank(BODY_TOP + row, c, ' ', ATTR_BASE);
    }
  }
}

void draw_screen(void)
{
  draw_status_line();
  draw_body();
  draw_fkey_row();
}

/* ---- ダイアログ（すみれのdraw_dialog()/draw_input_box()/input_dialog()
   を土台に、つばきはカーソルを常時表示するため表示/非表示の切替を
   省いた版） ------------------------------------------------------------ */

void draw_dialog(char *msg, char *errmsg)
{
  int i;
  int row;

  draw_screen();

  row = DIALOG_ROW;
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BORDER);
  row++;

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
  vram_puts_cells(row, DIALOG_COL + 1, msg, ATTR_VALUE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
  row++;

  if (errmsg != 0) {
    vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
    vram_puts_cells(row, DIALOG_COL + 1, errmsg, ATTR_VALUE, DIALOG_WIDTH);
    vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
    row++;
  }

  vram_ank(row, DIALOG_COL, BOXCH_BL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_BR, ATTR_BORDER);
}

void show_notice_dialog(char *msg)
{
  draw_dialog(msg, 0);
  key_read();
}

void draw_input_box(char *prompt, char *buf, int len, char *errmsg)
{
  char line[DIALOG_WIDTH * 2 + FILENAME_MAX + 4];
  int i;
  int row;
  int fieldRow;
  int fieldCol;
  int p;
  int promptCells;

  draw_screen();

  row = DIALOG_ROW;
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BORDER);
  row++;

  p = 0;
  sappend(line, &p, prompt, sizeof(line));
  promptCells = text_width(line);
  buf[len] = 0;
  sappend(line, &p, buf, sizeof(line));

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
  vram_puts_cells(row, DIALOG_COL + 1, line, ATTR_VALUE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
  fieldRow = row;
  row++;

  if (errmsg != 0) {
    vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
    vram_puts_cells(row, DIALOG_COL + 1, errmsg, ATTR_VALUE, DIALOG_WIDTH);
    vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
    row++;
  }

  vram_ank(row, DIALOG_COL, BOXCH_BL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_BR, ATTR_BORDER);

  fieldCol = DIALOG_COL + 1 + promptCells + len;
  ansi_goto(fieldRow, fieldCol);
}

/* 1つのテキストフィールドについてモーダル編集ループを回す。BSは
   常に「最後の1文字を削除」（すみれのinput_dialog()と同じ規律：
   このダイアログの中では左移動と衝突しない）。表示可能なASCII
   （0x20-0x7E）はmaxlen未満の余地があれば追記する。Enterで確定なら
   1、ESCでキャンセルなら0を返す。 */
int input_dialog(char *prompt, char *buf, int maxlen, char *errmsg)
{
  int len;
  int key;

  len = (int)strlen(buf);
  if (len > maxlen) len = maxlen;

  for (;;) {
    draw_input_box(prompt, buf, len, errmsg);
    key = key_read();

    if (key == KEY_ESC) {
      buf[len] = 0;
      return 0;
    }
    if (key == KEY_ENTER) {
      buf[len] = 0;
      return 1;
    }
    if (key == 0x08) {
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
    /* それ以外のキー（矢印、TABなど）：このダイアログでは無視する */
  }
}

int confirm_quit(void)
{
  int key;

  if (!g_modified) return 1;
  for (;;) {
    draw_dialog(MSG(MSG_QUIT_CONFIRM), 0);
    key = key_read();
    if (key == 'y' || key == 'Y') return 1;
    if (key == 'n' || key == 'N' || key == KEY_ESC) return 0;
  }
}

/* ---- コマンド（保存・名前を変えて保存・行番号指定） -------------------- */

void goto_line_dialog(void)
{
  char buf[8];
  int ok;
  int v;
  int i;

  buf[0] = 0;
  ok = input_dialog(MSG(MSG_GOTO_PROMPT), buf, 6, 0);
  if (!ok) return;
  v = 0;
  for (i = 0; buf[i] != 0; i++) {
    if (buf[i] >= '0' && buf[i] <= '9') v = v * 10 + (buf[i] - '0');
  }
  if (v <= 0) return;
  g_curLine = v - 1;
  if (g_curLine >= g_lineCount) g_curLine = g_lineCount - 1;
  if (g_curLine < 0) g_curLine = 0;
  g_curByte = 0;
  g_goalCol = 0;
  ensure_visible();
}

/* 名前を構成する1要素（basename[.ext]）がDOSの8.3形式に収まるかを
   調べる。sはNUL終端されていなくてよい（呼び出し元がバックスラッシュ
   区切りの断片を渡すため）。基本名1～8文字・拡張子0～3文字・ドット
   は最大1つ。不正なら0、妥当なら1を返す。 */
int is_valid_dos_element(char *s, int len)
{
  int i;
  int dotpos;
  int baselen;
  int extlen;

  if (len <= 0) return 0;

  dotpos = -1;
  for (i = 0; i < len; i++) {
    if (s[i] == '.') {
      if (dotpos >= 0) return 0; /* 拡張子は最大1つ */
      dotpos = i;
    }
  }
  if (dotpos < 0) {
    baselen = len;
    extlen = 0;
  } else {
    baselen = dotpos;
    extlen = len - dotpos - 1;
  }
  if (baselen < 1 || baselen > 8) return 0;
  if (extlen > 3) return 0;
  return 1;
}

/* DOSのファイル名として使えない文字（* ? " < > | と制御文字）かどうか。
   実機で踏んだ事故（LINES.TXTSAVED.TXTのような連結）はここでは
   弾けない（.と英数字しか使っていないため）――弾くのは
   is_valid_dos_element()のドット数・長さチェックの役目。この関数は
   別種の不正（記号や制御文字の混入）を担当する。 */
int is_forbidden_char(unsigned char c)
{
  if (c < 0x20) return 1;
  if (c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 1;
  return 0;
}

/* nameがDOSのパスとして妥当かを検査する。ドライブ名（例：B:）と
   バックスラッシュ区切りのパスを受け付け、'\'で区切られた各要素に
   is_valid_dos_element()を適用する。空文字列は呼び出し元
   （save_as_command()）が別のメッセージで先に弾くので、ここでは
   「空＝不正」として扱ってよい。 */
int is_valid_dos_path(char *name)
{
  int i;
  int start;
  unsigned char c;

  if (name[0] == 0) return 0;

  i = 0;
  /* ドライブ名（英字1文字＋コロン）は先頭にだけ許す */
  if (((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':') {
    i = 2;
  }
  /* ドライブの直後の1つの'\'はルートを表す区切りであり、名前の要素
     ではないので、要素の開始位置をその次からにする */
  if (name[i] == '\\') i++;

  start = i;
  for (;; i++) {
    c = (unsigned char)name[i];
    if (c == '\\' || c == 0) {
      if (!is_valid_dos_element(name + start, i - start)) return 0;
      if (c == 0) break;
      start = i + 1;
      continue;
    }
    if (c == ':') return 0; /* ドライブ以外でのコロンは不可 */
    if (is_forbidden_char(c)) return 0;
  }
  return 1;
}

/* F3：名前を変えて保存する。すみれのdo_rename()/do_mkdir()と同じ
   「確定するたびに検査し、失敗したら入力済みの文字列を残したまま
   同じダイアログへエラー行付きで戻る」形にしている（設計書の
   do_rename()参照）。以前はinput_dialog()を1回呼ぶだけで、save_file()
   失敗を「保存に失敗しました（書込禁止？）」という別の
   press-any-keyダイアログで報告していた。だが実機では、F3の
   初期値（現在のファイル名）を消さずに打つと名前が連結され
   （例："LINES.TXT"+"SAVED.TXT"="LINES.TXTSAVED.TXT"）、8.3形式を
   超えた名前でDOSへの書き込みに失敗した。このとき出た
   「書込禁止？」は原因の誤帰属であり（実際は名前が不正なだけ）、
   しかもダイアログが閉じて入力内容も失われるため打ち直せなかった。
   そこで、DOSへ渡す前にここで名前を検査し（is_valid_dos_path()）、
   save_file()を呼ぶのはそれを通った後だけにする。失敗の種類ごとに
   別のメッセージを出し、原因を断定する文言（「書込禁止？」等）は
   使わない。 */
void save_as_command(void)
{
  char buf[FILENAME_MAX + 1];
  char *err;
  int ok;
  int r;

  strcpy(buf, g_filename);
  err = 0;
  for (;;) {
    ok = input_dialog(MSG(MSG_SAVEAS_PROMPT), buf, FILENAME_MAX, err);
    if (!ok) return;
    if (buf[0] == 0) {
      err = MSG(MSG_SAVEAS_ERR_EMPTY);
      continue;
    }
    if (!is_valid_dos_path(buf)) {
      err = MSG(MSG_SAVEAS_ERR_INVALID);
      continue;
    }
    r = save_file(buf);
    if (r != 0) {
      err = MSG(MSG_SAVEAS_ERR_FAILED);
      continue;
    }
    break;
  }
  strcpy(g_filename, buf);
  g_modified = 0;
}

void save_command(void)
{
  int r;

  if (g_filename[0] == 0) { save_as_command(); return; }
  r = save_file(g_filename);
  if (r != 0) { show_notice_dialog(MSG(MSG_SAVE_FAIL)); return; }
  g_modified = 0;
}

/* ---- コマンドラインスイッチ ----------------------------------------- */

char *g_argFile = 0;

void parse_args(int argc, char *argv[])
{
  int i;

  g_lang = LANG_JA;
  g_argFile = 0;
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "/E") == 0 || strcmp(argv[i], "/e") == 0) {
      g_lang = LANG_EN;
    } else if (strcmp(argv[i], "/J") == 0 || strcmp(argv[i], "/j") == 0) {
      g_lang = LANG_JA;
    } else {
      g_argFile = argv[i];
    }
  }
}

/* ---- main ------------------------------------------------------------ */

int main(int argc, char *argv[])
{
  int key;
  int running;
  int r;
  int screenRow;
  int screenCol;

  if (!msg_selftest()) {
    write_str("TSUBAKI: message table error (JA/EN mismatch)\r\n");
    return 1;
  }

  parse_args(argc, argv);

  g_textLen = 0;
  g_text[0] = 0;
  rebuild_lines();
  g_curLine = 0;
  g_curByte = 0;
  g_goalCol = 0;
  g_topLine = 0;
  g_leftCol = 0;
  g_insertMode = 1;
  g_modified = 0;
  g_filename[0] = 0;
  g_notice[0] = 0;

  if (g_argFile != 0) {
    strncpy(g_filename, g_argFile, FILENAME_MAX);
    g_filename[FILENAME_MAX] = 0;
    r = load_file(g_argFile);
    if (r == LOAD_OK) {
      g_modified = 0;
    } else if (r == LOAD_NOTFOUND) {
      g_textLen = 0;
      rebuild_lines();
      g_modified = 0;
    } else {
      g_textLen = 0;
      rebuild_lines();
      g_filename[0] = 0;
      write_str("\x1b[>1h");
      write_str("\x1b[>5l");
      vram_shadow_init();
      vram_clear_all();
      show_notice_dialog(r == LOAD_TOO_LARGE ? MSG(MSG_LOAD_TOO_LARGE) : MSG(MSG_LOAD_TOO_MANY_LINES));
    }
  }

  write_str("\x1b[>1h"); /* 最下段のファンクションキー行を解放する */
  write_str("\x1b[>5l"); /* テキストカーソルを表示する（編集中は常時表示） */

  vram_shadow_init();
  vram_clear_all();

  ensure_visible();
  draw_screen();

  running = 1;
  while (running) {
    screenRow = BODY_TOP + (g_curLine - g_topLine);
    screenCol = col_at_byte(g_curLine, g_curByte) - g_leftCol;
    ansi_goto(screenRow, screenCol);

    key = key_read();
    g_notice[0] = 0;

    if (key == KEY_LEFT) { cursor_left(); }
    else if (key == KEY_RIGHT) { cursor_right(); }
    else if (key == KEY_UP) { cursor_up(); }
    else if (key == KEY_DOWN) { cursor_down(); }
    else if (key == KEY_ROLLUP) { page_move(BODY_ROWS); }
    else if (key == KEY_ROLLDOWN) { page_move(-BODY_ROWS); }
    else if (key == KEY_HOME) { cursor_home(); }
    else if (key == KEY_CTRL_A) { cursor_home(); }
    else if (key == KEY_CTRL_E) { cursor_line_end(); }
    else if (key == KEY_CTRL_T) { cursor_doc_home(); }
    else if (key == KEY_CTRL_B) { cursor_doc_end(); }
    else if (key == KEY_CTRL_G) { goto_line_dialog(); }
    else if (key == KEY_CTRL_Y) { delete_current_line(); }
    else if (key == KEY_CTRL_V) { g_insertMode = !g_insertMode; }
    else if (key == KEY_BS) { delete_before_cursor(); }
    else if (key == KEY_CTRL_D) { delete_at_cursor(); }
    else if (key == KEY_ENTER) { insert_newline(); }
    else if (key == KEY_TAB) { insert_tab(); }
    else if (key == KEY_F2) { save_command(); }
    else if (key == KEY_F3) { save_as_command(); }
    else if (key == KEY_F10) { if (confirm_quit()) running = 0; }
    else if (key == KEY_CTRL_Q) { if (confirm_quit()) running = 0; }
    else if (key == KEY_ESC) { /* 何もしない（誤操作で終了しないため） */ }
    else if (key >= 0x20 && key <= 0x7e) {
      insert_char_at_cursor((unsigned char)key, 0, 1);
    } else if (key >= 0xa1 && key <= 0xdf) {
      /* 半角カナ（1バイト）。is_lead_byte()の範囲（0x81-0x9F/0xE0-0xFC）
         とは重ならないので、SJIS先頭バイトと衝突する心配はない
         （レビュー指摘：半角カナが入力できない不具合の修正）。 */
      insert_char_at_cursor((unsigned char)key, 0, 1);
    } else if ((key >= 0x81 && key <= 0x9f) || (key >= 0xe0 && key <= 0xfc)) {
      int key2;
      key2 = key_read(); /* SJISの2バイト目を必ず取ってからまとめて挿入する（設計書6章） */
      insert_char_at_cursor((unsigned char)key, (unsigned char)key2, 2);
    }
    /* それ以外の未割当キー（F1/F4-F9、HELP、INSなど）は無視する */

    draw_screen();
  }

  vram_clear_all();
  ansi_goto(0, 0);
  write_str("\x1b[>5l");
  write_str("\x1b[>1l");

  return 0;
}
