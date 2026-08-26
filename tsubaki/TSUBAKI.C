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
 *
 * v1 で検索（^F）・次を検索（^N・^L）・逐次置換（^R）・一括置換（^P）を
 * 追加した。設計根拠は docs/editor-measure-04.md（原物JEDの実測）と
 * docs/tsubaki-spec-01.md 9章。検索は文字単位で行い、SJISの2バイト文字の
 * 途中には一致させない（line_find()参照）。検索語・置換語の入力は
 * 既存のinput_dialog()をそのまま使うため、v1でも半角のみ（全角の検索語は
 * まだ打てない。FEP経由で入るかどうかは未確認）。
 *
 * さらにUndo（取り消し。^U、別名^Z）を追加した。バッファが
 * buffer_insert()/buffer_delete()を通じてのみ編集される単一の平文
 * バッファ＋行表であることを利用し、「どこに何バイト挿入した／どこから
 * 何バイト削除した」を固定サイズ（2KB）のリングバッファへ「1操作＝
 * 1記録」の粒度で積む（打鍵をまとめない。単純さを優先した自作の判断）。
 * リングが満杯になったら最古の記録から捨てる。詳細な設計はg_undoBuf[]
 * 直前のコメントを参照。Redo・打鍵のまとめ・カーソル移動だけの取消しは
 * 作っていない（README.md参照）。
 * まだ入れないもの：ブロック編集／クリップボード、ウィンドウ
 * 分割、引数なし起動時のファイル選択、BAK、XMS/EMS退避、オート
 * インデント、CFG。
 *
 * DOS/BIOS呼び出し・VRAM直書き層・ダイアログ機構・メッセージテーブル
 * 機構は、同じプロジェクトの guest/sumire/SUMIRE.C（ファイラ）から
 * 土台部分（下記参照）をコピーして流用している（同一プロジェクト・
 * 同一MITライセンスのため）。ファイラ固有の関数（ディレクトリ走査・
 * コピー／移動・実行など）はコピーしていない。エディタ本体（バッファ・
 * 行表・カーソル・描画・キー処理・ファイル入出力・検索・置換）はすべて
 * 新規に書き下ろした。
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
#define SEARCH_MAX  32     /* g_searchTerm[] に入れられる最大文字数。入力欄は既存の
                               input_dialog()を使うため半角のみ（v1設計方針参照） */
#define REPLACE_MAX 32     /* g_replaceTerm[] に入れられる最大文字数。同上 */

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
#define KEY_CTRL_F   0x06  /* 検索（v1） */
#define KEY_CTRL_G   0x07  /* 指定行番号へ */
#define KEY_CTRL_L   0x0c  /* 次を検索（v1。原物JEDと同じ割り当てを踏襲。^Nと同じ動作） */
#define KEY_CTRL_N   0x0e  /* 次を検索（v1） */
#define KEY_CTRL_P   0x10  /* 一括置換（v1） */
#define KEY_CTRL_Q   0x11  /* 終了 */
#define KEY_CTRL_R   0x12  /* 逐次置換（v1） */
#define KEY_CTRL_T   0x14  /* 文頭 */
#define KEY_CTRL_U   0x15  /* Undo（取り消し） */
#define KEY_CTRL_V   0x16  /* 挿入／上書き切替 */
#define KEY_CTRL_Y   0x19  /* カーソル行削除 */
#define KEY_CTRL_Z   0x1a  /* Undo（取り消し。^Uの別名） */

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

/* ---- ディレクトリ読み取り（引数なし起動のファイル選択で使う）--------------
 * すみれの同名ラッパをそのまま流用。どれも文書化された汎用DOS APIで、
 * 特定のプログラムのソースに由来するものではない。 */

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

/* (*hiP:*loP) /= 10 を行い、余り（0～9）を返す。SmallerCの16ビット
   モードにはlong型が無いので、ファイルサイズ（32ビット）を10進へ
   直すのにこれが要る。すみれと同じもの。 */
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
#define MSG_SEARCH_PROMPT       13 /* ^F・^R・^P：検索語入力ダイアログのプロンプト */
#define MSG_REPLACE_PROMPT      14 /* ^R・^P：置換語入力ダイアログのプロンプト */
#define MSG_SEARCH_NOT_FOUND    15 /* ^F/^N・^L/^R：検索語が（それ以上）見つからないときのダイアログ */
#define MSG_REPLACE_CONFIRM     16 /* ^R：一致ごとの置換確認ダイアログ（Y/N/ESC） */
#define MSG_REPLACE_LIMIT_STOP  17 /* ^R：置換で容量上限に達し、途中で打ち切ったときのダイアログ */
#define MSG_REPLACE_COUNT_PREFIX 18 /* ^P：ステータス行の通知に置換件数を組み立てる前半部分 */
#define MSG_REPLACE_COUNT_SUFFIX 19 /* ^P：件数の後に続ける後半部分（上限に達しなかった場合） */
#define MSG_REPLACE_LIMIT_SUFFIX 20 /* ^P：件数の後に続ける後半部分（容量上限で途中打ち切りの場合） */
#define MSG_UNDO_NONE     21 /* ^U/^Z：戻せる記録が無いときのステータス行通知 */
#define MSG_UNDO_FAILED   22 /* ^U/^Z：容量不足で削除の取り消し（再挿入）に失敗したときのステータス行通知 */
/* 引数なし起動のファイル選択（下の pick_* 群）。末尾に足すこと――
   既存の MSG_* の番号は動かさない。 */
#define MSG_PICK_GUIDE     23 /* 下枠に出すキーガイド */
#define MSG_PICK_EMPTY     24 /* 一覧に1件も無いとき、最初の行に出す */
#define MSG_PICK_TRUNCATED 25 /* 上限で打ち切ったとき、上枠のパスの後ろへ足す */

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
  "保存に失敗しました",
  "検索: ",
  "置換後: ",
  "見つかりませんでした（何かキーを押してください）",
  "置換しますか？ Y=置換 N=次へ ESC=中止",
  "上限のため置換を中止しました（何かキーを押してください）",
  "置換しました: ",
  "件",
  "件（上限のため中断）",
  "取り消せる変更がありません",
  "容量不足のため取り消せませんでした",
  "移動:上下 開く:Enter 親へ:BS 終了:ESC",
  "ファイルがありません",
  " ※以降は表示できません"
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
  "Save failed",
  "Find: ",
  "Replace with: ",
  "Not found (press any key)",
  "Replace? Y=replace N=skip ESC=cancel",
  "Replace stopped: limit reached (press any key)",
  "Replaced: ",
  " times",
  " times (stopped: limit)",
  "Nothing to undo",
  "Undo failed: not enough space",
  "Up/Dn Enter:open BS:parent ESC:quit",
  "No files here",
  " (list truncated)"
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

char g_searchTerm[SEARCH_MAX + 1];   /* ^F/^N・^L/^R/^Pで共有し、次回以降も保持する（v1設計方針） */
char g_replaceTerm[REPLACE_MAX + 1]; /* ^R/^Pで共有し、同様に保持する（次回の手間を省く自作判断） */

char g_iobuf[IOBUF_SIZE];

int g_lineOverflow; /* rebuild_lines()が最後に数えた行数がMAX_LINESを
                        超えていたら1。g_lineCount自体はMAX_LINESで
                        丸めてしまうため、超過の有無はこのフラグでしか
                        分からない（load_file()参照）。 */

/* ---- Undo ログ -------------------------------------------------------
 * つばきのバッファは単一の平文g_text[]＋行表なので、編集を「どこに
 * 何バイト挿入した／どこから何バイト削除した」の記録として残せる。
 * 挿入の取り消しはその範囲を削除するだけでよく、バイト列を保存する
 * 必要が無い。削除の取り消しは削除したバイト列を戻す必要があるので、
 * そのときだけバイト列を保存する（本タスクの指示に基づく設計。
 * docs/tsubaki-spec-01.mdにUndo専用の章はまだ無い）。
 *
 * 固定サイズのリングバッファ（UNDO_BUF_SIZE=2KB。目安どおり。ビルド後
 * の.mapで見たデータ使用量に対して十分な余裕がある）へ、可変長の
 * レコードを「ヘッダ→（削除のときだけ）バイト列→フッタ」の順で
 * 詰めていく自己記述形式で格納する。フッタにレコード全長を持たせて
 * おくことで、直近の書き込み位置から逆向きに1件だけ読み戻せる
 * （＝Undoスタックの先頭を、別の索引表を持たずに求められる）。
 *
 * レコード粒度は「1操作＝1記録」で固定する（打鍵をまとめない）。
 * まとめる実装（例：連続する文字入力を1件に合成する）は行のまたがりや
 * 上書きモードとの組合せで境界判定を誤りやすく、今回は単純さを優先して
 * すべてのbuffer_insert()/buffer_delete()呼び出しを1件ずつ記録する
 * （一括置換^Pの1回は複数回のbuffer_delete()/buffer_insert()になるが、
 * それぞれ独立した記録になる＝1回の^Pを1回の^Uで戻せる必要はない。
 * README.md参照）。
 *
 * リングが満杯のときは、新しい記録を書く前に末尾（最古）から記録を
 * 捨てて空きを作る。1件のレコードが空のリング全体よりも大きい場合
 * （非常に長い行を^Yで削除した場合など）はそもそも収まらないので、
 * その操作だけ記録をあきらめる（記録しないだけで構造は壊れない）。
 *
 * UNDO_BUF_SIZE(2048)を2の冪にしてあるのは、環状インデックスの計算を
 * 剰余ではなく&でのマスクにできるから（2048は65536の約数なので、
 * 16bit unsigned のまま引き算しても & (UNDO_BUF_SIZE-1) だけで正しく
 * 2048を法とした結果になる。SmallerCの16bit演算でも安全）。 */
#define UNDO_BUF_SIZE    2048             /* 2KB。目安どおり */
#define UNDO_MASK        (UNDO_BUF_SIZE - 1)
#define UNDO_TYPE_INSERT 1
#define UNDO_TYPE_DELETE 2
#define UNDO_HDR_SIZE    5   /* type(1) + absOffset(2) + len(2) */
#define UNDO_FOOTER_SIZE 2   /* レコード全長（自分自身込み）。逆向き読み出し用 */

#define UNDO_RESULT_EMPTY  0 /* 戻す記録が無い */
#define UNDO_RESULT_OK     1 /* 1件取り消した */
#define UNDO_RESULT_FAILED 2 /* 容量不足で戻せなかった（記録は保持するので、
                                 容量を空ければ再度^U/^Zで試せる） */

unsigned char g_undoBuf[UNDO_BUF_SIZE];
unsigned int g_undoWrite;   /* 次に書き込むバイト位置（環状） */
unsigned int g_undoUsed;    /* リングが現在使っているバイト数 */
int g_undoApplying;         /* 1のとき、buffer_insert()/buffer_delete()は
                                Undoログを取らない（undo_perform()自身が
                                内部でbuffer_insert()/buffer_delete()を
                                呼ぶ際の再帰記録を防ぐ。取ってしまうと
                                直後にRedoができてしまうが、今回はRedoを
                                作らない方針のため：意図的な抑制） */

/* undo_perform()が削除の取り消し（バイト列の再挿入）をするとき、環状
   バッファ上の（物理末尾をまたぎ得る）バイト列を一旦ここへ直線的に
   並べ直してからbuffer_insert()へ渡す。1レコードのpayload長は
   UNDO_BUF_SIZE-UNDO_HDR_SIZE-UNDO_FOOTER_SIZE未満までしか記録され
   得ないが、簡潔さのためUNDO_BUF_SIZEぶんを確保しておく。 */
char g_undoTmp[UNDO_BUF_SIZE];

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

/* 空きが足りないとき、末尾（最古）のレコードから捨てて場所を作る。
   呼び出し元はneededがUNDO_BUF_SIZE以下であることを保証すること。 */
void undo_evict_for(unsigned int needed)
{
  unsigned int tail;
  unsigned char t;
  unsigned int len;
  unsigned int recTotal;

  while (g_undoUsed > 0 && g_undoUsed + needed > UNDO_BUF_SIZE) {
    tail = (g_undoWrite - g_undoUsed) & UNDO_MASK;
    t = g_undoBuf[tail];
    len = (unsigned int)g_undoBuf[(tail + 3) & UNDO_MASK];
    len |= (unsigned int)g_undoBuf[(tail + 4) & UNDO_MASK] << 8;
    recTotal = UNDO_HDR_SIZE + UNDO_FOOTER_SIZE;
    if (t == UNDO_TYPE_DELETE) recTotal += len;
    g_undoUsed -= recTotal;
  }
}

/* type(UNDO_TYPE_INSERT/UNDO_TYPE_DELETE)・absOffset・len・（削除のときだけ）
   payload[0..len)を1レコードとしてリングへ積む。g_undoApplying中
   （Undo自身の適用中）は何もしない（g_undoApplyingのコメント参照）。
   1レコードが空のリング全体よりも大きい場合は記録をあきらめる
   （その操作だけがUndo対象から外れる。UNDO_BUF_SIZE直前のコメント
   参照）。 */
void undo_push(int type, int absOffset, int len, char *payload)
{
  unsigned int recTotal;
  unsigned int p;
  int i;

  if (g_undoApplying) return;

  recTotal = UNDO_HDR_SIZE + UNDO_FOOTER_SIZE;
  if (type == UNDO_TYPE_DELETE) recTotal += (unsigned int)len;
  if (recTotal > UNDO_BUF_SIZE) return;

  undo_evict_for(recTotal);

  p = g_undoWrite;
  g_undoBuf[p] = (unsigned char)type;
  p = (p + 1) & UNDO_MASK;
  g_undoBuf[p] = (unsigned char)(absOffset & 0xff);
  p = (p + 1) & UNDO_MASK;
  g_undoBuf[p] = (unsigned char)((absOffset >> 8) & 0xff);
  p = (p + 1) & UNDO_MASK;
  g_undoBuf[p] = (unsigned char)(len & 0xff);
  p = (p + 1) & UNDO_MASK;
  g_undoBuf[p] = (unsigned char)((len >> 8) & 0xff);
  p = (p + 1) & UNDO_MASK;
  if (type == UNDO_TYPE_DELETE) {
    for (i = 0; i < len; i++) {
      g_undoBuf[p] = (unsigned char)payload[i];
      p = (p + 1) & UNDO_MASK;
    }
  }
  g_undoBuf[p] = (unsigned char)(recTotal & 0xff);
  p = (p + 1) & UNDO_MASK;
  g_undoBuf[p] = (unsigned char)((recTotal >> 8) & 0xff);
  p = (p + 1) & UNDO_MASK;

  g_undoWrite = p;
  g_undoUsed += recTotal;
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
  /* Undoログ：この挿入の取り消しは[atOffsetAbs, atOffsetAbs+nBytes)を
     削除するだけでよいので、バイト列そのものは保存しない。 */
  undo_push(UNDO_TYPE_INSERT, atOffsetAbs, nBytes, 0);
  return 1;
}

void buffer_delete(int atOffsetAbs, int nBytes)
{
  if (nBytes <= 0) return;
  /* Undoログ：削除の取り消しには消えるバイト列そのものが要るので、
     memmove()で上書きされる前にここで記録する。 */
  undo_push(UNDO_TYPE_DELETE, atOffsetAbs, nBytes, g_text + atOffsetAbs);
  memmove(g_text + atOffsetAbs, g_text + atOffsetAbs + nBytes, (unsigned)(g_textLen - atOffsetAbs - nBytes));
  g_textLen -= nBytes;
  rebuild_lines();
  g_modified = 1;
}

/* absOffset（絶対バイト位置）が属する行と、その行内でのバイトオフセット
   を求める。呼び出し時点でg_lineStart[]/g_lineCountが最新であることが
   前提（buffer_insert()/buffer_delete()は必ずrebuild_lines()を通した
   後に戻ってくるので、undo_perform()から呼ぶ時点では常に満たされる）。
   他のバッファ走査と同様、正しさを優先して線形探索する（設計書2章と
   同じ方針）。 */
void locate_offset(int absOffset, int *outLine, int *outByte)
{
  int i;

  for (i = 0; i + 1 < g_lineCount; i++) {
    if (absOffset < (int)g_lineStart[i + 1]) break;
  }
  *outLine = i;
  *outByte = absOffset - (int)g_lineStart[i];
}

/* Undoログの先頭（もっとも新しい記録）を1件取り消す。
   戻り値はUNDO_RESULT_*（EMPTY＝戻す記録が無い、OK＝1件取り消した、
   FAILED＝容量不足で削除の取り消し・再挿入に失敗した。記録は保持され
   ポップされないので、容量を空ければ再試行できる）。
   取り消したあと、カーソルは取り消した編集の位置（戻した内容が見える
   位置）へ移動する。
   g_modifiedは0に戻さない：すべての記録を取り消しても、それが最初に
   ファイルを開いたときの内容と一致するかを判定する仕組みが無いため
   （単純さを優先した判断。README.md参照）。 */
int undo_perform(void)
{
  unsigned int p;
  unsigned int recTotal;
  unsigned int recStart;
  unsigned char type;
  unsigned int absOffset;
  unsigned int len;
  unsigned int i;
  unsigned int src;
  int line;
  int byteOff;

  if (g_undoUsed == 0) return UNDO_RESULT_EMPTY;

  /* 直前に書いたフッタ2バイト（レコード全長）を読み、そこから
     レコード先頭まで逆向きに飛ぶ。 */
  p = (g_undoWrite - UNDO_FOOTER_SIZE) & UNDO_MASK;
  recTotal = (unsigned int)g_undoBuf[p];
  recTotal |= (unsigned int)g_undoBuf[(p + 1) & UNDO_MASK] << 8;

  recStart = (g_undoWrite - recTotal) & UNDO_MASK;

  p = recStart;
  type = g_undoBuf[p];
  p = (p + 1) & UNDO_MASK;
  absOffset = (unsigned int)g_undoBuf[p];
  p = (p + 1) & UNDO_MASK;
  absOffset |= (unsigned int)g_undoBuf[p] << 8;
  p = (p + 1) & UNDO_MASK;
  len = (unsigned int)g_undoBuf[p];
  p = (p + 1) & UNDO_MASK;
  len |= (unsigned int)g_undoBuf[p] << 8;
  p = (p + 1) & UNDO_MASK;

  g_undoApplying = 1;
  if (type == UNDO_TYPE_INSERT) {
    /* 挿入の取り消し＝その範囲を削除するだけ。 */
    buffer_delete((int)absOffset, (int)len);
  } else {
    /* 削除の取り消し＝保存したバイト列を戻す。環状バッファ上では物理
       末尾をまたいでいる可能性があるので、一旦g_undoTmp[]へ直線的に
       並べ直してからbuffer_insert()へ渡す。 */
    src = p;
    for (i = 0; i < len; i++) {
      g_undoTmp[i] = (char)g_undoBuf[src];
      src = (src + 1) & UNDO_MASK;
    }
    if (!buffer_insert((int)absOffset, g_undoTmp, (int)len, 0)) {
      g_undoApplying = 0;
      return UNDO_RESULT_FAILED;
    }
  }
  g_undoApplying = 0;

  /* このレコードぶんをリングから取り除く（Undoは一度きりのスタック
     操作。Redoは作らない方針のため、取り消した記録は戻ってこない）。 */
  g_undoWrite = recStart;
  g_undoUsed -= recTotal;

  /* カーソルを取り消した編集の位置へ（戻した内容が見える位置）。 */
  locate_offset((int)absOffset, &line, &byteOff);
  g_curLine = line;
  g_curByte = byteOff;
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();

  return UNDO_RESULT_OK;
}

/* ^U / ^Z をUndoに割り当てる。main()の分岐チェーンへ直接足すと
   30個規律に触れる（docs/smallerc-pitfalls.md：29個までは通り30個で
   壊れる）ため、handle_search_replace_key()と同じやり方でこの関数へ
   まとめ、main()側は1分岐の増加に抑える。戻り値は「処理した(1)／
   対象のキーではなかった(0)」。 */
int handle_undo_key(int key)
{
  int r;

  if (key != KEY_CTRL_U && key != KEY_CTRL_Z) return 0;

  r = undo_perform();
  if (r == UNDO_RESULT_EMPTY) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_UNDO_NONE));
  } else if (r == UNDO_RESULT_FAILED) {
    sappend_copy(g_notice, sizeof(g_notice), MSG(MSG_UNDO_FAILED));
  }
  return 1;
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

/* ---- 検索・置換（v1） -----------------------------------------------
 * 実測根拠は docs/editor-measure-04.md（原物JEDの実測）。設計方針は
 * docs/tsubaki-spec-01.md 9章。原物の文言はいっさい流用せず、
 * メッセージはすべて自作の言い回しにしてある。
 *
 * 検索・置換語の入力は既存のinput_dialog()をそのまま使う。
 * input_dialog()は0x20-0x7Eの半角文字しか受け付けないため、v1の
 * 検索・置換語は半角のみになる（全角の検索語はまだ打てない。FEP
 * 経由で入力できるかどうかはこの版では確認していない）。
 * 検索語・置換語はどちらも半角のみなので、以降のコードは1文字=1
 * バイトという前提で長さを扱ってよい（strlen()がそのままバイト数=
 * 文字数になる）。 --------------------------------------------------- */

/* 検索の中核。haystack（1行ぶん。line_len()で渡された長さがハイストの
   長さで、'\n'を含まない）の中から、startByte以降で最初に一致する
   位置をバイトオフセットで返す（無ければ-1）。英大小は区別する
   （素朴なmemcmp()なのでそのまま区別される）。
   一致候補の開始位置は必ず文字境界（char_len_at()と同じ規則）である
   ことを確かめてから比較する。これにより、SJISの2バイト文字の2バイト目
   （例：帰=8B 41 の41、ア=83 41 の41）を単独の'A'として拾うことは
   ない――境界でない位置は決してiに現れないので、そこでの比較自体が
   起こらない（docs/editor-measure-04.md 結果3で実測した原物の挙動と
   同じにするための実装）。
   グローバル状態（g_text等）に依存しない純粋な関数にしてあるので、
   ホスト側の単体検証（tools/search_core_test.c、このファイルから
   is_lead_byte()とline_find()だけを切り出して直接呼ぶ）でもそのまま
   使える。 */
int line_find(char *hay, int hayLen, int startByte, char *term, int termLen)
{
  int i;
  int chLen;
  unsigned char c;

  if (termLen <= 0) return -1;

  i = startByte;
  while (i + termLen <= hayLen) {
    if (memcmp(hay + i, term, (unsigned)termLen) == 0) return i;
    c = (unsigned char)hay[i];
    chLen = (is_lead_byte(c) && i + 1 < hayLen) ? 2 : 1;
    i += chLen;
  }
  return -1;
}

/* fromLine行のfromByteバイト目（含む）以降で、term（NUL終端）に最初に
   一致する位置を文書全体から探す。行をまたいでも折り返さない
   （fromLineより前の行、およびg_lineCountを超えた先は探さない。
   設計方針9章「折り返しは行わない」）。見つかればoutLine/outByteへ
   一致文字列の先頭を書いて1を返し、無ければ0を返す。 */
int search_forward(int fromLine, int fromByte, char *term, int *outLine, int *outByte)
{
  int lineIdx;
  int start;
  int termLen;
  int pos;

  termLen = (int)strlen(term);
  if (termLen == 0) return 0;

  for (lineIdx = fromLine; lineIdx < g_lineCount; lineIdx++) {
    start = (lineIdx == fromLine) ? fromByte : 0;
    if (start < 0) start = 0;
    pos = line_find(g_text + g_lineStart[lineIdx], line_len(lineIdx), start, term, termLen);
    if (pos >= 0) {
      *outLine = lineIdx;
      *outByte = pos;
      return 1;
    }
  }
  return 0;
}

/* カーソルを一致位置へ移動し、画面のスクロール位置も追従させる。 */
void jump_to_match(int lineIdx, int byteOff)
{
  g_curLine = lineIdx;
  g_curByte = byteOff;
  g_goalCol = col_at_byte(g_curLine, g_curByte);
  ensure_visible();
}

/* lineIdx行のbyteOffバイト目にある、長さmatchLenバイトの一致箇所を
   rep[0..repLen)へ置き換える。置換後の全体サイズがTEXT_MAXを超える
   場合は何もせず0を返す（設計書2章「容量オーバーは黙って切り捨て
   ない」の方針どおり、削除より前に判定するので、失敗しても元の
   文字列は消えずに残る）。置換語は半角のみで改行を含み得ないので、
   buffer_insert()の行数上限チェック（newlineCount=0）に引っかかる
   ことはない。 */
int replace_at(int lineIdx, int byteOff, int matchLen, char *rep, int repLen)
{
  int abspos;
  int newTextLen;

  newTextLen = g_textLen - matchLen + repLen;
  if (newTextLen > TEXT_MAX) return 0;
  abspos = (int)g_lineStart[lineIdx] + byteOff;
  buffer_delete(abspos, matchLen);
  buffer_insert(abspos, rep, repLen, 0);
  return 1;
}

/* ^F：検索語を入力し、現在のカーソル位置から最初の一致へ移動する。
   検索語は次回以降も保持する（g_searchTermはダイアログの初期値に
   そのまま使われる）。 */
void search_command(void)
{
  int ok;
  int foundLine;
  int foundByte;

  ok = input_dialog(MSG(MSG_SEARCH_PROMPT), g_searchTerm, SEARCH_MAX, 0);
  if (!ok) return;                  /* ESC：検索語を変えずに戻る */
  if (g_searchTerm[0] == 0) return; /* 空の検索語では何もしない */

  if (search_forward(g_curLine, g_curByte, g_searchTerm, &foundLine, &foundByte)) {
    jump_to_match(foundLine, foundByte);
  } else {
    show_notice_dialog(MSG(MSG_SEARCH_NOT_FOUND));
  }
}

/* ^N・^L：直前に入力した検索語で再検索する。まだ検索語が無ければ
   何もしない。現在のカーソル位置がちょうど直前の一致の先頭にいる
   ことが多いので、同じ場所へ再度止まらないよう、一致した検索語の
   長さぶんだけ先へ進めてから探す（検索語は半角のみなのでバイト長=
   文字数）。この「1件分スキップしてから探す」規則は自作の判断で、
   原物の^Lが重複しうる一致に対してどう振る舞うかは
   docs/editor-measure-04.md では未実測（今回は複数一致の重なりを
   実測していない）。 */
void search_next_command(void)
{
  int termLen;
  int fromByte;
  int lineLen;
  int foundLine;
  int foundByte;

  if (g_searchTerm[0] == 0) return;

  termLen = (int)strlen(g_searchTerm);
  fromByte = g_curByte + termLen;
  lineLen = line_len(g_curLine);
  if (fromByte > lineLen) fromByte = lineLen;

  if (search_forward(g_curLine, fromByte, g_searchTerm, &foundLine, &foundByte)) {
    jump_to_match(foundLine, foundByte);
  } else {
    show_notice_dialog(MSG(MSG_SEARCH_NOT_FOUND));
  }
}

/* ^R：逐次置換。検索語→置換語の順に入力させたあと、現在のカーソル
   位置から一致するたびに確認ダイアログを出す。Yで置換してそのまま
   次の一致へ、Nで飛ばして次の一致へ進む。原物（docs/editor-measure-04.md
   結果4）はY/Nの2択しか提示しないが、この自作実装ではESCで置換
   ループそのものを中止できる経路を追加した（Y/Nしか無いと押し
   間違えたときに抜け出せず不便なため。すでに置換した分はそのまま
   残る＝ループの途中でESCしても一括では戻せない。ただし各置換は
   replace_at()内でbuffer_delete()＋buffer_insert()の2回に分かれて
   Undoログへ記録されるので、押し間違えた置換は^U/^Zを2回で1件ずつ
   戻せる）。 */
void replace_command(void)
{
  int ok;
  int termLen;
  int replaceLen;
  int foundLine;
  int foundByte;
  int fromLine;
  int fromByte;
  int key;
  int lineLen;

  ok = input_dialog(MSG(MSG_SEARCH_PROMPT), g_searchTerm, SEARCH_MAX, 0);
  if (!ok) return;
  if (g_searchTerm[0] == 0) return;

  ok = input_dialog(MSG(MSG_REPLACE_PROMPT), g_replaceTerm, REPLACE_MAX, 0);
  if (!ok) return; /* 置換語をESCすると何も変更せずに戻る（検索はまだ実行していない） */

  termLen = (int)strlen(g_searchTerm);
  replaceLen = (int)strlen(g_replaceTerm);
  fromLine = g_curLine;
  fromByte = g_curByte;

  for (;;) {
    if (!search_forward(fromLine, fromByte, g_searchTerm, &foundLine, &foundByte)) {
      draw_screen();
      show_notice_dialog(MSG(MSG_SEARCH_NOT_FOUND));
      return;
    }
    jump_to_match(foundLine, foundByte);
    draw_screen(); /* draw_dialog()は背景を描き直さないので、確認ダイアログの前に必ず1回描く */

    for (;;) {
      draw_dialog(MSG(MSG_REPLACE_CONFIRM), 0);
      key = key_read();
      if (key == 'y' || key == 'Y' || key == 'n' || key == 'N' || key == KEY_ESC) break;
    }
    if (key == KEY_ESC) return; /* 中止：ここまでの置換はそのまま残す */

    if (key == 'y' || key == 'Y') {
      if (!replace_at(foundLine, foundByte, termLen, g_replaceTerm, replaceLen)) {
        draw_screen();
        show_notice_dialog(MSG(MSG_REPLACE_LIMIT_STOP));
        return;
      }
      fromLine = foundLine;
      fromByte = foundByte + replaceLen;
    } else {
      fromLine = foundLine;
      fromByte = foundByte + termLen;
    }
    lineLen = line_len(fromLine);
    if (fromByte > lineLen) fromByte = lineLen;
  }
}

/* ^P：一括置換。確認なしで最後まで置換し、置換件数をステータス行の
   通知欄（g_notice）へ出す。開始位置は文書の先頭（0行0バイト目）に
   した――「一括」＝文書全体を置換するという自作側の判断で、原物の
   ^QAの挙動はdocs/editor-measure-04.mdでは未実測のため独自に決めた
   （^Fや^R が現在のカーソル位置から探すのとは対照的に、^Pだけは
   常に先頭から全件を対象にする）。
   置換のたびに容量（TEXT_MAX）を確かめ、超える手前で打ち切る。
   黙って切り捨てず、それまでの件数と「上限のため中断」の旨を
   同じ通知欄へまとめて出す。 */
void replace_all_command(void)
{
  int ok;
  int termLen;
  int replaceLen;
  int foundLine;
  int foundByte;
  int fromLine;
  int fromByte;
  int lineLen;
  int count;
  int hitLimit;
  int savedLine;
  int savedByte;
  int p;

  ok = input_dialog(MSG(MSG_SEARCH_PROMPT), g_searchTerm, SEARCH_MAX, 0);
  if (!ok) return;
  if (g_searchTerm[0] == 0) return;

  ok = input_dialog(MSG(MSG_REPLACE_PROMPT), g_replaceTerm, REPLACE_MAX, 0);
  if (!ok) return;

  termLen = (int)strlen(g_searchTerm);
  replaceLen = (int)strlen(g_replaceTerm);

  savedLine = g_curLine;
  savedByte = g_curByte;

  fromLine = 0;
  fromByte = 0;
  count = 0;
  hitLimit = 0;

  for (;;) {
    if (!search_forward(fromLine, fromByte, g_searchTerm, &foundLine, &foundByte)) break;
    if (!replace_at(foundLine, foundByte, termLen, g_replaceTerm, replaceLen)) {
      hitLimit = 1;
      break;
    }
    count++;
    fromLine = foundLine;
    fromByte = foundByte + replaceLen;
    lineLen = line_len(fromLine);
    if (fromByte > lineLen) fromByte = lineLen;
  }

  if (count > 0) {
    jump_to_match(fromLine, fromByte);
  } else {
    g_curLine = savedLine;
    g_curByte = savedByte;
    ensure_visible();
  }

  p = 0;
  g_notice[0] = 0;
  sappend(g_notice, &p, MSG(MSG_REPLACE_COUNT_PREFIX), sizeof(g_notice));
  sappend_uint(g_notice, &p, (unsigned int)count, sizeof(g_notice));
  sappend(g_notice, &p, hitLimit ? MSG(MSG_REPLACE_LIMIT_SUFFIX) : MSG(MSG_REPLACE_COUNT_SUFFIX), sizeof(g_notice));
}

/* main()の分岐チェーンに^F/^N・^L/^R/^Pの5個をそのまま「else if」で
   追加すると、既存の約25個と合わせて1本のelse-ifチェーンが30個近くに
   達し、SmallerCが「Undeclared identifier 'key'」という一見無関係な
   エラーでコンパイルに失敗する（実測：else-ifを29個までに抑えれば
   通り、30個で壊れた。else-ifチェーンはネストして再帰的にパースされる
   実装らしく、再帰が深くなりすぎるのが原因と見られる）。この関数へ
   まとめ、main()側の分岐は1個ぶんの増加に抑えることで回避している。
   戻り値は「処理した(1)／対象のキーではなかった(0)」。 */
int handle_search_replace_key(int key)
{
  if (key == KEY_CTRL_F) { search_command(); return 1; }
  if (key == KEY_CTRL_N) { search_next_command(); return 1; }
  if (key == KEY_CTRL_L) { search_next_command(); return 1; } /* 原物と同じ割り当て（^Nと同じ動作） */
  if (key == KEY_CTRL_R) { replace_command(); return 1; }
  if (key == KEY_CTRL_P) { replace_all_command(); return 1; }
  return 0;
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
   省いた版）

   以前はここでdraw_screen()を呼んで画面全体を描き直してから、その上に
   ダイアログを重ねていた。draw_fkey_row()の行24と同じ理由（同関数の
   コメント参照）で、これはダイアログの枠の下に隠れる本文セルへ「本文
   →枠」の2回書きを発生させ、文字入力中に実測でちらついた（利用者の
   報告どおり。行10-13のダイアログ帯で107フレーム中dip 8回・最小輝度
   684、静止時は92フレーム中0回・2125で一定）。呼び出し元（main()の
   モーダルループ）は各キー処理の最後に必ずdraw_screen()を1回だけ
   呼ぶため、ダイアログの下の背景はすでに直前のフレームで正しく
   描かれている。よってここでは背景を描き直さず、ダイアログの枠と
   内容だけを1セル1回書きで描く。閉じるとき（ESC・確定・エラー含む
   どの経路でも）の背景の復元は、呼び出し元に戻った後のその1回の
   draw_screen()に一本化されている（経路ごとには書かない）。
   ------------------------------------------------------------------- */

void draw_dialog(char *msg, char *errmsg)
{
  int i;
  int row;

  row = DIALOG_ROW;
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BORDER);
  row++;

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
  vram_puts_cells(row, DIALOG_COL + 1, msg, ATTR_VALUE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
  row++;

  /* エラー行は常に確保する（高さを固定するため。errmsgの有無で枠の
     高さが変わると、さっきまで枠だったセルを本文へ戻す処理が別途
     必要になり、そこがまた二重書きや消し残しの温床になる――すみれで
     経路ごとに消す対策をして再発させた前例と同じ形）。エラーが無い
     ときはDIALOG_WIDTH幅の空白で埋め、前回エラー行だった文字を
     残さない。 */
  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
  vram_puts_cells(row, DIALOG_COL + 1, errmsg != 0 ? errmsg : "", ATTR_VALUE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
  row++;

  vram_ank(row, DIALOG_COL, BOXCH_BL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_BR, ATTR_BORDER);
}

void show_notice_dialog(char *msg)
{
  draw_dialog(msg, 0);
  key_read();
}

/* draw_dialog()と同じ理由でdraw_screen()を呼ばず、枠と内容だけを1セル
   1回書きで描く（上のコメント参照）。エラー行を常に確保するのも同じ
   理由：input_dialog()のモーダルループはerrmsgをキー入力のたびに
   有り／無しへ行き来させる（BSや通常文字の入力でerrmsg=0に戻す）ため、
   ここで高さを固定しないと同じループの中で枠の高さが変わってしまう。 */
void draw_input_box(char *prompt, char *buf, int len, char *errmsg)
{
  char line[DIALOG_WIDTH * 2 + FILENAME_MAX + 4];
  int i;
  int row;
  int fieldRow;
  int fieldCol;
  int p;
  int promptCells;

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

  vram_ank(row, DIALOG_COL, BOXCH_V, ATTR_BORDER);
  vram_puts_cells(row, DIALOG_COL + 1, errmsg != 0 ? errmsg : "", ATTR_VALUE, DIALOG_WIDTH);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_V, ATTR_BORDER);
  row++;

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

/* ---- 引数なし起動のファイル選択 -----------------------------------------
 * 引数を1つも与えずに起動したときに開く一覧。原物 JED 1.59c の同じ
 * 入口を実測した結果（docs/editor-measure-05.md）に合わせてある：
 *
 *   ・幅40セルのウィンドウを桁20から。上枠の中に「パス＋パターン」、
 *     一覧は17行、下枠の中にキーガイド
 *   ・1カラム。ディレクトリは名前の末尾に '\' が付き、サイズ欄が <DIR>
 *   ・親は ".." として先頭に出る（ルートでは出ない）
 *   ・並べ替えはしない。DOS が返す順（＝ディレクトリエントリの並び）
 *     そのまま。原物もディレクトリを先出ししていない（実測）
 *   ・↑↓は1行で端では止まる。←→はページ単位。BS で親へ。
 *     Enter はディレクトリなら入り、ファイルなら選んで編集を始める。
 *     ESC はプログラムごと終了
 *
 * 原物と変えたところ（意図的）:
 *   ・**マークを持たない。**原物は SPACE で複数選び、選んだぶんを複数
 *     ウィンドウへ読み込む。つばきは1ファイル1画面なので、マークは
 *     「Enter はカーソル位置を開く」を壊すだけになる
 *   ・ROLL UP/ROLL DOWN も←→と同じページ送りに割り当てた（本文の
 *     ページ送りと同じキーで動くほうが迷わないため）。原物では未確認
 *   ・原物の [L]（ドライブ／パス／パターンの入力）は入れていない。
 *     ドライブをまたぐ必要があるときは、いまのところ引数で渡すこと
 *
 * 画面の文言はこのプログラムのために書き下ろしたもので、原物の表示を
 * 写したものではない（docs/reimpl-policy.md）。 */

#define PICK_MAX          200  /* 一覧に取り込むエントリ数の上限。超えた分は
                                  黙って捨てず、上枠に打ち切りを表示する */
#define PICK_NAME_LEN     13   /* "NAME.EXT" + NUL（DOSが返す形式） */
#define PICK_ROWS         17   /* 一覧の行数（原物の実測値と同じ） */
#define PICK_COL          20   /* ウィンドウの左端の桁（同上） */
#define PICK_INNER_WIDTH  38   /* 枠の内側のセル数（幅40 － 左右の枠2） */
#define PICK_TOP_ROW      5    /* 上枠（パスを載せる） */
#define PICK_FIRST_ROW    (PICK_TOP_ROW + 1)
#define PICK_GUIDE_ROW    (PICK_FIRST_ROW + PICK_ROWS)
#define PICK_PATH_MAX     80   /* g_pickPath[]（ドライブとバックスラッシュ込み） */

#define PICK_ATTR_HIDDEN   0x02
#define PICK_ATTR_SYSTEM   0x04
#define PICK_ATTR_VOLLABEL 0x08
#define PICK_ATTR_DIR      0x10
#define PICK_ATTR_RDONLY   0x01

unsigned char g_dta[43];

char g_pickPath[PICK_PATH_MAX];   /* 末尾は必ず '\\' */
char g_pickSearch[PICK_PATH_MAX + 8];
char g_pickedPath[PICK_PATH_MAX + PICK_NAME_LEN]; /* 選ばれたファイルの絶対パス。
                                                      main()がg_argFileの代わりに使う */
char g_pickName[PICK_MAX * PICK_NAME_LEN];
unsigned char g_pickAttr[PICK_MAX];
unsigned int g_pickSizeLo[PICK_MAX];
unsigned int g_pickSizeHi[PICK_MAX];
unsigned int g_pickDate[PICK_MAX];
unsigned int g_pickTime[PICK_MAX];
int g_pickCount;
int g_pickCursor;
int g_pickTop;
int g_pickTruncated;

/* (hi:lo) を10進のASCIIへ。out[]は12バイト以上。すみれの
   format_u32_plain()と同じ（カンマ区切りは入れない）。 */
void pick_format_size(unsigned int hi, unsigned int lo, char *out)
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
  for (i = 0; i < n; i++) out[i] = digits[n - 1 - i];
  out[n] = 0;
}

/* DOSディレクトリエントリのパックされた日付／時刻を "YY-MM-DD" と
   "HH:MM" へ。年はDOSの1980年起点。 */
void pick_format_date(unsigned int d, char *out)
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

void pick_format_time(unsigned int t, char *out)
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

/* 現在のドライブとカレントディレクトリから g_pickPath を作る。
   末尾は必ず '\\'。 */
void pick_read_path(void)
{
  unsigned int drive;
  char cwdbuf[68];
  int ok;

  drive = dos_getdrive();
  ok = dos_getcwd(drive + 1, cwdbuf); /* AH=47hは1がA: */
  if (ok != 0) cwdbuf[0] = 0;

  g_pickPath[0] = (char)('A' + drive);
  g_pickPath[1] = ':';
  g_pickPath[2] = '\\';
  g_pickPath[3] = 0;
  strcat(g_pickPath, cwdbuf);
  if (cwdbuf[0] != 0) strcat(g_pickPath, "\\");
}

/* g_pickPath の中身を読み直す。"." は捨て、".." は残す（原物も親だけ
   出す）。ボリュームラベルは一覧に出さない。 */
void pick_read_dir(void)
{
  int ok;
  int idx;
  unsigned char attr;

  g_pickCount = 0;
  g_pickTruncated = 0;

  strcpy(g_pickSearch, g_pickPath);
  strcat(g_pickSearch, "*.*");

  dos_setdta(g_dta);
  ok = dos_findfirst(g_pickSearch,
                     PICK_ATTR_RDONLY | PICK_ATTR_HIDDEN | PICK_ATTR_SYSTEM | PICK_ATTR_DIR);
  while (ok == 0) {
    attr = g_dta[21];
    if (!(attr & PICK_ATTR_VOLLABEL) && strcmp((char *)&g_dta[30], ".") != 0) {
      if (g_pickCount < PICK_MAX) {
        idx = g_pickCount;
        strcpy(&g_pickName[idx * PICK_NAME_LEN], (char *)&g_dta[30]);
        g_pickAttr[idx] = attr;
        g_pickTime[idx] = (unsigned int)g_dta[22] | ((unsigned int)g_dta[23] << 8);
        g_pickDate[idx] = (unsigned int)g_dta[24] | ((unsigned int)g_dta[25] << 8);
        g_pickSizeLo[idx] = (unsigned int)g_dta[26] | ((unsigned int)g_dta[27] << 8);
        g_pickSizeHi[idx] = (unsigned int)g_dta[28] | ((unsigned int)g_dta[29] << 8);
        g_pickCount++;
      } else {
        g_pickTruncated = 1;
        break;
      }
    }
    ok = dos_findnext();
  }

  g_pickCursor = 0;
  g_pickTop = 0;
}

/* 一覧1行ぶんの本文（PICK_INNER_WIDTHセル、すべてANK）を作る。
   桁の割り付け：
     0        空白
     1～12    名前（ファイルは 8 + '.' + 3 の固定欄、ディレクトリは名前＋'\'）
     13～14   空白
     15～21   サイズ（右詰め7桁。ディレクトリは <DIR>）
     22～23   空白
     24～31   日付 YY-MM-DD
     32       空白
     33～37   時刻 HH:MM
*/
void pick_entry_text(int idx, char *out)
{
  char *name;
  char sizeBuf[12];
  char dateBuf[9];
  char timeBuf[6];
  int i;
  int j;
  int n;

  for (i = 0; i < PICK_INNER_WIDTH; i++) out[i] = ' ';
  out[PICK_INNER_WIDTH] = 0;

  name = &g_pickName[idx * PICK_NAME_LEN];

  if (g_pickAttr[idx] & PICK_ATTR_DIR) {
    i = 0;
    while (name[i] != 0 && i < 11) { out[1 + i] = name[i]; i++; }
    out[1 + i] = '\\';
  } else {
    i = 0;
    while (name[i] != 0 && name[i] != '.' && i < 8) { out[1 + i] = name[i]; i++; }
    j = i;
    while (name[j] != 0 && name[j] != '.') j++; /* 8文字を超える名前は起きないが、
                                                    '.' の位置まで進めておく */
    out[9] = '.';
    if (name[j] == '.') {
      j++;
      i = 0;
      while (name[j] != 0 && i < 3) { out[10 + i] = name[j]; i++; j++; }
    }
  }

  if (g_pickAttr[idx] & PICK_ATTR_DIR) {
    strcpy(sizeBuf, "<DIR>");
  } else {
    pick_format_size(g_pickSizeHi[idx], g_pickSizeLo[idx], sizeBuf);
  }
  n = (int)strlen(sizeBuf);
  if (n > 7) n = 7; /* 7桁に収まらない値（10億バイト超）は欄からはみ出させない */
  for (i = 0; i < n; i++) out[15 + (7 - n) + i] = sizeBuf[i];

  pick_format_date(g_pickDate[idx], dateBuf);
  for (i = 0; i < 8; i++) out[24 + i] = dateBuf[i];
  pick_format_time(g_pickTime[idx], timeBuf);
  for (i = 0; i < 5; i++) out[33 + i] = timeBuf[i];
}

/* 枠の行（上枠／下枠）に文字列を載せる。まず罫線で内側を埋めてから、
   文字の幅ぶんだけ上書きする。vram_puts_cells() は指定幅を空白で
   埋めてしまうので、幅には文字列自身の幅を渡す。 */
void pick_draw_frame_row(int row, unsigned char left, unsigned char right, char *text)
{
  int i;
  int w;

  vram_ank(row, PICK_COL, left, ATTR_BORDER);
  for (i = 0; i < PICK_INNER_WIDTH; i++) {
    vram_ank(row, PICK_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  }
  vram_ank(row, PICK_COL + 1 + PICK_INNER_WIDTH, right, ATTR_BORDER);

  if (text != 0 && text[0] != 0) {
    w = text_width(text);
    if (w > PICK_INNER_WIDTH) w = PICK_INNER_WIDTH;
    vram_puts_cells(row, PICK_COL + 1, text, ATTR_TITLE, w);
  }
}

void pick_draw(void)
{
  int row;
  int col;
  int i;
  int idx;
  int p;
  char line[PICK_INNER_WIDTH + 1];
  char title[PICK_PATH_MAX + 32];
  unsigned int attr;

  /* ウィンドウの外は空白で覆う。vram_set_cell() が「変化したセルだけ
     書く」ので、これは毎回の全画面書き込みにはならない。 */
  for (row = 0; row < VRAM_ROWS; row++) {
    for (col = 0; col < VRAM_COLS; col++) {
      vram_ank(row, col, ' ', ATTR_BASE);
    }
  }

  p = 0;
  title[0] = 0;
  sappend(title, &p, g_pickPath, sizeof(title));
  if (g_pickTruncated) sappend(title, &p, MSG(MSG_PICK_TRUNCATED), sizeof(title));
  pick_draw_frame_row(PICK_TOP_ROW, BOXCH_TL, BOXCH_TR, title);

  for (i = 0; i < PICK_ROWS; i++) {
    row = PICK_FIRST_ROW + i;
    idx = g_pickTop + i;
    vram_ank(row, PICK_COL, BOXCH_V, ATTR_BORDER);
    if (idx < g_pickCount) {
      pick_entry_text(idx, line);
      attr = (idx == g_pickCursor) ? ATTR_FKEY_KEY : ATTR_VALUE;
      vram_puts_cells(row, PICK_COL + 1, line, attr, PICK_INNER_WIDTH);
    } else if (g_pickCount == 0 && i == 0) {
      vram_puts_cells(row, PICK_COL + 1, MSG(MSG_PICK_EMPTY), ATTR_VALUE, PICK_INNER_WIDTH);
    } else {
      vram_puts_cells(row, PICK_COL + 1, "", ATTR_VALUE, PICK_INNER_WIDTH);
    }
    vram_ank(row, PICK_COL + 1 + PICK_INNER_WIDTH, BOXCH_V, ATTR_BORDER);
  }

  pick_draw_frame_row(PICK_GUIDE_ROW, BOXCH_BL, BOXCH_BR, MSG(MSG_PICK_GUIDE));
}

void pick_ensure_visible(void)
{
  if (g_pickCursor < g_pickTop) g_pickTop = g_pickCursor;
  if (g_pickCursor >= g_pickTop + PICK_ROWS) g_pickTop = g_pickCursor - PICK_ROWS + 1;
  if (g_pickTop < 0) g_pickTop = 0;
}

/* ページ送り（←→）。原物は窓をページ単位で動かすので、カーソルは
   新しいページの先頭へ置く。行スクロール（↑↓）とは別物。 */
void pick_page(int dir)
{
  int top;

  if (g_pickCount == 0) return;
  top = g_pickTop + dir * PICK_ROWS;
  if (top > g_pickCount - PICK_ROWS) top = g_pickCount - PICK_ROWS;
  if (top < 0) top = 0;
  g_pickTop = top;
  g_pickCursor = top;
}

/* g_pickPath を親ディレクトリへ。ドライブルート "X:\\" より上へは行かない。 */
void pick_go_parent(void)
{
  int len;

  len = (int)strlen(g_pickPath);
  if (len > 0 && g_pickPath[len - 1] == '\\') len--;
  while (len > 0 && g_pickPath[len - 1] != '\\') len--;
  if (len < 3) len = 3;
  g_pickPath[len] = 0;
  pick_read_dir();
}

void pick_enter_dir(int idx)
{
  char *name;
  int p;

  name = &g_pickName[idx * PICK_NAME_LEN];
  if (strcmp(name, "..") == 0) {
    pick_go_parent();
    return;
  }
  p = (int)strlen(g_pickPath);
  sappend(g_pickPath, &p, name, PICK_PATH_MAX);
  sappend(g_pickPath, &p, "\\", PICK_PATH_MAX);
  pick_read_dir();
}

/* 選択されたら1を返し、outPath へ絶対パスを入れる。ESCで中止したら0。
   画面はこの関数が丸ごと使う（呼び出し元は戻ってから描き直すこと）。 */
int pick_file(char *outPath, int cap)
{
  int key;
  int idx;
  int p;

  pick_read_path();
  pick_read_dir();

  for (;;) {
    pick_ensure_visible();
    pick_draw();
    key = key_read();

    if (key == KEY_ESC) return 0;

    if (key == KEY_UP) {
      if (g_pickCursor > 0) g_pickCursor--;
    } else if (key == KEY_DOWN) {
      if (g_pickCursor + 1 < g_pickCount) g_pickCursor++;
    } else if (key == KEY_RIGHT || key == KEY_ROLLUP) {
      pick_page(1);
    } else if (key == KEY_LEFT || key == KEY_ROLLDOWN) {
      pick_page(-1);
    } else if (key == KEY_HOME) {
      g_pickCursor = 0;
      g_pickTop = 0;
    } else if (key == KEY_BS) {
      pick_go_parent();
    } else if (key == KEY_ENTER) {
      if (g_pickCount == 0) continue;
      idx = g_pickCursor;
      if (g_pickAttr[idx] & PICK_ATTR_DIR) {
        pick_enter_dir(idx);
      } else {
        p = 0;
        outPath[0] = 0;
        sappend(outPath, &p, g_pickPath, cap);
        sappend(outPath, &p, &g_pickName[idx * PICK_NAME_LEN], cap);
        return 1;
      }
    }
    /* それ以外のキーは無視する */
  }
}

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
  g_searchTerm[0] = 0;
  g_replaceTerm[0] = 0;
  g_undoWrite = 0;
  g_undoUsed = 0;
  g_undoApplying = 0;

  /* 引数を1つも与えられていないときは、空のバッファをいきなり開くの
     ではなくファイル選択の一覧を出す（原物 JED と同じ入口。
     docs/editor-measure-05.md）。ESCで中止されたら、何も開かずに
     そのまま終了する――原物も同じで、新規バッファは開かない。 */
  if (g_argFile == 0) {
    write_str("\x1b[>1h"); /* 最下段のファンクションキー行を解放する */
    write_str("\x1b[>5h"); /* 一覧の間はテキストカーソルを隠す */
    vram_shadow_init();
    if (!pick_file(g_pickedPath, sizeof(g_pickedPath))) {
      vram_clear_all();
      ansi_goto(0, 0);
      write_str("\x1b[>5l");
      write_str("\x1b[>1l");
      return 0;
    }
    g_argFile = g_pickedPath;
  }

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
      draw_screen(); /* draw_dialog()はもう背景を描かないので、通知の下地をここで1回だけ用意する */
      show_notice_dialog(r == LOAD_TOO_LARGE ? MSG(MSG_LOAD_TOO_LARGE) : MSG(MSG_LOAD_TOO_MANY_LINES));
    }
  }

  write_str("\x1b[>1h"); /* 最下段のファンクションキー行を解放する */
  write_str("\x1b[>5l"); /* テキストカーソルを表示する（編集中は常時表示） */

  vram_shadow_init();

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
    else if (handle_search_replace_key(key)) { /* ^F/^N・^L/^R/^P（下のコメント参照） */ }
    else if (handle_undo_key(key)) { /* ^U/^Z：Undo（下のコメント参照） */ }
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
