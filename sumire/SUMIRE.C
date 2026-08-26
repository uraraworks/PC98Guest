/*
 * SUMIRE.C - PC-98 / FreeDOS(98) 用ディレクトリブラウザ（第7マイルストーン）
 * 第7マイルストーンで、このプログラムの名前を（それまでの仮名 FILER から）
 * Sumire/すみれ に改名し、実測で見つかった行0のちらつきを修正し
 * （下の draw_title_row() 参照）、ディスク合計行の数値を右詰めにして
 * 単位サフィックスを外し（draw_disk_line() 参照）、本家自身の
 * プログラム起動コマンドである F2/eXec（do_exec()）を本家と同じ F2 の
 * 位置に追加した。
 *
 * ゼロから独立に実装したもの。独立性についての宣言はこのディレクトリの
 * README.md を参照。以下の画面表示文字列はすべてこのプロジェクトのために
 * 書き下ろしたオリジナルの文言であり、既存製品のいかなる文字列も再現
 * していない。
 *
 * 第1マイルストーンの範囲：ディレクトリ一覧表示・カーソル移動・終了のみ。
 * 第2マイルストーンで追加：ファイルのマーク（SPACE/TAB/HOME）、
 * ディレクトリへの出入り（Enter、"."と".."は本家同様に表示）、
 * マークされたファイル（何もマークされていなければカーソル位置の
 * ファイル）を確認ダイアログ付きで削除。ディレクトリは決してマークされず、
 * このコマンドで削除されることもない。
 * 第3マイルストーンで追加：再利用可能なモーダルテキスト入力ダイアログ
 * （input_dialog()）。Rename（R、カーソル位置の1件のみ対象）と
 * mKdir（K、カレントパスにディレクトリを作成）が共用する。どちらも
 * 同じダイアログを使い回し、失敗メッセージを別行にせず同じボックス内に
 * インラインで表示することで、プロンプトと入力途中の文字列を画面に
 * 残したままにする。
 * 第4マイルストーンで追加：Copy（C）と Move（M）。ターゲットの決め方は
 * Delete と同じ「マークが空でなければマーク集合、空ならカーソル位置の
 * 1件」というルールで、ファイルごとに Y/N/ESC の上書き確認を行う
 * （本家はプロンプトなしで同名コピーを一律拒否する。この実装が代わりに
 * 尋ねる理由は README の独立性に関する注記を参照）。また最下段の
 * コマンド行も置き換え、CMDLINE_WIDTH セルに収まる長い日英併記の
 * 「キー:説明」メッセージ1行だった従来方式をやめ、英語のコマンド単語と
 * そのキー文字を反転表示で示す方式にした。
 * 第5マイルストーンで、DOSコンソール/ANSIによる画面書き込みを廃止し、
 * テキストVRAMへの直接書き込みに置き換えた（下記参照）。ヘッダ／ダイアログの
 * 枠を本家自身の半角罫線コードで描くようにし（これに伴い BOX_WIDTH は
 * 76から78セルへ拡張）、カーソル行／コマンドキーの強調表示は ESC[7m の
 * 代わりにVRAM属性バイトの反転ビットで再現し、画面全体をクリアしてから
 * 描き直す方式をやめて、フレーム間で実際に変化したセルだけを書くように
 * した。
 *
 * 第12マイルストーンで追加：Logdsk（L、F1）――カレントドライブの変更。
 * 原物と同じF1の位置に置いた。切り替え先の有効性はINT 21h AH=36hで
 * 事前に確かめ、切り替えた後にAH=19hで実際に移ったことを確認する
 * （do_logdsk()参照）。
 *
 * SmallerC（C89寄りのサブセット、16ビット・スモールモデルの MZ EXE）で
 * ビルド。第5マイルストーンで、画面の*内容*描画はすべて DOS コンソールから
 * テキストVRAMへの直書き（下の vram_* 関数群、および
 * docs/tvram-measure-01.md / docs/filer-measure-03.md 参照）へ移した。
 * これにより、（1）このフォントには全角罫線文字のグリフが無く
 * 第1～4マイルストーンで使っていたヘッダ枠が実測すると何も表示されて
 * いなかった問題、（2）毎フレーム ESC[2J を送っていたことによる
 * ちらつき、（3）DOSコンソールは 0x81-0x9F を Shift_JIS の先頭バイトと
 * 解釈するため通せなかった本家自身の半角罫線コードが使えなかった問題、
 * の3つを解決した。ごく一部の純ASCIIな処理（ハードウェアテキスト
 * カーソルの表示／非表示、input_dialog() 中のカーソル位置決め、最下段
 * ファンクションキー行の解放・復元）は引き続きDOSコンソールのANSI風
 * エスケープシーケンスを通す（WebNP2/FreeDOS(98)で実測。
 * docs/escape-measure-01.md 参照）。これらはSJISテキストを一切運ばないため
 * VRAM書き込みで解決した問題の影響を受けない。ディレクトリアクセスは
 * 素の DOS INT 21h サービス（AH=1Ah/4Eh/4Fh/47h/19h/36h）を使っており、
 * これらは文書化された汎用DOS APIであって特定のプログラムのソースコード
 * に由来するものではない。
 *
 * 画面出力は一切 stdio を経由しない。printf()/puts()/putchar() を
 * 画面表示に使ってはならない。
 *
 * 画面文字列は日英バイリンガル対応（下のメッセージテーブル参照）。
 * コマンドラインスイッチ /J（日本語、既定）または /E（英語）で選択する。
 *
 * 第6マイルストーンで、第4マイルストーンのコマンド行を実測した本家の
 * 最下段行に置き換えた：ファンクションキー10個ぶんの固定フィールド
 * （g_fkeyLabel[]/g_fkeyCol[]/draw_cmdline() および
 * docs/filer-measure-05.md 参照）で、F3/F4/F5は本家自身のCopy/Delete/
 * Renameの位置のまま、このプログラムの他のコマンドは空いている枠に
 * 配置する。このプログラムが実装していないコマンド（Sort/Find/Tree/…）
 * の枠は、名前を付けず空欄のままにする。すべてのコマンドは
 * 従来どおり普通の文字キーでも到達できる――ファンクションキーは
 * あくまで追加の入口である。
 * 第9マイルストーンで、行0のタイトル文字列を "<< >>" の中央に配置し
 * （下の draw_title_row() 参照）、キー入力を DOS INT 21h AH=08h から
 * BIOS INT 18h AH=00h へ移行した（下の dos_getch()/dos_kbhit() および
 * docs/dos-key-measure-01.md 参照）：DOSのコンソールドライバは ^S を
 * XOFF として飲み込み、ROLL UP/ROLL DOWN/HELP は一切届かなかった
 * （すべて実測済み）。画面出力はすでに第5マイルストーンで直接VRAM書き込みに
 * 移っているため、これらはもはやDOSコンソールを一切経由しない。
 * INT 18h はファンクションキーをそれ自身のスキャンコードとして直接
 * 報告してくるため、第6マイルストーンで必要だった旧来の
 * 「0x1Bを受けてから dos_kbhit() で追いのバイトが来ているか確認する」
 * トリックはこれとともに不要になった――下の KEY_* / dos_getch() の
 * コメント参照。
 */

#include <string.h>

/* ---- 定数 ------------------------------------------------------ */

#define MAX_ENTRIES   1024
#define NAME_LEN      13     /* 8.3形式の名前 + ドット + NUL。DOSが返す形式に合わせている */
#define INPUT_MAXLEN  12     /* input_dialog() で入力できる最大文字数（NULを含まない）：
                                 8.3形式の名前で、NAME_LEN-1 と同じ上限。
                                 do_rename()/do_mkdir() 参照 */
#define DEST_MAXLEN   40     /* Copy/Move の移動先ディレクトリ（例 "B:\SUBDIR"）として入力できる
                                 最大文字数。これはパスであって8.3名では
                                 ないため、INPUT_MAXLENとは異なる（より長い）
                                 上限になっている。do_copy()/do_move() 参照 */
#define COPY_BUF_SIZE 4096   /* ファイルコピー用に共有するグローバルバッファ g_copybuf の
                                 サイズ。その宣言部分も参照 */
#define LEFT_ROWS     17
#define VISIBLE_MAX   34     /* 17行×2列。ページング未実装（第1マイルストーンの制約）に
                                 ついてはREADMEを参照 */

/* ---- カーソル移動方向（move_cursorで使用） ---------------------- */
#define DIR_UP    1
#define DIR_DOWN  2
#define DIR_LEFT  3
#define DIR_RIGHT 4

/* ---- キーコード（下の dos_getch() が返す値） ---------------------
 * 第9マイルストーンで dos_getch()/dos_kbhit() を DOS INT 21h AH=08h/0Bh
 * から BIOS INT 18h AH=00h/01h へ移した（該当の関数は下の方にある）が、
 * これらの値そのものは DOS の頃と全く同じ値に保ってあるので、この
 * ファイルの他の場所でこれらと比較している箇所は一切変更していない。
 * INT 18h AH=00h は AH=スキャンコード、AL=文字コードを返し、文字を
 * 持たないキーでは AL=0 になる――すべてプローブ用プログラムで実測済み。
 * docs/key-measure-01.md、docs/key-measure-02.md、docs/tsukushi-keys.md、
 * docs/dos-key-measure-01.md 参照。dos_getch() 自身がAL／スキャンコードの
 * 変換を行い（生のスキャンコードを見るのはこの関数だけ）、次のように
 * 返す：
 *   - AL が0でなければ AL をそのまま返す――これは下の素の値
 *     （BS/TAB/ENTER/SPACE/ESC/^E/^X）すべてに既に一致している。
 *     INT 18h でのこれらキーのALは、DOSのAH=08hがかつて返していたのと
 *     数値として同じバイトだからである（これも実測済み）。
 *   - ALが0（文字を持たない）のときは、スキャンコードから変換した
 *     KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT/KEY_HOME/KEY_F1..KEY_F10/
 *     KEY_ROLLUP/KEY_ROLLDOWN のいずれかを返す。
 *   - それ以外の文字を持たないキー（このプログラムでは未割り当て。
 *     例：HELP/INS/DEL――まだ用途を与えていない。README.mdのTODO参照）
 *     は0を返す。
 * ------------------------------------------------------------------- */
#define KEY_UP     0x0b
#define KEY_DOWN   0x0a
#define KEY_LEFT   0x08   /* BSと同じコードだが、このリストの中では左方向として扱う */
#define KEY_RIGHT  0x0c
#define KEY_HOME   0x1a
#define KEY_ESC    0x1b
#define KEY_TAB    0x09
#define KEY_SPACE  0x20
#define KEY_ENTER  0x0d
/* WordStar風の代替キー、上下移動のみ。^S（0x13、WordStar方式では
 * 「左」）は絶対にここで割り当ててはならない：実機のDOSコンソール
 * ドライバは ^S を XOFF として扱い、^Q が押されるまで画面出力を
 * 凍結させる（実機で確認済み。ただしこのプログラムは第9マイルストーン
 * 以降、入力にDOSコンソールをもう使っていないため、^S自体はINT 18h
 * に対して直接再実測していない――なので推測で割り当てず、未割当の
 * ままにしてある）。^Dも未割当のままにしてある：^E/^Xと違って何かと
 * 衝突しないことを確認していないため。 */
#define KEY_CTRL_E 0x05   /* 上方向の代替キー */
#define KEY_CTRL_X 0x18   /* 下方向の代替キー */

/* ファンクションキー用の疑似コード。dos_getch() は INT 18h の
 * スキャンコードが F1-F10（0x62-0x6Bと実測済み。docs/key-measure-01.md/
 * -02.md 参照）のときにこれらを返す。実際のALバイトや上の制御コード
 * 定数の範囲外である 0xFF より大きい値を選んであるので、文字キー
 * 入力と衝突することは決してない。旧DOSコンソール方式（0x1Bに文字を
 * 続けてキューに積む方式。これが置き換えた、削除済みの FKEY_CODE_*
 * 定数を参照）と異なり、INT 18h はファンクションキーを単一の
 * 独立したスキャンコードとして報告してくるため、main() のディスパッチ
 * はESCの後で dos_kbhit() を覗いて両者を区別する必要がもうない。 */
#define KEY_F1  0x100
#define KEY_F2  0x101
#define KEY_F3  0x102
#define KEY_F4  0x103
#define KEY_F5  0x104
#define KEY_F6  0x105
#define KEY_F7  0x106
#define KEY_F8  0x107
#define KEY_F9  0x108
#define KEY_F10 0x109

/* ROLL UP/ROLL DOWN：実機のINT 18hに対して実測したスキャンコード
 * （docs/bios-key-measure-01.md：ROLL UP=0x36、ROLL DOWN=0x37、
 * どちらもAL=0）。組み込みビューア（do_view()）の中でのみ割り当てて
 * ある。その挙動も実機で実測済みなのはここだけ（docs/filer-measure-07.md：
 * ROLL UPはVIEW_CONTENT_ROWS行ぶん進み、ROLL DOWNは同じ量だけ戻って
 * 先頭行で止まる）――ファイラ自身の一覧表示ではこれらのキーは使わない
 * （一覧側のページング自体が未実装。README.mdのTODO参照）。 */
#define KEY_ROLLUP   0x10a
#define KEY_ROLLDOWN 0x10b

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
#define ROW_GAP       23  /* 一覧とファンクションキー行の間の行。どちらの通常描画も
                              触れない――方針A：draw_screen_frame()側で明示的に埋める */
#define ROW_CMD       24

#define CMDLINE_WIDTH 79  /* 最下段の80桁目（0始まりで79桁目）に書き込むとコンソールが
                              スクロールしてしまう。draw_screen_frame() が
                              コマンド行を切り詰めている理由はこれ */

#define COL_LEFT      0
#define COL_RIGHT     40
#define COL_GAP       39  /* 左右2列の一覧の間、1セル分の隙間 */
#define COL_LAST      79  /* 画面の最終列。どちらの列描画（幅39セル）も届かない */

/* ---- ヘッダボックス（0～5行目）を半角罫線文字で描画 ----
 * 第5マイルストーン：画面出力をテキストVRAMへの直接書き込みに移した
 * （下の vram_* 関数群参照）ので、半角の単線罫線文字（0x9C-0x9Fなど）を
 * そのまま使えるようになった――0x81-0x9Fを Shift_JIS の先頭バイトと
 * 解釈して化けさせていたのは、DOSコンソール側の出力経路（ANSI.SYSを
 * 通るAH=40h）だけだった（実測済み。docs/filer-measure-03.md参照）。
 * これらはANK（画面1セル分）のコードでありSJISテキストではないので、
 * ここではCの文字列ではなく単なるバイト定数として扱っている――
 * vram_puts_cells() のようなCP932前提の関数に決して渡してはならない
 * （渡すと0x9C..をトレイルバイトを要求するSJIS先頭バイトと誤解して
 * しまう）。実際のメッセージ文字列の幅計算はすべてtext_width()による
 * 画面セル単位で行うが、これらの罫線文字については直接の桁数カウントで
 * 行う。
 * ------------------------------------------------------------------- */
#define BOX_WIDTH     78   /* 枠の内側の幅。ボーダーとボーダーの間のセル数 */

#define BOXCH_TL      0x9c  /* 左上の角 */
#define BOXCH_TR      0x9d  /* 右上の角 */
#define BOXCH_BL      0x9e  /* 左下の角 */
#define BOXCH_BR      0x9f  /* 右下の角 */
#define BOXCH_H       0x95  /* 水平線 */
#define BOXCH_V       0x96  /* 垂直線 */
#define BOXCH_LT      0x93  /* 左T字（中間区切り線の左端） */
#define BOXCH_RT      0x92  /* 右T字（中間区切り線の右端） */
#define BOXCH_TJ      0x91  /* 上端の枠と内部の縦区切り線の交差点
                                （0行目用）。docs/filer-measure-03.md参照 */
#define BOXCH_HJ      0x90  /* 水平線と内部の縦区切り線の交差点
                                （2行目用）。docs/filer-measure-03.md参照 */

/* ---- ヘッダ1行目（ディスク合計）の縦区切り線 -----------------------
 * 1行目（合計/使用/空き）には0x96の区切り線が2本入る（docs/filer-measure-03.md
 * 参照）。0行目の枠線と2行目の区切り線は、それぞれの区切り線の真上／
 * 真下に交差記号（0x91／0x90）を置かなければならない。
 * 第7マイルストーンで本家自身のディスク合計行を実測したところ、
 * 数値はフィールド右端に1セル分の余白を残して右詰めになっており、
 * "bytes"/"バイト" という単位サフィックスは一切付いていなかった
 * （ラベル側で既に何を表しているか分かるため）――下の
 * sappend_field_rj() と draw_disk_line() を参照。
 *
 * 3つのフィールドはすべて同じ幅ではない：元の20/20/20分割（区切り線を
 * 21/42桁目に置く案）では、このプログラムはフィールドが3つしかない
 * （本家製品は"Page"を含め4つ）ため、3番目のフィールドが36セルもの
 * 幅になり、"free"の右側に大きな空白ができてしまう。そこで代わりに
 * 区切り線の桁位置自体を直接固定し（26／53桁目）、BOX_WIDTHの78セルを
 * ほぼ等分の3つ（25／26／25）に分割している。各フィールドの幅は
 * この区切り桁から導出されるので、区切り位置と幅がずれることは
 * ありえない。3つとも、想定される最悪のフィールドサイズ――ラベル
 * 6セル分＋format_u32()の最大出力（"4,294,967,295"、13セル）＋
 * 末尾の1セル分の余白＝20セル――は、どちらの言語でも余裕をもって
 * 収まる（check.pyのcell_width()で実測確認済み）。 */
#define DISK_SEP_COL1      26
#define DISK_SEP_COL2      53
#define DISK_FIELD1_WIDTH  (DISK_SEP_COL1 - 1)
#define DISK_FIELD2_WIDTH  (DISK_SEP_COL2 - DISK_SEP_COL1 - 1)
#define DISK_FIELD3_WIDTH  (BOX_WIDTH - DISK_SEP_COL2)

/* ---- ヘッダ0行目（タイトル）右側の時計フィールド -------------------------
 * "YY-MM-DD HH:MM:SS"、17セル。タイトル行の右端に表示される。
 * TITLE_DATETIME_GAPは、タイトルのダッシュ埋め部分と時計フィールドの
 * 間に置く空白セル。TITLE_DECOR_WIDTHは、タイトル文字列を直接囲む
 * 固定幅の "<<" + " " + " " + ">>" の幅（本家で実測。draw_title_row()
 * 参照。本家自身のタイトルもこの括弧の中で中央寄せされ、その両側を
 * ダッシュ埋めで行の固定幅まで埋める。ダッシュの連続部分と括弧の間には
 * もう1セル空白が入る）。check.pyは、これらの値とBOX_WIDTHから
 * g_titleのセル幅上限を導出しており、ハードコードしていない――
 * つまりこれらの値のどれかを変えると強制される上限も一緒に変わる。
 * g_title自体は固定の英語文字列であり、g_msgJA/g_msgENの一部ではない
 * ――理由は下の g_fkeyLabel[] 近くにあるコメント参照。 */
#define DATETIME_WIDTH      17
#define TITLE_DATETIME_GAP  1
#define TITLE_DECOR_WIDTH   6
/* 時計フィールドの後ろ、右上の角の前に置く末尾の " "＋BOXCH_H
   セル1つぶん（実測：実機では時計と角の間に、時計が角にぴったり
   くっつくのではなく、1セルの空白とさらに枠線セル1つが入っている）
   ――draw_title_row()参照。 */
#define TITLE_TAIL_WIDTH    2

/* ---- 言語／メッセージテーブル ----------------------------------------
 * 画面表示文字列はすべてここに集約し、インデックスで引く。呼び出し側で
 * リテラルのUI文字列を直接書くことはしない。設計の考え方は
 * docs/i18n-design.md 参照。
 * ------------------------------------------------------------------- */

#define LANG_JA 0
#define LANG_EN 1

/* 第11マイルストーンでタイトルは g_title（言語非依存の固定英語文字列、
 * g_fkeyLabel[] と同じ扱い）へ切り出したため、MSG_TITLE は無くなった。
 * 以降の MSG_* は0番から詰め直している。 */
#define MSG_PATH_PREFIX   0
#define MSG_TRUNC_PREFIX  1
#define MSG_TRUNC_SUFFIX  2
#define MSG_INFO_PREFIX   3
#define MSG_INFO_EMPTY    4
#define MSG_DISK_UNAVAIL  5
#define MSG_DISK_TOTAL    6
#define MSG_DISK_USED     7
#define MSG_DISK_FREE     8
#define MSG_BYTES_SUFFIX  9
#define MSG_ATTR_LABEL    10
#define MSG_MARKED_LABEL       11
#define MSG_DEL_CONFIRM_ONE_PRE   12
#define MSG_DEL_CONFIRM_ONE_SUF   13
#define MSG_DEL_CONFIRM_MARK_PRE  14
#define MSG_DEL_CONFIRM_MARK_SUF  15
#define MSG_DEL_ERR_ISDIR      16
#define MSG_DEL_ERR_FAILED     17
#define MSG_RENAME_PROMPT       18
#define MSG_RENAME_ERR_EMPTY    19
#define MSG_RENAME_ERR_FAILED   20
#define MSG_MKDIR_PROMPT        21
#define MSG_MKDIR_ERR_EMPTY     22
#define MSG_MKDIR_ERR_FAILED    23
#define MSG_CM_ERR_HASDIR       24
#define MSG_COPY_PROMPT         25
#define MSG_MOVE_PROMPT         26
#define MSG_CM_ERR_EMPTY        27
#define MSG_COPY_ERR_PRE        28
#define MSG_MOVE_ERR_PRE        29
#define MSG_OVERWRITE_PRE       30
#define MSG_OVERWRITE_SUF       31
#define MSG_COPY_DONE_PRE       32
#define MSG_COPY_DONE_SUF       33
#define MSG_MOVE_DONE_PRE       34
#define MSG_MOVE_DONE_SUF       35
#define MSG_EXEC_ERR_NOTEXE     36
#define MSG_EXEC_ERR_FAILED     37
#define MSG_EXEC_PRESS_KEY      38

/* 第8マイルストーン（組み込みビューア。COM/EXE以外のファイルでEnter）のメッセージ */
#define MSG_VIEW_FILENAME_LABEL 39
#define MSG_VIEW_LINENO_LABEL   40
#define MSG_VIEW_ERR_OPEN       41
#define MSG_VIEW_CMDLINE        42

/* 第12マイルストーン（ドライブ変更。F1 / Logdsk）のメッセージ。
   末尾に足すこと――既存の MSG_* の番号は動かさない。 */
#define MSG_LOGDSK_PROMPT       43
#define MSG_LOGDSK_ERR_EMPTY    44
#define MSG_LOGDSK_ERR_INVALID  45

const char *g_msgJA[] = {
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
  " 件",
  "COM/EXEのみ実行できます（何かキーを押してください）",
  "実行に失敗しました（何かキーを押してください）",
  "実行を終了しました（何かキーを押してください）",
  "ファイル名: ",
  "行番号: ",
  "ファイルを開けません（何かキーを押してください）",
  "ESC:一覧へ戻る",
  "ドライブ: ",
  "ドライブ名を入力してください",
  "そのドライブは使えません"
};

const char *g_msgEN[] = {
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
  " file(s)",
  "Only COM/EXE files can be executed (press any key)",
  "Execute failed (press any key)",
  "Finished executing (press any key)",
  "File name: ",
  "Line No: ",
  "Cannot open the file (press any key)",
  "ESC: back to list",
  "Drive: ",
  "Enter a drive letter",
  "That drive is not available"
};

const char **g_msgTables[2] = { g_msgJA, g_msgEN };

int g_lang = LANG_JA;

#define MSG(id) (g_msgTables[g_lang][id])

/* 日英両テーブルの要素数が一致していること、どちらにも空文字列が
   無いことを確認する。OKなら1、壊れていれば0を返す */
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

/* ---- header row 0 (title): fixed English text, language-independent ---
 * 利用者指定（第11マイルストーン）：画面上部のタイトルは日英切替の対象
 * から外し、常に英語表記に固定する。それ以外の文言（パス表示・情報・
 * 合計・各種ダイアログ/エラー等）は引き続き g_msgJA/g_msgEN の日英切替
 * のままで、変更しない。タイトルだけを言語表から切り離すため、置き場も
 * g_msgJA/g_msgEN ではなく、この下の g_fkeyLabel[]（最下行のファンクション
 * キー表示。あちらも「言語に依らない固定の C データ」として言語表の外に
 * 置かれている）と同じ扱いにした。draw_title_row() 自身の組み立て方
 * （"<< " + タイトル + " >>" をダッシュで埋めた枠の中央に置く処理）は
 * 変更していない。check.py のセル幅検査も、g_msgJA/g_msgEN の一部としてで
 * はなく、この文字列単独に対して行うよう追従させてある。 */
/* Directory viewerだと実態より狭い。すみれはCopy/Move/Delete/Rename/mKdir/eXec/Viewを持つファイラであって、閲覧専用ではない。
 * 原物FDは自分を「File & Directory tool」と説明している（FDはその頭文字）が、
 * そこには寄せない。原物の自己紹介の文言であり、権利方針で写さないと決めているため。
 * File managerは英語圏で標準的な呼び方で、海外利用者にも伝わる。
 */
char *g_title = "Sumire - File manager";

/* ---- 最下段：PC-98のファンクションキー割り当て（言語非依存） --
 * 第6マイルストーンで、第4マイルストーンの「キー:説明」コマンド行を、
 * 実機のFD 3.13(98)から直接実測した本家自身の最下段行の慣習
 * （docs/filer-measure-05.md参照）に置き換えた：ファンクションキー
 * F1～F10それぞれに対応する、幅6セルの固定フィールドが10個
 * （g_fkeyLabel[i]がF(i+1)のラベル）で、実機の属性面から実測した
 * 桁位置（g_fkeyCol[]）に左詰めで配置される。この行はコマンド名が
 * 単なる英語の略称であって翻訳対象の文言ではないという設計により、
 * 意図的にどちらの言語でも同じにしてある――そのためg_msgJA/g_msgEN
 * ではなく、ここに普通のCデータとして置いてある。
 * 旧来の移動／マーク／開くの説明行（"Arrow:move SP/Tab:mark ..."）は
 * 廃止した：実測した本家はこの行に操作説明を一切表示しておらず、
 * コマンドの位置だけを示している。それらの操作自体は従来どおり
 * すべて使用可能なままである（main()のキーディスパッチ参照）――
 * 変わったのは*画面上での説明のされ方*だけである。
 * g_fkeyLabel[i]が空文字列（""）であることは、そのファンクションキーが
 * このプログラムではまだ実装されていない（例：本家のSort/Find/Tree/…
 * の枠）ことを意味する――その位置は（フィールド間の隙間と
 * 同様に）空白のまま予約され、使い回されない。これにより将来の
 * マイルストーンで他を動かすことなくコマンドを追加でき、かつ
 * 実際には持っていないコマンドを持っているかのように見せることもない。
 * g_fkeyHiPos[i]は、main()での実際のディスパッチキーとなる文字の
 * g_fkeyLabel[i]内でのインデックスである――例えばmKdirは'K'がインデックス1
 * で、そこの 'k'/'K' のcaseと対応している。-1はハイライト無効
 * （空ラベルとの組み合わせでのみ使用）。ここにある空でないラベルの
 * キーはすべて、第4/5マイルストーンから変わらず、普通の文字キーでも
 * 引き続き到達可能である――ファンクションキーは置き換えではなく
 * 追加の入口である。 */
#define FKEY_COUNT       10

#define IDLE_POLL_SPIN  3000  /* main()のアイドルループでdos_kbhit()をポーリングする間の
                                 ビジーウェイト――そのコメントを参照 */
#define FKEY_FIELD_WIDTH 6

int g_fkeyCol[] = { 4, 11, 18, 25, 32, 42, 49, 56, 63, 70 };

char *g_fkeyLabel[] = {
  "Logdsk", "eXec", "Copy", "Delete", "Rename", "Move", "mKdir", "", "", "Quit"
};
int g_fkeyHiPos[] = { 0, 1, 0, 0, 0, 0, 1, -1, -1, 0 };

/* ---- 組み込みビューア（第8マイルストーン、ROLL UP/ROLL DOWNは第11マイルストーンで追加）
 * ディレクトリでないエントリで拡張子がCOM/EXEでないものにEnterを
 * 押すと、何もしない代わりに読み取り専用のフルスクリーンテキスト
 * ビューアを開く――実際のプログラム起動はF2/X（do_exec()）のままで
 * 変更なし。以下の画面レイアウトとキー動作は実機で実測したもの。
 * docs/filer-measure-07.md参照。ROLL UPはVIEW_CONTENT_ROWS（22）行
 * 進む（実測：内容表示領域がちょうど22行なので前ページとの重なりは
 * 無い）。ROLL DOWNは同じ量だけ戻り、先頭行で止まる――下のdo_view()
 * 参照。ファイラ自身の一覧表示ではこのマイルストーンでこれらのキーは
 * 使えない：一覧側のページング自体が未実装であり、本家でのそちら側の
 * 挙動も実測していない（README.mdのTODO参照）。
 * ファイルを丸ごとメモリに読み込むことは決してしない
 * （スモールモデルの64KBデータセグメントには既にディレクトリ一覧と
 * VRAMシャドウバッファが入っている）。小さなバッファを介して1回に
 * 表示行1行ぶんずつ前方向に読み進め、目的の行が「今画面に出ている
 * 行の単なる次の行」ではない場合はファイル先頭から前方に読み捨てて
 * 表示を作り直す――下のview_goto_line()/view_render()参照。
 * メモリ上にランダムアクセス用の行インデックスは一切保持しないため、
 * このプログラムはファイルの行数に比例したメモリを確保することが
 * 決してない。 */
#define VROW_HEADER        0    /* "File name:"/"Line No:" の行 */
#define VROW_BLANK         1    /* 常に空の行（実測したレイアウトどおり） */
#define VROW_CONTENT_TOP   2    /* 22行あるファイル内容行の最初の行 */
#define VIEW_CONTENT_ROWS  22   /* 実測：2～23行目 */
#define VROW_CMD           24   /* ファイラ自身のファンクションキー行と物理的に同じ行――
                                   使い回しであり2つ目のコピーではない
                                   （draw_view_cmdline()参照） */
#define VIEW_TAB_WIDTH     8    /* 実測：タブは表示上の桁位置で次の8の倍数まで
                                   展開される */
#define VIEW_READ_BUF      512  /* ビューア自身のバッファ付きリーダー用ディスク読み込み
                                   チャンクサイズ――vreader_getc()参照。
                                   ファイルを1バイトずつ読むことは決して
                                   しない */
#define VIEW_LINE_BUF      88   /* 1つの表示行は最大でVRAM_COLS（80）セル、すなわち最大80バイト
                                   （どのセルもANKでも全角の半分でも
                                   ちょうど1バイト消費する）に加えて、
                                   2バイトの行末マークとNUL1つ分の
                                   余裕を持たせてある */
#define VIEW_PATH_BUF      96   /* g_path（ドライブ／バックスラッシュを含め79文字以下）
                                   ＋8.3形式の名前。このファイルの他の
                                   フルパス用バッファ（do_delete()の
                                   path[96]参照）と同じサイズの考え方 */

/* 行末マーク：全角の下向き矢印（CP932の0x81 0xAB）。表示行が
   実際に本物の行終端で終わったときにのみ描画する（折り返しでは
   決して描かず、末尾に改行の無い最終行でも決して描かない――
   view_read_line()のhadNL出力参照）。この字形自体はこのプロジェクト
   独自の選択であって本家製品を再現したものではない――実測で
   分かっている事実は、*何らかの*全角の行末マークが表示される
   ということだけであり（docs/filer-measure-07.md参照）、その正確な
   見た目までは実測していない。 */
#define VIEW_EOL_MARK_B1  0x81
#define VIEW_EOL_MARK_B2  0xab

/* ---- グローバル状態 ------------------------------------------------------ */

char g_name[MAX_ENTRIES * NAME_LEN];
unsigned char g_attr[MAX_ENTRIES];
unsigned int g_time[MAX_ENTRIES];
unsigned int g_date[MAX_ENTRIES];
unsigned int g_sizeLo[MAX_ENTRIES];
unsigned int g_sizeHi[MAX_ENTRIES];
unsigned char g_marked[MAX_ENTRIES]; /* 1＝マーク済み。ディレクトリは決してマークされない
                                         （mark_cursor()参照） */

int g_count;        /* 実際に格納されているエントリ数（MAX_ENTRIES以下） */
int g_truncated;     /* ディレクトリ内のファイル数がMAX_ENTRIESを超えていたら1 */
int g_cursor;         /* 表示中の一覧内でのインデックス（0～visibleCount-1） */

char g_path[80];      /* カレントディレクトリ。必ず'\\'で終わる */
char g_search[88];    /* g_path + "*.*" */
unsigned char g_dta[43];

char g_copybuf[COPY_BUF_SIZE]; /* Copy/Move が共用する単一のI/Oバッファ――
                                    COPY_BUF_SIZEのコメント参照。16ビットの
                                    スモールモデルプログラムでこのサイズの
                                    バッファをスタックに置いてはならない。 */

/* ---- 組み込みビューアの状態（第8マイルストーン） ------------------------
 * ビューアのセッションは同時に1つしか存在しない（do_view()が
 * do_rename()/do_copy()/…と同じように自前のモーダルループを回すため）
 * ので、呼び出しのたびに引き回す構造体ではなく、このファイルの
 * 上の方にあるDOS呼び出しラッパー／バッファ群と同じ形の、ただの
 * グローバル状態にしてある。 */
char g_viewPath[VIEW_PATH_BUF];   /* 表示中のファイルのフルパス */
int g_viewHandle;                 /* DOSハンドル。開いていなければ-1 */
char g_viewReadBuf[VIEW_READ_BUF];/* vreader_getc()用のディスク読み込みバッファ */
int g_viewReadLen;                 /* g_viewReadBuf内で有効なバイト数 */
int g_viewReadPos;                 /* g_viewReadBuf内の次の未読インデックス */
int g_viewPushback[2];             /* 1～2バイトのプッシュバックキュー（先頭が先）――
                                       下のvreader_pushback1()/2()参照。
                                       折り返し判定で1バイト先まで読んで
                                       しまった場合（タブや全角文字の
                                       トレイルバイト）に、そのバイトを
                                       捨てずに次の表示行へ戻すために
                                       必要 */
int g_viewPushbackLen;             /* 0, 1, 2のいずれか */
int g_viewLineNo;                   /* 現在いちばん上に表示されている行の
                                       1始まりの行番号 */
int g_viewTotalLines;               /* 表示行の総数。ビューアを開いたときに
                                       view_count_lines()が一度だけ計算する */

/* ---- 低レベルなDOS/BIOS呼び出し（インラインアセンブラ。doc/smlrc.mdの"asm()"参照） -- */

/* INT 18h AH=00h（BIOSキーボード入力、キー入力を待つ）：
   AH=スキャンコード、AL=文字コードを返す（実測。docs/key-measure-01.md
   とdocs/key-measure-02.md参照）。*scanにスキャンコードを受け取り、
   関数自体の戻り値はALをゼロ拡張したもの――このポインタ出力パラメータ
   の形式は、このファイルの他の複数値を返すDOS呼び出し（上の
   dos_getftime()など）と揃えてある。この関数を呼ぶのは下の
   dos_getch()だけ。 */
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

/* 第9マイルストーンでDOS INT 21h AH=08hからBIOS INT 18h AH=00hへ
   移行した（理由はdocs/dos-key-measure-01.md参照――DOSのコンソール
   ドライバは^SをXOFFとして飲み込み、ROLL UP/ROLL DOWN/HELPは
   一切届かなかった）。旧来と同じ名前・0引数の「文字を返す」という
   形はそのまま保っているので、このファイル内の約30箇所の既存
   呼び出し元は一切変更不要。文字を持つキー（AL != 0）はそのまま
   返す――このプログラムが比較している素の値すべてに既に一致している
   理由は上のKEY_*のコメント参照。文字を持たないキー（AL == 0）は
   そのスキャンコードから、上のKEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT/
   KEY_HOME/KEY_F1..KEY_F10/KEY_ROLLUP/KEY_ROLLDOWNの疑似コードの
   いずれかに変換される。このプログラムが割り当てていないキー
   （例：HELP/INS/DEL――スキャンコード自体はdocs/bios-key-measure-01.md
   で分かっているが、まだ用途を割り当てていない。READMEのTODO参照）
   なら0になる。 */
int dos_getch(void)
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
  return 0;
}

/* INT 18h AH=01h（BIOSキーボード状態確認）：保留中のキーを消費
   しない。信頼できる「キーがあるか」のフラグはBHだけ（1＝キーが
   待っている、0＝無い）――AXやフラグレジスタはこの用途には信頼
   できない（実測。docs/key-measure-02.mdの「AH=01h」の節参照）。
   DOS INT 21h AH=0Bhの置き換え。0／非0の契約は従来と同じなので
   呼び出し元は変更不要。 */
int dos_kbhit(void)
{
  asm("mov ah, 1\n"
      "int 0x18\n"
      "mov al, bh\n"
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

/* ファイルを削除する（INT 21h AH=41h、DS:DX=ASCIZパス）。成功時0、
   失敗時（読み取り専用・使用中など）は非0を返す。呼ばれるのは常に
   ディレクトリでないエントリから組み立てたパスに対してのみ
   （do_delete()参照）。DOS自体もこの方法でディレクトリをunlinkする
   ことは拒否するはずだが、それに頼ってはいない――ディレクトリの
   ケースはここが呼ばれる前段階で既にフィルタして除外してある。 */
int dos_delete(char *path)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x41\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* ディレクトリを作成する（INT 21h AH=39h、DS:DX=ASCIZパス）。
   成功時0、失敗時（すでに同名が存在する等）は非0を返す。 */
int dos_mkdir(char *path)
{
  asm("mov dx, [bp+4]\n"
      "mov ah, 0x39\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* ファイルまたはディレクトリの名前変更／移動（INT 21h AH=56h、
   DS:DX=旧ASCIZパス、ES:DI=新ASCIZパス）。一般にESが最初からDSと
   同じ値である保証はないため、決め打ちにせずここで退避・設定・復元
   している。成功時0、失敗時（移動先に既に同名が存在する等）は
   非0を返す。 */
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

/* DSの現在値を返す（このスモールモデルプログラムのグローバル／
   static変数がすべて置かれているデータセグメント――COPY_BUF_SIZEの
   コメント参照）。これが必要なのは下のdos_exec()だけ：組み立てる
   EXECパラメータブロックには、この同じセグメント内にあるバッファを
   指す offset:segment 形式のfarポインタが明示的に含まれるが、この
   方言のCにはセグメント値を直接書く方法が無いため、このような
   レジスタ読み出しから得るしかない。 */
unsigned int get_ds(void)
{
  asm("mov ax, ds");
}

/* 子プログラムを実行し、終了を待つ（INT 21h AH=4Bh AL=0、EXEC
   「読み込んで実行」）。pathはASCIIZのプログラムパス。paramBlockは
   下のdo_exec()が組み立てる14バイトのEXECパラメータブロック
   （word型の環境セグメント、続けてoffset:segment形式のfarポインタ
   3つ――コマンドテール、1つ目の既定FCB、2つ目の既定FCB）。成功時0、
   失敗時（ファイルが見つからない、メモリ不足など）は非0を返す。
   上のdos_rename()と同様、ESはこの呼び出しの間だけDS（paramBlock
   自身が置かれているセグメント。常にdo_exec()のローカルバッファの
   一つなので）に切り替え、戻る前に元に戻す――呼び出し元が戻った後
   のESの値を気にしないとは限らないため。DS:DXはpath用として既に
   正しいデータセグメントの値のまま何も触らない（このファイルの他の
   dos_*のパス引数と全く同様）――AH=4Bhでは、この関数が触る
   バッファがすべて同じ一つのスモールモデルのデータセグメントに
   あるため、ESをDSと異ならせる必要は一度も無い。 */
int dos_exec(char *path, unsigned char *paramBlock)
{
  asm("mov dx, [bp+4]\n"
      "mov bx, [bp+6]\n"
      "push es\n"
      "mov ax, ds\n"
      "mov es, ax\n"
      "mov al, 0\n"
      "mov ah, 0x4b\n"
      "int 0x21\n"
      "pop es\n"
      "sbb ax, ax");
}

/* ---- ファイルI/O（Copy/Move用。INT 21h AH=3Dh/3Ch/3Fh/40h/3Eh/57h） ----------
 * これらは上で使っている"sbb ax,ax"のトリックではなく
 * "jnc LABEL / mov ax,-1 / LABEL:" の形を使っている。成功時の値が
 * 単純な0ではなく、ハンドル（dos_open/dos_create）やバイト数
 * （dos_read/dos_wfile）であり、CFがクリアのときはAXの値をそのまま
 * 返す必要があるため。以下の各ラベル名はこのファイル全体で重複が
 * 無いようにしてある：このインラインアセンブラは1つの共有アセンブリ
 * 出力にそのまま出力される（smlrc.mdの「そのまま出力」参照）ため、
 * 2つの関数が同じラベル名を使うとアセンブル時に衝突してしまう。
 * ------------------------------------------------------------------ */

/* 既存ファイルを開く（モード0＝読み取り専用。このプログラムの
   唯一の用途であるCopy/Moveの読み込み元に対応）。成功時はハンドル
   （0以上）、失敗時は-1を返す。 */
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

/* ファイルを作成する（既に存在すれば切り詰める）。書き込み用、
   通常属性で。成功時はハンドル（0以上）、失敗時は-1を返す。 */
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

/* dos_open()/dos_create()で得たハンドルを閉じる。成功時0、
   失敗時は非0を返す。 */
int dos_close(unsigned int handle)
{
  asm("mov bx, [bp+4]\n"
      "mov ah, 0x3e\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* bufへ最大len バイトを読み込む。実際に読み込んだバイト数
   （0はエラーではなくファイル終端を意味する）、失敗時は-1を返す。 */
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

/* 開いているハンドルへbufからlenバイトを書き込む（画面出力用に
   ハンドル1固定になっている上のdos_write()とは異なる）。実際に
   書き込んだバイト数、失敗時は-1を返す。呼び出し元はこれを要求した
   lenと比較して書き込み不足（ディスクフルなど）を検出する。 */
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

/* AH=57h AL=0：開いているハンドルのファイル日付／時刻を
   *timeOut/*dateOutへ読み込む（パックされたDOS形式。このファイルの
   他所でformat_date()/format_time()が既に復号しているのと同じ
   エンコーディング）。成功時0、失敗時は非0を返す。 */
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

/* AH=57h AL=1：開いているハンドルのファイル日付／時刻を設定する
   （パックされたDOS形式）。成功時0、失敗時は非0を返す。 */
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

/* INT 21h AH=0Eh（デフォルトドライブの設定）：DL=0がA:。ALには
   システムの論理ドライブ数が返るが、**存在しないドライブを指定
   しても失敗を報告しない**ので、有効性の確認は呼び出し側が
   先にAH=36h（dos_diskfree()）で行う――do_logdsk()参照。 */
void dos_setdrive(unsigned int drive)
{
  asm("mov dl, [bp+4]\n"
      "mov ah, 0x0e\n"
      "int 0x21");
}

int dos_getcwd(unsigned int drive, char *buf)
{
  asm("mov dl, [bp+4]\n"
      "mov si, [bp+6]\n"
      "mov ah, 0x47\n"
      "int 0x21\n"
      "sbb ax, ax");
}

/* AX＝クラスタあたりのセクタ数（エラー時0xFFFF）。*availClus、
   *bytesPerSec、*totalClusはそれぞれBX/CX/DXから埋める。 */
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

/* INT 21h AH=2Ah（日付取得）：CX=年、DH=月、DL=日
   （AL=曜日、ここでは未使用）。*year/*month/*dayをこれらから
   埋める――下のformat_datetime()参照。この関数の呼び出し元はそこ
   だけ。 */
void dos_getdate(unsigned int *year, unsigned int *month, unsigned int *day)
{
  asm("mov ah, 0x2a\n"
      "int 0x21\n"
      "mov si, [bp+4]\n"
      "mov [si], cx\n"
      "mov si, [bp+6]\n"
      "mov al, dh\n"
      "mov ah, 0\n"
      "mov [si], ax\n"
      "mov si, [bp+8]\n"
      "mov al, dl\n"
      "mov ah, 0\n"
      "mov [si], ax");
}

/* INT 21h AH=2Ch（時刻取得）：CH=時、CL=分、DH=秒
   （DL=1/100秒、ここでは未使用）。*hour/*minute/*secondをこれらから
   埋める――下のformat_datetime()参照。この関数の呼び出し元はそこ
   だけ。 */
void dos_gettime(unsigned int *hour, unsigned int *minute, unsigned int *second)
{
  asm("mov ah, 0x2c\n"
      "int 0x21\n"
      "mov si, [bp+4]\n"
      "mov al, ch\n"
      "mov ah, 0\n"
      "mov [si], ax\n"
      "mov si, [bp+6]\n"
      "mov al, cl\n"
      "mov ah, 0\n"
      "mov [si], ax\n"
      "mov si, [bp+8]\n"
      "mov al, dh\n"
      "mov ah, 0\n"
      "mov [si], ax");
}

/* buf から始まる len バイトを INT 21h AH=40h で標準出力
   （ハンドル1）へ書き込む。stdioの代わりにこれを使うのは、画面への
   書き込みを、改行を認識しない行バッファ付きストリーム経由ではなく、
   1回ごとに完結する明示的なDOS呼び出しにするため。 */
void dos_write(char *buf, unsigned int len)
{
  asm("mov dx, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mov bx, 1\n"
      "mov ah, 0x40\n"
      "int 0x21");
}

/* ---- 32ビット演算ヘルパー（SmallerCの16ビットモードにはlong型が無い） ---- */

/* a*b（下位ワード）を返す。*hiOutに上位ワードが入る */
unsigned int umul32(unsigned int a, unsigned int b, unsigned int *hiOut)
{
  asm("mov ax, [bp+4]\n"
      "mov cx, [bp+6]\n"
      "mul cx\n"
      "mov bx, [bp+8]\n"
      "mov [bx], dx");
}

/* (*hiP:*loP) /= 10 を行い、余り（0～9）を返す。*hiP/*loPはその場で更新する */
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

/* (hi:lo) * val16 を32ビットに切り詰めた結果。下位ワードを返し、*outHiが上位ワード */
unsigned int mul32x16(unsigned int hi, unsigned int lo, unsigned int val16,
                       unsigned int *outHi)
{
  unsigned int t1hi;
  unsigned int t1lo;
  unsigned int t2;

  t1lo = umul32(lo, val16, &t1hi);
  t2 = hi * val16; /* 16×16ビットの符号無し乗算。上位ビットは意図的に捨てる */
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

/* 32ビット符号無し値を3桁ごとに','を入れた10進表記で整形する */
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
    /* digits[]は下位桁から順に格納されている（digits[0]が1の位）ので、
       インデックスiはその桁が1の位から何桁離れているかを表す。カンマは、
       1の位から数えて3桁ぶんのグループが完結するたび――すなわちiが
       3の正の倍数のとき――その桁の直後に入る。 */
    if (i > 0 && (i % 3) == 0) {
      out[j] = ',';
      j++;
    }
  }
  out[j] = 0;
}

/* format_u32()と同様だが','区切りを入れない版。ファイル一覧の
   サイズ欄（区切りなし）用――ヘッダの合計欄だけがカンマ区切りに
   なっている。 */
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

/* ---- 日付／時刻の整形 -------------------------------------------- */

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

/* 現在時刻を"YY-MM-DD HH:MM:SS"（DATETIME_WIDTH==17セル、すべて
   ASCII、NULを含めるとout[]は少なくとも18バイト必要）として返す。
   上のformat_date()/format_time()（DOSディレクトリエントリの
   パックされた日付／時刻を復号する）とは異なり、これは呼ばれる
   たびにINT 21h AH=2Ah/2Chで現在時計を読み直すので、毎フレーム
   呼び直すことで画面上の時計が進む。 */
void format_datetime(char *out)
{
  unsigned int year, month, day;
  unsigned int hour, minute, second;
  unsigned int yy;

  dos_getdate(&year, &month, &day);
  dos_gettime(&hour, &minute, &second);
  yy = year % 100;

  out[0] = (char)('0' + yy / 10);
  out[1] = (char)('0' + yy % 10);
  out[2] = '-';
  out[3] = (char)('0' + month / 10);
  out[4] = (char)('0' + month % 10);
  out[5] = '-';
  out[6] = (char)('0' + day / 10);
  out[7] = (char)('0' + day % 10);
  out[8] = ' ';
  out[9] = (char)('0' + hour / 10);
  out[10] = (char)('0' + hour % 10);
  out[11] = ':';
  out[12] = (char)('0' + minute / 10);
  out[13] = (char)('0' + minute % 10);
  out[14] = ':';
  out[15] = (char)('0' + second / 10);
  out[16] = (char)('0' + second % 10);
  out[17] = 0;
}

/* 属性文字列 "R H S A"、ビットが立っていなければ'_' */
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

/* ---- 小さなテキストヘルパー ------------------------------------------------ */

void put_str_n(char *buf, int col, char *s, int maxlen)
{
  int i;

  i = 0;
  while (s[i] != 0 && i < maxlen) {
    buf[col + i] = s[i];
    i++;
  }
}

/* 画面セル単位での表示幅：SJISの先頭バイトは2、それ以外
   （ASCII／トレイルバイトは先頭バイトとまとめてスキップ）は1として
   数える。桁位置の揃えはすべてこれを通す必要があり、strlen()を
   使ってはならない――そうすることで日本語の全角文字もASCII文字と
   同じように桁が揃う。 */
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
        w += 1; /* 文字列末尾で先頭バイトだけで切れている場合、1として数える */
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

/* NUL終端された文字列sをdstのバイトオフセット*lenpに追記し、
   改めて終端する。*lenpは追記したバイト数ぶん進める。行の内容
   （ラベル＋数値＋ラベル…）を、ヘッダボックスの行に置く前に
   プレーンなバッファへ組み立てるために使う。
   'cap'は呼び出し元が宣言したdstバッファの総サイズ（NUL用の
   余地を含めたバイト数）。sappend()は、終端のNUL以外ではdst[cap-1]
   以降には決して書き込まず、2バイトのSJIS／罫線文字を半分だけ
   書くことも決してない――そのような文字の両バイトぶんの余地が
   残っていなければ、その文字（とs内でそれ以降のすべて）は黙って
   捨てられ、dstは常に有効なNUL終端文字列のままになる。 */
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
      chBytes = (s[i + 1] != 0) ? 2 : 1; /* 先頭バイトだけで切れている場合：1として扱う */
    } else {
      chBytes = 1;
    }
    if (lp + chBytes > cap - 1) break; /* NUL用の余地を残す */
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

/* sappend()と同様だが、符号無しintを単純な10進数として整形した
   ものを追記する。'cap'の意味はsappend()と同じ。桁はASCII（1バイト
   ずつ）なのでここでマルチバイト文字が分断されることは無いが、
   各桁を書く前に残り容量を毎回確認している。 */
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

/* sappend()と同様だが、'width'画面セル分になるまで追記文字列を
   単純なASCIIスペースでパディングする（1スペースごとにsappend()を
   1回呼ぶので、'cap'は同様に守られる）――draw_disk_line()が
   合計／使用／空きの各フィールドを画面上で固定幅にし、それらの間の
   縦区切り線が毎フレーム同じ桁に来るようにするために使う。sが
   既に'width'セル以上あれば何もパディングしない
   （draw_disk_line()は各フィールドを想定される最悪ケースに
   合わせてサイズしているので実際には起こらないはずだが、エラー
   扱いにはせず単にパディングしないだけにしてある）。 */
void sappend_padded(char *dst, int *lenp, char *s, int width, int cap)
{
  int w;
  int i;

  w = text_width(s);
  sappend(dst, lenp, s, cap);
  for (i = w; i < width; i++) sappend(dst, lenp, " ", cap);
}

/* 上のsappend_padded()と似ているが、左詰めではなくラベル＋数値を
   'width'画面セル内で右詰めにする：パディングのスペースは数値の後
   ではなくラベルと数値の間に入り、数値の最後の桁は常にフィールド
   右端からちょうど1セル手前（固定1セルの末尾余白）に来る。これは
   本家自身のディスク合計行（実測。draw_disk_line()と上の
   DISK_SEP_COL1/DISK_SEP_COL2のコメント参照）――「各欄で数値は
   右詰め、右端に1桁ぶんの余白」――と一致させたもの。ラベル＋数値＋
   1セルの余白が既に'width'セル以上あれば何もパディングしない
   （draw_disk_line()は各フィールドを想定される最悪ケースに合わせて
   サイズしているので実際には起こらないはずだが、sappend_padded()
   同様エラー扱いにはしない）。 */
void sappend_field_rj(char *dst, int *lenp, char *label, char *number, int width, int cap)
{
  int labelCells;
  int numberCells;
  int pad;
  int i;

  labelCells = text_width(label);
  numberCells = text_width(number);
  pad = width - labelCells - numberCells - 1;
  if (pad < 0) pad = 0;
  sappend(dst, lenp, label, cap);
  for (i = 0; i < pad; i++) sappend(dst, lenp, " ", cap);
  sappend(dst, lenp, number, cap);
  sappend(dst, lenp, " ", cap);
}

/* ---- テキストVRAM（画面への直接書き込み。第5マイルストーン） --------------------
 * 画面出力は内容についてはもうDOSコンソール（AH=40h＋ANSI.SYSの
 * エスケープ）を一切経由せず、PC-98のテキストVRAM2面――文字面
 * 0xA0000と属性面0xA2000、どちらも80×25セル×2バイト/セルで
 * 同じレイアウト（実測。docs/tvram-measure-01.md参照）――へ直接
 * 書き込む。これにより、実機のコンソール経路に対して実測した
 * 次の3点を解決した：(1) このプログラムがヘッダ枠に使っていた
 * 全角罫線グリフはここで使っているフォントにグリフが無く、何も
 * 描画されていなかった。(2) 再描画のたびにまずESC[2Jを送っており、
 * 画面全体をクリアしてから再描画するためちらついていた。(3) 本家
 * 自身の半角罫線コード（0x9C-0x9Fなど。上のBOXCH_*参照）は、
 * DOSコンソールでは0x81-0x9FがShift_JISの先頭バイトとして解釈
 * されるため送れなかった――VRAMへの直接書き込みにはそのような
 * 解釈が無いため、本家と全く同じように使うことができる。
 *
 * ANK（半角）セル：上位バイト0x00、下位バイト＝文字コードそのまま
 * （上の半角罫線コードもこの形式で格納されている――これらはSJISで
 * はなくANKコードである）。全角セル：ここでのソーステキストは
 * CP932（Shift_JIS）だが、VRAM側はJIS X 0208を要求するため、
 * まずsjis_to_jis()でSJISバイトのペアを変換し、セルの下位バイト＝
 * （JIS第1バイト － 0x20）、上位バイト＝JIS第2バイト（実測。
 * docs/tvram-measure-01.md参照）とする。全角文字は画面セルを
 * 2つ占有する。右側のセルの内容は表示に影響しない（これも実測
 * 済み）が、それでも古い値のまま放置せず明確な値（空白のANK
 * スペース）を入れておく――そうしないと、後の再描画で左側の
 * セルの内容だけが変わった場合に、右側のセルに古いデータが
 * 残ったままになり、後で何かがそれを読んでしまう可能性がある。
 *
 * 再描画のたびに画面全体をクリアすることはしない。代わりに
 * g_curChar[]/g_curAttr[]が、2000セルそれぞれについて現在実際に
 * VRAMに入っている内容を反映しており、1セルを書き込むすべての
 * 呼び出しはまずこのミラーと新しい値を比較し、実際に変化した
 * ときだけハードウェアに触れて（そしてミラーを更新して）いる。
 * draw_screen_frame()（およびその下位にあるdraw_dialog()、
 * draw_input_box()）は呼ばれるたびに常に*フレーム全体*の内容を
 * これらの関数を通してセル単位で再生成するので、この比較こそが
 * 再描画でちらつきや空白化を防いでいる唯一の仕組みである――
 * 変化していないセルは単に二度と書き込まれず、変化したセルは
 * 一切空白を経由せずその場で更新される。
 * ------------------------------------------------------------------- */

#define VRAM_ROWS  25
#define VRAM_COLS  80
#define VRAM_CELLS (VRAM_ROWS * VRAM_COLS)

/* 属性バイト（docs/tvram-measure-01.md参照）：bit7-5＝色(GRB)、
   b3＝下線（実測した描画バグのため未使用――グリフが4ドット
   右にずれる）、b2＝反転表示、b0＝そのセルを表示するかどうか
   （立っていないとセルが空白になるので常に立てておく必要がある）。
   ATTR_BASEは普通の白。ATTR_REVは同じ色でセルだけ反転したもので、
   カーソル行やコマンド文字のハイライトに使う――README参照。 */
#define ATTR_BASE  0xE1
#define ATTR_REV   0xE5

/* ---- 実機で実測した本家の色（docs/filer-measure-06.md） --------------
 * ATTR_BORDER：罫線の枠／区切り線とヘッダのラベル（シアン）。
 * ATTR_TITLE：タイトル文字列と0行目の時計フィールド（黄色）。
 * ATTR_VALUE：ヘッダの値（パス／ファイル名／数値）――ATTR_BASEと
 *             バイト値は同じだが、呼び出し側で「これは値である」
 *             ことが読み取れるよう、あえて別名にしてある
 *             （「これは歴史的な既定値である」ではなく）。
 * ATTR_LIST_DIR/ATTR_LIST_FILE/ATTR_LIST_SYS：エントリの種類
 * （ディレクトリ／通常ファイル／システムまたは隠しファイル）ごとの
 * ファイル一覧行の色。カーソル行はその種別色をそのまま保ち、反転
 * ビット（ATTR_CURSOR_BIT）を足すだけ――下のentry_attr()参照。
 * 同じファイルについて実測したカーソル行／非カーソル行の属性
 * バイトはこのビットだけが異なり、色は決して変わらない。これは
 * これが置き換えた「カーソル＝黄色／それ以外＝白」という旧方式
 * とは異なる。 ------------------------------------------------------- */
#define ATTR_BORDER      0xA1
#define ATTR_LABEL       0xA1
#define ATTR_TITLE       0xC1
#define ATTR_VALUE       0xE1
#define ATTR_LIST_DIR    0xA1
#define ATTR_LIST_FILE   0xC1
#define ATTR_LIST_SYS    0x61
#define ATTR_CURSOR_BIT  0x04

/* 最下段のファンクションキー行の属性（24行目のみ）――実機の属性面
 * から実測したもので、上のATTR_REVとは異なる慣習になっている：
 * ラベル全体が反転され、かつ非白の色を使い、実際のディスパッチ
 * キーである1文字だけが、その同じ反転ビットの上にさらに独自の
 * （黄色の）色を持つ――旧コマンド行のような「キー文字だけ反転、
 * 残りは平文」という単純な形ではない。docs/filer-measure-05.md
 * 参照。 */
#define ATTR_FKEY_LABEL 0xA5  /* 反転、色101――ラベル文字全体 */
#define ATTR_FKEY_KEY   0xC5  /* 反転、色110（黄色）――キー文字のみ */

unsigned int g_curChar[VRAM_CELLS];  /* 現在実際にVRAMへ入っている内容を反映するミラー */
unsigned char g_curAttr[VRAM_CELLS];

/* 実際にハードウェアへ触れる唯一の関数。offsetはいずれかの面への
   バイトオフセット（0, 2, 4, ... 3998、すなわちcellIndex*2）。
   chWordは16ビットのワードとして格納する（offsetに下位バイト、
   offset+1に上位バイト。すなわちx86のリトルエンディアンそのままの
   格納）。attrの下位バイトは属性面の同じオフセットへ1バイトとして
   格納する。セグメントは汎用レジスタ（cx）経由でロードしている
   （x86ではセグメントレジスタへ即値を直接movできないため）。esは、
   呼び出し元がその後のesの値を気にしないとは限らないため、上の
   dos_rename()がint 21hの前後でesを退避・復元しているのと同じ
   考え方でこの周りでも退避・復元している。 */
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

/* CP932（Shift_JIS）→JIS X 0208、1文字ぶん。これを実際に何かの
   描画に使う前に、実機の画面から実測した漢字／かな16文字の
   表から検証済み（第5マイルストーンのコミットメッセージ／
   ドキュメント参照）――既存の変換表を読んで作ったものではない。
   s1/s2は正当なSJISの先頭／トレイルバイトのペアでなければならない
   （0x81-0x9Fまたは0xE0-0xFCが先頭、0x40-0xFC（0x7Fを除く）が
   トレイル――このファイルの他所でtext_width()/sappend()が既に
   前提としているのと同じ範囲）。 */
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

/* ミラーを、このプログラムが実際に書き込むどんなセルの内容にも
   決して一致しない状態にクリアする（chWord 0は、書き込まれることの
   無い「ANKのNUL」に相当する値としてしか発生しない――
   vram_ank()/vram_zenkaku()参照）。これにより最初のフレームは、
   古びたBSSのゼロ初期化を信用するのではなく、必ず触れるすべての
   セルを実際に書き込むことになる。起動時に一度だけ呼ばれる。 */
void vram_shadow_init(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    g_curChar[i] = 0;
    g_curAttr[i] = 0;
  }
}

/* ミラーが示している現在の内容と異なる場合に限り1セルを書く。
   row/colはセル座標（0-24／0-79）。範囲外は黙って無視する
   （防御的なもの――下の各呼び出し元はすでにすべて範囲内に
   収まっているが、何らかのメッセージが想定より長くなって計算された
   固定幅フィールドがVRAM_CELLSを超えてインデックスすることが
   絶対に無いようにするため）。 */
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

/* ANK（半角）セル1つぶん：上位バイト0x00、下位バイト＝コードその
   まま。実際のANKテキストにも、半角罫線コード（上のBOXCH_*。単なる
   バイト定数でありCの文字列では決してない）にも使う。 */
void vram_ank(int row, int col, unsigned char code, unsigned int attr)
{
  vram_set_cell(row, col, (unsigned int)code, attr);
}

/* (row,col)と(row,col+1)の2セルにまたがる全角文字1文字ぶん。
   s1/s2は生のCP932バイトペア。セルのエンコーディングと、なぜ
   右側のセルにも書き込むのかについてはファイル冒頭のコメント
   参照。 */
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

/* (row,col)にNUL終端のCP932文字列を最大'width'画面セルぶん配置し
   （このファイルの他所のtext_width()/sappend()と同じSJIS先頭バイト
   ルール）、'width'の残り部分は空白でパディングする――旧ANSI
   ライターのためにバイトバッファを組み立てていたput_str_cells()の
   置き換え。今やすべてのセルがこの関数を通して直接書かれるように
   なった。残りをパディングするのは、毎フレームのESC[2Jが無く
   なった今では必須である：これをしないと、短い文字列が以前の
   長い内容を末尾のセルに残したままにしてしまう。 */
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
        vram_ank(row, col + cell, c, attr); /* 先頭バイトだけで切れている：ANKにフォールバック */
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

/* 2000セルすべてを普通の白いスペースで塗りつぶす――起動時に
   一度（ブート／DOSプロンプトが残していたものを消すため）、
   終了時に一度（DOSがまた使える状態に画面を戻すため）使う。
   どちらもvram_set_cell()を経由するので、ここでも実際に必要な
   セルだけが書き込まれる。 */
void vram_clear_all(void)
{
  int i;

  for (i = 0; i < VRAM_CELLS; i++) {
    vram_set_cell(i / VRAM_COLS, i % VRAM_COLS, 0x0020, ATTR_BASE);
  }
}

/* DOS/BIOSのテキストカーソル（点滅するハードウェアカーソル。
   input_dialog()の間だけ表示される――その"\x1b[>5l"/"\x1b[>5h"の
   呼び出し参照）を、普通のANSI CUPエスケープで位置決めする。これは
   上のvram_*関数群が解決しているSJIS／半角罫線の問題とは無関係――
   運ぶのはASCIIの数字だけでテキストは含まない――なので、VRAM
   経由ではなく、下のwrite_str()と全く同様に引き続きDOSコンソール
   経由で送っている。 */
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

/* ヘッダボックスの1行を描画する：左枠文字＋内容（BOX_WIDTH
   セルに合わせてスペース埋め／切り詰め）＋右枠文字。 */
void box_row(int row, unsigned char lb, unsigned char rb, char *content, unsigned int contentAttr)
{
  vram_ank(row, 0, lb, ATTR_BORDER);
  vram_puts_cells(row, 1, content, contentAttr, BOX_WIDTH);
  vram_ank(row, 1 + BOX_WIDTH, rb, ATTR_BORDER);
}

/* ヘッダボックスの区切り行を描画する：枠文字、BOX_WIDTH個の
   水平線セル、枠文字。build_dash_row() + box_row()の置き換え――
   これらはかつて（2セル幅の全角）水平線文字をBOX_WIDTHセルぶん
   並べた文字列をまず組み立てていたが、水平線文字が今は1セルの
   ANKコードになっており、ANKコードはvram_puts_cells()に渡せない
   （ファイル冒頭のコメント参照）ため、各セルを直接書いている。 */
void box_dash_row(int row, unsigned char lb, unsigned char rb)
{
  int i;

  vram_ank(row, 0, lb, ATTR_BORDER);
  for (i = 0; i < BOX_WIDTH; i++) vram_ank(row, 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, 1 + BOX_WIDTH, rb, ATTR_BORDER);
}

/* ヘッダボックスの上端（0行目）を描画する：角、水平線、タイトル
   メッセージ、水平線で、内側のBOX_WIDTHセルをちょうど埋める。幅は
   決め打ちにせず常にtext_width()でセル単位に計算するので、
   長さをハードコードすることなくどちらの言語テーブルにも対応
   できる。build_title_row() + box_row()の置き換え理由は上の
   box_dash_row()と同じ――BOXCH_Hは今やSJISテキストではなくANK
   コードなので、実際の（CP932の）タイトル文字列と1つの文字列に
   混ぜてvram_puts_cells()へ一括で渡すことができない。 */
/* 第7マイルストーンでの修正：実機で実測したところ、DISK_SEP_COL1
   の桁（ディスク行区切りの0行目での交差点）が、14サンプル中
   おおよそ5:9の比率で、空白と交差記号0x91の間でちらついていた
   ――このこと自体、この関数が毎フレーム最終的には同じ内容へ
   収束しているにもかかわらず、である。原因：以前のバージョンは
   まず埋め部分のすべてのセルを（単純にダッシュで、無条件に）書き、
   その後の最後の2回の呼び出しでDISK_SEP_COL1/2を交差記号で
   上書きしていた。この2回の書き込みは、実際に何かが変化したか
   どうかにかかわらず*毎フレーム*発生する――vram_set_cell()の
   ミラーは、そのセルへ*最後に*書いた値と*新しい*値が一致する
   ときだけ書き込みを省略するが、ここでは2つの段階が実際に
   毎フレーム順番に異なる2つの値を書いているのでスキップされない。
   つまり実機はそのセルへ毎フレーム2回の実際の書き込み――ダッシュ、
   そして交差記号――を見ており、画面はその間のフレーム中間の
   タイミングでサンプリングされうる。（別途実測：全角文字の
   右側セルは表示には影響しないが、そこに最後に書かれた値は
   そのまま保持される――つまりタイトル文字の右側セルの上に
   交差記号を置くと、見た目を変えずにVRAM上のバイトだけが変わる
   ことになり、これはここで観測された空白／交差記号パターンが
   そのようなセルに来ていたことと整合する。）
   修正：各セルの最終的な内容を、行を1回だけ走査してちょうど
   一度に決める。これにより1つのセルはフレームごとに最大1回
   （ミラーが既にその値を保持していれば0回）しか書き込まれない。
   第9マイルストーンでもタイトルを中央寄せしつつこの単一パス
   方式を保っている：交差記号は、"<< タイトル >>"ブロックの
   左右いずれかのダッシュ埋め部分を走査している間にのみ置く――
   つまり、括弧・タイトル文字列・時計前の隙間／時計の桁のいずれ
   にも既に占有されていない桁でのみ置く。もしDISK_SEP_COL1/2が
   それらの占有領域の中に来た場合（例えば別言語で長いタイトルに
   なったとき）は、その位置には交差記号を描かない――その桁は
   それを占有している内容が一度描いたものをそのまま保持する。
   これは「占有済みの桁には交差記号を置かない」というこのファイル
   の規則と一致している。 */
void draw_title_row(void)
{
  char *title;
  int titleCells;
  int totalFill;
  int leftFill;
  int rightFill;
  int i;
  int col;
  char datetime[DATETIME_WIDTH + 1];

  vram_ank(ROW_TITLE, 0, BOXCH_TL, ATTR_BORDER);
  vram_ank(ROW_TITLE, 1 + BOX_WIDTH, BOXCH_TR, ATTR_BORDER);

  title = g_title;
  titleCells = text_width(title);
  /* ダッシュ埋め、空白セル、"<< タイトル >>"（本家で実測：タイトルは
     左寄せではなく括弧の中で中央寄せされている――上のTITLE_DECOR_WIDTH
     のコメント参照）、さらに空白セル、再びダッシュ埋め、その後
     右側に時計フィールドとの隙間と時計フィールド、という順。
     totalFillは左右のダッシュにできるだけ均等に分配する（余った
     1セルは右側へ）ことで、中央寄せに見えるようにする――どちらに
     せよ、この行はタイトルの長さに関わらず常に右端ぴったりで
     終わる（従来どおり）。 */
  totalFill = BOX_WIDTH - TITLE_DECOR_WIDTH - titleCells - 2
              - TITLE_DATETIME_GAP - DATETIME_WIDTH - TITLE_TAIL_WIDTH;
  if (totalFill < 0) totalFill = 0; /* 防御的処理：タイトルが幅に収まらない場合 */
  leftFill = totalFill / 2;
  rightFill = totalFill - leftFill;

  col = 1;
  for (i = 0; i < leftFill; i++) {
    /* この行がディスク区切りの交差記号を置く2箇所のうちの1つ――
       埋めループの中でインラインにここで行う理由（後から別の
       無条件パスとして行うのではなく）は、このセルがちらつかない
       ようにするためであり、上の関数コメント参照。 */
    if (col == DISK_SEP_COL1 || col == DISK_SEP_COL2) {
      vram_ank(ROW_TITLE, col, BOXCH_TJ, ATTR_BORDER);
    } else {
      vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BORDER);
    }
    col++;
  }
  vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, '<', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, '<', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++;
  vram_puts_cells(ROW_TITLE, col, title, ATTR_TITLE, titleCells);
  col += titleCells;
  vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, '>', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, '>', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++;
  for (i = 0; i < rightFill; i++) {
    if (col == DISK_SEP_COL1 || col == DISK_SEP_COL2) {
      vram_ank(ROW_TITLE, col, BOXCH_TJ, ATTR_BORDER);
    } else {
      vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BORDER);
    }
    col++;
  }
  for (i = 0; i < TITLE_DATETIME_GAP; i++) { vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++; }

  format_datetime(datetime);
  for (i = 0; i < DATETIME_WIDTH; i++) {
    vram_ank(ROW_TITLE, col, (unsigned char)datetime[i], ATTR_TITLE); col++;
  }
  /* TITLE_TAIL_WIDTH：空白セル1つ、続けて枠線セルもう1つ。ちょうど
     この関数の冒頭で書かれる右上の角の直前に来る――上の
     TITLE_TAIL_WIDTHのコメント参照。 */
  vram_ank(ROW_TITLE, col, ' ', ATTR_TITLE); col++;
  vram_ank(ROW_TITLE, col, BOXCH_H, ATTR_BORDER); col++;
}

/* その場限りの即時書き込み。main()のフレーム前後の端末モード
   エスケープ（画面フレームの一部ではない）と、上のansi_goto()
   だけで使う。 */
void write_str(char *s)
{
  dos_write(s, (unsigned int)strlen(s));
}

/* ---- ディレクトリスキャン ------------------------------------------------ */

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

/* ---- マーク処理 ----------------------------------------------------------- */

/* ディレクトリは決してマーク不可――本家に対する実測：32件中、
   マーク可能だったのは非ディレクトリの18件のみで、14件の
   ディレクトリはマークできなかった。 */
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

/* SPACE/TAB：カーソル位置のエントリにマークを立てる（オフには
   戻さない――トグルするのはHOMEだけ）。ディレクトリでは何もせず、
   一覧が空でも何もしない。 */
void mark_cursor(void)
{
  if (g_count == 0) return;
  if (is_dir_entry(g_cursor)) return;
  g_marked[g_cursor] = 1;
}

/* HOME：何か1つでもマークされていれば全マークを解除し、そうで
   なければマーク可能な（非ディレクトリの）エントリすべてに
   マークを立てる。 */
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

/* ---- ディレクトリの移動 ----------------------------------------------- */

/* g_path＋nameをout[]へ組み立てる。'cap'はout[]の宣言サイズで、
   このファイルの他所でsappend()が強制しているのと同じ方法で
   強制する――あふれさせず切り詰め、マルチバイト文字を分断
   することは決してない。 */
void build_full_path(char *out, int cap, char *name)
{
  int p;

  p = 0;
  sappend(out, &p, g_path, cap);
  sappend(out, &p, name, cap);
}

/* nameの拡張子（大文字小文字を区別しない）がCOMまたはEXEなら真
   ――F2/Xでファイルを実行してよいかをdo_exec()が判断するのと
   同じルール。enter_selected()（第8マイルストーン）と共用：
   そのようなファイルへのEnterはここでは何もしない（プログラムの
   実行は従来どおりF2/Xのみ）が、それ以外の非ディレクトリへの
   Enterは代わりに組み込みビューア（do_view()）を開く。 */
int is_exec_ext(char *name)
{
  int len;
  int dotpos;
  int i;
  char ext[4];
  char c;

  len = strlen(name);
  dotpos = -1;
  for (i = 0; i < len; i++) {
    if (name[i] == '.') dotpos = i;
  }
  if (dotpos < 0 || (len - dotpos - 1) != 3) return 0;
  for (i = 0; i < 3; i++) {
    c = name[dotpos + 1 + i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    ext[i] = c;
  }
  ext[3] = 0;
  return (strcmp(ext, "COM") == 0 || strcmp(ext, "EXE") == 0);
}

/* ディレクトリエントリへのEnter："."（その場に留まる）、".."
   （1つ上の階層へ。"."と".."は本家同様に一覧に表示され、隠さない）、
   またはサブディレクトリ名（その中へ入る）。g_pathは常に'\\'で
   終わる。どちらの分岐もその不変条件を保つ。追記操作はすべて、
   このファイルの他所で固定長文字列を組み立てる場合と同じ
   規律で、sappend()経由でsizeof(g_path)に対して境界チェックされる。 */
void enter_selected(void)
{
  char *name;
  int len;
  int p;

  if (g_count == 0) return;

  name = &g_name[g_cursor * NAME_LEN];

  if (!is_dir_entry(g_cursor)) {
    /* 第8マイルストーン：COM/EXEはここでは引き続き何もしない
       （代わりにF2/Xで実行する――do_exec()自身の起動ルールと
       一致）。それ以外は組み込みビューアを開く。 */
    if (is_exec_ext(name)) return;
    do_view(name);
    return;
  }

  if (strcmp(name, ".") == 0) {
    return;
  }

  if (strcmp(name, "..") == 0) {
    len = strlen(g_path);
    if (len > 0 && g_path[len - 1] == '\\') len--;
    while (len > 0 && g_path[len - 1] != '\\') len--;
    if (len < 3) len = 3; /* ドライブルート "X:\\" はそのまま保つ */
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

/* ---- 削除 -------------------------------------------------------------- */

/* ベースの画面を描いた上に、小さなモーダルダイアログボックス
   （2行：プロンプト、そして任意でその下にエラー行）を重ねる。
   ここが触るセルはすべて、vram_set_cell()の「変化が無ければ
   スキップ」というチェック（上のvram_*節参照）を通るため、これは
   何かを空白にすることが決してない――背後の一覧は、ダイアログ
   ボックスが占めるまさにそのセルが変化するその瞬間まで表示され
   続ける。これは本家で実測した「専用のステータス行は無く、
   エラーはダイアログの中に現れ、プロンプトは表示されたまま」
   という挙動――マイルストーンのドキュメント参照。errmsgは
   「エラー行なし」を表すのに0を渡してよい。 */
#define DIALOG_WIDTH  60
#define DIALOG_ROW    10
#define DIALOG_COL    8

void draw_dialog(char *msg, char *errmsg)
{
  int i;
  int row;

  draw_screen_frame();

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

/* ---- 再利用可能なテキスト入力ダイアログ（Rename / mKdir） -----------------------
 * draw_dialog()と同種のボックスを描く（下の基本画面はそのまま
 * 見えており、ダイアログの領域だけが上書きされる）が、編集可能な
 * フィールドを1つ持つ：プロンプトの断片＋入力中のテキスト。
 * do_rename()とdo_mkdir()の両方から使われるので、ボックス・編集
 * キー・「エラーはインラインで表示、プロンプトは表示されたまま、
 * 入力継続」という挙動が、ちょうど1箇所にだけ存在する。
 * ------------------------------------------------------------------- */

/* 入力ダイアログの1フレームを描画する：基本画面、ボックスの枠、
   プロンプト＋入力済みテキストの行、その下の任意のエラー行、
   そして（一時的に表示される）テキストカーソルを最後に入力した
   文字のすぐ後ろに置き、ユーザーが今どこにいるか分かるようにする。
   'buf'には既に入力済みの'len'文字が入っている（まだ'len'の位置で
   NUL終端されているとは限らない――呼び出し元は戻るときにNUL終端
   するのであって、フレームごとにではない）。'line'はかなり
   余裕を持たせたサイズになっている：ここでのプロンプトの断片は
   短い日英ラベルであり、'buf'は最大でもINPUT_MAXLENバイトの
   ASCIIなので、これがあふれることには到底ならない。それでも
   sappend()が念のため防御的に制限をかけている――上の他の行組み立て
   関数すべてと同じ規律。 */
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
  vram_ank(row, DIALOG_COL, BOXCH_TL, ATTR_BORDER);
  for (i = 0; i < DIALOG_WIDTH; i++) vram_ank(row, DIALOG_COL + 1 + i, BOXCH_H, ATTR_BORDER);
  vram_ank(row, DIALOG_COL + 1 + DIALOG_WIDTH, BOXCH_TR, ATTR_BORDER);
  row++;

  p = 0;
  sappend(line, &p, prompt, sizeof(line));
  promptCells = text_width(line);
  buf[len] = 0; /* 下のline/sappendにはNUL終端されたC文字列が必要 */
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

  /* BOXCH_Vは今や1セルのANK枠文字になったので、フィールドの
     テキストは枠の桁の1セル後ろから始まる（枠が2セルの全角
     文字だった頃は2セルだった）。 */
  fieldCol = DIALOG_COL + 1 + promptCells + len;
  ansi_goto(fieldRow, fieldCol);
}

/* 1つのテキストフィールドについてモーダル編集ループを回す。
   'buf'には入る時点で初期値が入っている（空でもよいが、既に
   NUL終端されていること）。戻るときには、確定・キャンセルの
   いずれの場合も編集後のテキスト（同じくNUL終端）が入る。
   'buf'は少なくともmaxlen+1バイトで宣言されていなければならない
   ――INPUT_MAXLENを参照。このファイルのすべての呼び出し元が
   使う上限（8.3形式のDOS名）である。'errmsg'は「開始時にエラー
   行なし」を表すのに0を渡してよい。0以外のerrmsgを渡すことで、
   呼び出し元は失敗したDOS操作の後、同じプロンプトの横にエラーを
   表示したまま、入力済みのテキストもそのままの状態でこのループへ
   再入できる――別の「何かキーを押してください」ダイアログの陰に
   プロンプトを失わせることなく。
   編集：BS（0x08――このダイアログの外でKEY_LEFTが使うのと同じ
   コードだが、ここでは常にバックスペースであり、「左へ移動」で
   あることは決して無い）は最後の1文字を削除する。表示可能な
   ASCII（0x20-0x7E）は、maxlen未満の余地がまだあれば追記される。
   それ以外はすべて無視される。編集キーが押されると、ユーザーが
   今それに対処しているという理由で、表示中のエラーは消える。
   このプログラムの他のあらゆる場所ではテキストカーソルは隠されて
   いる（main()の起動時の"\x1b[>5h"参照）ので、このループの
   生存期間中だけ表示し、どの脱出経路でも戻る前に必ず再び隠す。
   Enterで入力が確定されれば1、ESCでキャンセルされれば0を返す。 */
int input_dialog(char *prompt, char *buf, int maxlen, char *errmsg)
{
  int len;
  int key;

  len = strlen(buf);
  if (len > maxlen) len = maxlen; /* 防御的処理 */

  write_str("\x1b[>5l"); /* 編集中はテキストカーソルを表示する */

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
    if (key == 0x08) { /* BS。KEY_LEFTと同じコードだが、ここではBSとして扱う */
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
    /* それ以外のキー（矢印、TAB、HOMEなど）：このダイアログでは無視する */
  }
}

/* D/d：マークされたファイル、あるいは（何もマークされていなければ）
   カーソル位置の1件を削除する。ターゲットの決め方は、食い違う
   ことがありうる2つのケースではなく、ただ1つのルール――「マークが
   空でなければマーク集合、空ならカーソル位置のエントリ」――で
   ある。マイルストーンのドキュメント参照。
   ディレクトリは決してマーク集合に入らない（mark_cursor()が
   拒否する）ので、ディレクトリが「対象」になりうるのは、マーク
   無し・カーソルがディレクトリ上にあるケースだけである。それは
   事前にとらえて、黙ってスキップしたり別の何かを黙って削除したり
   するのではなく、明示的なエラーダイアログで拒否する。成功時は
   本家に合わせて「削除完了」メッセージは表示しない。一覧が単に
   後で短くなっているだけである。 */
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

  /* マーク集合：ディレクトリを一切含まないことが保証されている
     （mark_cursor()が決してマークしないため）ので、ここでは
     エントリごとのディレクトリチェックは不要。 */
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

/* R/r：カーソル位置の1件だけを名前変更する（マークは使わない――
   対象は常にカーソル位置のエントリで、マーク集合を優先するDとは
   異なる）。"."と".."は名前変更不可で、enter_selected()が"."を
   何もしないものとして扱うのと同様、黙って無視される。
   入力ダイアログには現在の名前があらかじめ入っている。確定すると
   dos_rename()を試み、失敗した場合はエラー行付きでダイアログを
   再度開き、入力欄にはユーザーが入力したテキストがそのまま
   残っているので、全部打ち直さずに修正できる。 */
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

  strcpy(buf, name); /* 名前は最大でNAME_LEN-1＝INPUT_MAXLEN文字 */

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

/* K/k：カレントパス（g_path）に新しいディレクトリを作成する。
   入力ダイアログは空の状態で始まる。確定するとdos_mkdir()を試み、
   失敗した場合（既に同名が存在する等）は、上のdo_rename()と
   同じ形で、エラー行付きでダイアログを再度開く。 */
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

/* ---- ドライブ変更（F1 / Logdsk） ---------------------------------------- */

/* L/l、F1：カレントドライブを変更する。原物の最下段でも1番目
   （F1）の位置にあるコマンドで、位置はそこに合わせてある
   （docs/filer-measure-05.md）。入力は1文字のドライブ名で、
   "A" でも "A:" でも受ける（大文字小文字は問わない）。

   **有効性の確認は AH=0Eh の戻り値では行わない。**AH=0Eh は
   存在しないドライブを指定されても何も報告しないので、先に
   AH=36h（dos_diskfree()）を投げて 0xFFFF が返らないことを
   確認し、それが通ってから初めて実際に切り替える。切り替えた
   後にも AH=19h（dos_getdrive()）で本当に移ったかを確かめる
   ――要求した側の正しさは、末端がそうなっている証明にならない。

   エラーは他のダイアログと同じく、専用行ではなくダイアログの
   中にインラインで出し、入力中の文字列を残したまま入力を
   継続させる（原物に対する実測。docs/filer-measure-04.md）。

   成功したらパス・一覧・カーソル・マークをすべて新しいドライブ
   のものへ入れ替える。マークは前のドライブのファイルを指して
   いるので必ず捨てる。 */
#define LOGDSK_MAXLEN 2   /* "A" または "A:" */

void do_logdsk(void)
{
  char buf[LOGDSK_MAXLEN + 1];
  char *err;
  int confirmed;
  int c;
  unsigned int drive;
  unsigned int spc;
  unsigned int availClus;
  unsigned int bytesPerSec;
  unsigned int totalClus;

  buf[0] = 0;
  err = 0;
  for (;;) {
    confirmed = input_dialog(MSG(MSG_LOGDSK_PROMPT), buf, LOGDSK_MAXLEN, err);
    if (!confirmed) {
      draw_screen();
      return;
    }
    if (buf[0] == 0) {
      err = MSG(MSG_LOGDSK_ERR_EMPTY);
      continue;
    }

    c = buf[0];
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    /* 2文字目はコロンだけ許す。"AB" のようなものは弾く */
    if (c < 'A' || c > 'Z' || (buf[1] != 0 && buf[1] != ':')) {
      err = MSG(MSG_LOGDSK_ERR_INVALID);
      continue;
    }

    drive = (unsigned int)(c - 'A') + 1; /* AH=36hは1がA: */
    spc = dos_diskfree(drive, &availClus, &bytesPerSec, &totalClus);
    if (spc == 0xFFFF) {
      err = MSG(MSG_LOGDSK_ERR_INVALID);
      continue;
    }

    dos_setdrive(drive - 1); /* AH=0Ehは0がA: */
    if (dos_getdrive() != drive - 1) {
      err = MSG(MSG_LOGDSK_ERR_INVALID);
      continue;
    }
    break;
  }

  read_path();
  read_dir();
  g_cursor = 0;
  clear_marks();
  draw_screen();
}

/* ---- 実行（F2 / eXec） ------------------------------------------------- */

/* X/x、F2：カーソル位置のファイルを実行する（マークは使わない――
   対象は常にカーソル位置のエントリで、do_rename()と同じ
   「単一・カーソルのみ」というターゲットルールであり、
   do_delete()/do_copy()/do_move()の「マークが空でなければマーク
   集合」というルールではない――キー1つで複数のプログラムを実行
   するのは意味をなさないため）。実行するのは.COMまたは.EXE
   ファイルのみ。それ以外（ディレクトリ、拡張子が無いファイル、
   別の拡張子）は、何もせず黙って終わるのではなく、明示的な
   エラーダイアログで拒否する（do_exec()タスクの要件参照）。
   子プロセスはINT 21h AH=4Bh（上のdos_exec()）で実行する：この
   プログラムのMZヘッダはminalloc == maxallocに設定されており
   （ここで決め打ちにせず実測で確認済み）、ロード時にコンベンショナル
   メモリを丸ごと確保することは無いので、DOSが子プロセスを
   ロードするための空きメモリが残る。
   子プロセスは画面をどんな状態にしていってもよく（普通の
   コンソールプログラムなら大部分、あるいは全部を上書きしている
   だろう）、このプログラムには子プロセスが何を残していったか
   知る術が無いので、戻ってきたら何か他を描画する前に必ずVRAM
   シャドウミラー（vram_shadow_init()――上のvram_*節参照）を
   無効化し、実態と一致しなくなった古びた「既に画面にあるもの」
   状態を信用するのではなく、次の再描画のすべてのセルを実際に
   書き込ませる。
   「何かキーを押してください」ダイアログを（成功・失敗いずれの
   場合も）まず表示するので、このプログラム自身のフルスクリーン
   再描画が上書きしてしまう前に、ユーザーは子プロセスが何を
   表示したのかを実際に見ることができる。 */
void do_exec(void)
{
  char *name;
  char path[96];
  char ext[4];
  char cmdTail[2];
  char fcb1[16];
  char fcb2[16];
  char param[14];
  unsigned int ds;
  unsigned int off;
  int i;
  int len;
  int dotpos;
  int extlen;
  int rc;
  char c;

  if (g_count == 0) return;

  if (is_dir_entry(g_cursor)) {
    draw_dialog(MSG(MSG_EXEC_ERR_NOTEXE), 0);
    dos_getch();
    draw_screen();
    return;
  }

  name = &g_name[g_cursor * NAME_LEN];
  len = strlen(name);
  dotpos = -1;
  for (i = 0; i < len; i++) {
    if (name[i] == '.') dotpos = i;
  }
  extlen = (dotpos >= 0) ? (len - dotpos - 1) : 0;
  if (dotpos < 0 || extlen != 3) {
    draw_dialog(MSG(MSG_EXEC_ERR_NOTEXE), 0);
    dos_getch();
    draw_screen();
    return;
  }
  for (i = 0; i < 3; i++) {
    c = name[dotpos + 1 + i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    ext[i] = c;
  }
  ext[3] = 0;
  if (strcmp(ext, "COM") != 0 && strcmp(ext, "EXE") != 0) {
    draw_dialog(MSG(MSG_EXEC_ERR_NOTEXE), 0);
    dos_getch();
    draw_screen();
    return;
  }

  build_full_path(path, sizeof(path), name);

  /* コマンドテール：長さ接頭辞付きの文字列（カウントバイト、続けて
     その文字数ぶんの文字、末尾にCR）――ここでは空（カウント0）で、
     DOSプロンプトで引数を何も打たずにEnterを押した場合と同じ。 */
  cmdTail[0] = 0;
  cmdTail[1] = 0x0d;
  for (i = 0; i < 16; i++) { fcb1[i] = 0; fcb2[i] = 0; }
  for (i = 0; i < 14; i++) param[i] = 0;

  /* EXECパラメータブロック（INT 21h AH=4Bh）：word型の環境
     セグメント（0＝このプログラム自身の環境を子プロセスへ
     コピーする、通常のケース）、続けてoffset:segment形式の
     farポインタ3つ（コマンドテール、FCB1、FCB2）。ここでの
     ポインタはすべてこの同じスモールモデルのデータセグメント
     内のバッファを指すので、それぞれのセグメント側は単に
     get_ds()を繰り返し呼んでいるだけである。 */
  ds = get_ds();
  off = (unsigned int)cmdTail;
  param[2] = (char)(off & 0xff);
  param[3] = (char)(off >> 8);
  param[4] = (char)(ds & 0xff);
  param[5] = (char)(ds >> 8);
  off = (unsigned int)fcb1;
  param[6] = (char)(off & 0xff);
  param[7] = (char)(off >> 8);
  param[8] = (char)(ds & 0xff);
  param[9] = (char)(ds >> 8);
  off = (unsigned int)fcb2;
  param[10] = (char)(off & 0xff);
  param[11] = (char)(off >> 8);
  param[12] = (char)(ds & 0xff);
  param[13] = (char)(ds >> 8);

  rc = dos_exec(path, (unsigned char *)param);

  /* 子プロセスは画面に何を書いていてもおかしくない（あるいは、
     dos_exec()自体がロードする前に失敗していれば何も書いて
     いないかもしれない）――上のこの関数の冒頭コメントのとおり、
     以下のすべての書き込みが実際に行われるよう、無条件にミラーを
     無効化する。
     実測したバグ（実機、F2 → COMMAND.COM → EXIT）：子プロセス
     自身の出力によって実際の画面がスクロールすることがあり、
     これにより古い内容（例えばこのプログラム自身のファンクション
     キー行）が、このプログラムの通常の再描画が自力では再訪しない
     セルへずれ込んでしまう――23行目はファイル一覧とファンクション
     キー行の間にあって誰もそこを描画せず、2つの一覧の列の間の
     1セル分の隙間と、画面の一番最後の列も同様に誰も描画しない。
     ミラーを無効化するだけではこれには不十分である：下の
     draw_dialog()/draw_screen()は自分たちが実際に使うセルしか
     再塗りしないので、その集合に含まれない古びたセルは永遠に
     古びたまま残ってしまう――ずれた内容がそこにそのまま居座る。 */
  vram_shadow_init();
  /* このプログラム自身の描画がそのセルに二度と触れないとしても、
     25×80セルすべてを一度、強制的に空白で確実に上書きする――
     これが終わって初めて、以下の通常の部分再描画関数に処理を
     任せても安全になる。 */
  vram_clear_all();

  if (rc != 0) {
    draw_dialog(MSG(MSG_EXEC_ERR_FAILED), 0);
    dos_getch();
    draw_screen();
    return;
  }

  draw_dialog(MSG(MSG_EXEC_PRESS_KEY), 0);
  dos_getch();
  draw_screen();
}


/* ---- 組み込みビューア（第8マイルストーン） --------------------------------------- */

/* g_viewHandleからg_viewReadBufへ補充する。ビューア用に実際に
   dos_read()を呼ぶのはここだけ――このようにまとまった単位で
   読む（1バイトごとにINT 21hを1回呼ぶことは決してしない）理由は
   VIEW_READ_BUFのコメント参照。これが、大きなファイルの走査を
   遅くならないようにしている仕組みである。 */
void vreader_fill(void)
{
  g_viewReadLen = dos_read((unsigned int)g_viewHandle, g_viewReadBuf, (unsigned int)VIEW_READ_BUF);
  if (g_viewReadLen < 0) g_viewReadLen = 0;
  g_viewReadPos = 0;
}

/* ビューア用にpathを開く。成功時1、失敗時0を返す。リーダーの
   状態（バッファ＋プッシュバック）をすべてリセットするので、
   同じファイルに対して新しく前方向のパスをやり直すために
   何度でも安全に呼べる――下のview_goto_line()（唯一のもう1つの
   呼び出し元）参照。 */
int vreader_open(char *path)
{
  int h;

  h = dos_open(path, 0);
  if (h < 0) return 0;
  g_viewHandle = h;
  g_viewReadLen = 0;
  g_viewReadPos = 0;
  g_viewPushbackLen = 0;
  return 1;
}

void vreader_close(void)
{
  if (g_viewHandle >= 0) dos_close((unsigned int)g_viewHandle);
  g_viewHandle = -1;
}

/* 生の次の1バイト（0～255）、ファイル終端なら-1を返す。バッファ
   経由――VIEW_READ_BUFと上のvreader_fill()参照。 */
int vreader_getc(void)
{
  if (g_viewReadPos >= g_viewReadLen) {
    vreader_fill();
    if (g_viewReadLen <= 0) return -1;
  }
  return (unsigned char)g_viewReadBuf[g_viewReadPos++];
}

/* vreader_getc()と同様だが、まずg_viewPushback[]から出す――
   宣言部分（g_viewPushbackLenとともに上にある）にある通り、
   view_read_line()がこれを必要とする理由：先読みした1バイト、
   あるいは全角文字のペア丸ごとが実は*次*の表示行に属すると
   分かった場合、それを失わずに戻す必要があるため。 */
int vreader_getc_pb(void)
{
  int c;

  if (g_viewPushbackLen > 0) {
    c = g_viewPushback[0];
    g_viewPushbackLen--;
    if (g_viewPushbackLen > 0) g_viewPushback[0] = g_viewPushback[1];
    return c;
  }
  return vreader_getc();
}

void vreader_pushback1(int c)
{
  if (g_viewPushbackLen == 1) g_viewPushback[1] = g_viewPushback[0];
  g_viewPushback[0] = c;
  g_viewPushbackLen++;
}

void vreader_pushback2(int c1, int c2)
{
  g_viewPushback[0] = c1;
  g_viewPushback[1] = c2;
  g_viewPushbackLen = 2;
}

/* 表示行を1行（タブを展開し、全角ペアを分断せずにVRAM_COLSセルで
   折り返した状態で）out[]（NUL終端された生のCP932バイト文字列、
   最大でVIEW_LINE_BUF-1バイト――ここでは画面セル1つがANKでも
   全角の半分でもちょうど1バイトなので、VIEW_LINE_BUFの上限で
   常に十分な余地があるが、それでも下の各追記はこのファイルの
   他所でsappend()が使うのと同じ規律で'cap'に対してチェックされる）
   へ読み込む。*outCellsにはその行の画面上の幅が入る。*outHadNLは、
   この表示行が本物の行終端（CRLFまたはLF）で終わったときにのみ1に
   なる――折り返しでは決して1にならず、末尾に改行の無いファイルの
   最後のEOF終端行でも決して1にならない（どちらも実測した挙動。
   docs/filer-measure-07.md参照）。行が生成できれば1、ファイルに
   何も残っていなければ（この呼び出しがちょうどファイル終端から
   始まった場合）0を返す。 */
int view_read_line(char *out, int cap, int *outCells, int *outHadNL)
{
  int cell;
  int len;
  int producedAny;
  int c;
  int c2;
  int spaces;
  int i;

  cell = 0;
  len = 0;
  producedAny = 0;

  for (;;) {
    c = vreader_getc_pb();

    if (c == -1) {
      if (!producedAny) { *outCells = 0; *outHadNL = 0; return 0; }
      out[len] = 0;
      *outCells = cell;
      *outHadNL = 0;
      return 1;
    }

    if (c == 0x0d) {
      c2 = vreader_getc_pb();
      if (c2 != 0x0a && c2 != -1) vreader_pushback1(c2);
      out[len] = 0;
      *outCells = cell;
      *outHadNL = 1;
      return 1;
    }
    if (c == 0x0a) {
      out[len] = 0;
      *outCells = cell;
      *outHadNL = 1;
      return 1;
    }

    if (c == 0x09) {
      spaces = VIEW_TAB_WIDTH - (cell % VIEW_TAB_WIDTH);
      if (cell + spaces > VRAM_COLS) {
        vreader_pushback1(0x09);
        out[len] = 0;
        *outCells = cell;
        *outHadNL = 0;
        return 1;
      }
      for (i = 0; i < spaces; i++) {
        if (len + 1 > cap - 1) break; /* 防御的処理：実際には到達しないはず */
        out[len] = ' ';
        len++;
      }
      cell += spaces;
      producedAny = 1;
      continue;
    }

    if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) {
      c2 = vreader_getc_pb();
      if (c2 == -1) {
        /* EOFで先頭バイトだけで切れている場合：このファイルの他所で
           text_width()/sappend()が使うのと同じルールで、1セルの
           ANKにフォールバックする。 */
        if (cell + 1 > VRAM_COLS || len + 1 > cap - 1) {
          vreader_pushback1(c);
          out[len] = 0; *outCells = cell; *outHadNL = 0;
          return 1;
        }
        out[len] = (char)c; len++;
        cell++;
        producedAny = 1;
        continue;
      }
      if (cell + 2 > VRAM_COLS || len + 2 > cap - 1) {
        vreader_pushback2(c, c2);
        out[len] = 0;
        *outCells = cell;
        *outHadNL = 0;
        return 1;
      }
      out[len] = (char)c; len++;
      out[len] = (char)c2; len++;
      cell += 2;
      producedAny = 1;
      continue;
    }

    /* 普通のANKバイト */
    if (cell + 1 > VRAM_COLS || len + 1 > cap - 1) {
      vreader_pushback1(c);
      out[len] = 0; *outCells = cell; *outHadNL = 0;
      return 1;
    }
    out[len] = (char)c;
    len++;
    cell++;
    producedAny = 1;
  }
}

/* ファイル全体を（現在のリーダー位置から――do_view()は常に
   vreader_open()の直後、つまりバイト0からこれを呼ぶ）一度だけ
   走査し、内容を何も保持せずに表示行数だけを数える。大きな
   ファイルの全体サイズを訪れるのはここだけであり、
   vreader_getc()のバッファを通した1回の前方向パスに過ぎない
   （1バイトごとに1回のdos_read()呼び出しは決してせず、
   ファイルの行数に応じた配列も決して確保しない）ので、
   常に必ず終了し、ファイルサイズに比例したメモリを確保することも
   決してない。 */
int view_count_lines(void)
{
  char dummy[VIEW_LINE_BUF];
  int cells;
  int hadNL;
  int n;

  n = 0;
  while (view_read_line(dummy, sizeof(dummy), &cells, &hadNL)) n++;
  return n;
}

/* 次のview_read_line()呼び出しが表示行'target'（1始まり）を
   返すよう、リーダーの位置を再設定する。どこにもランダムアクセス
   用の行インデックスは保持していない――g_viewLineNoの上にある
   ビューア状態のコメント参照――ので、これは常にファイルを開き
   直し、先頭から前方向に読み直して'target'-1行ぶんを読み捨てる。
   このコストはファイルの総サイズにではなく、'target'がファイルの
   どれだけ奥にあるかに比例する。そして、view_count_lines()と
   同じ理由（単純な前方向スキャンであること）で常に終了する――
   非常に大きなファイルの奥まで'target'が進むほど遅くなるが、
   ハングすることは決して無い。
   成功時1、ファイルを開き直せなかった場合は0を返す
   （do_view()が既に一度正常に開いている以上、起こらないはず）。 */
int view_goto_line(int target)
{
  char dummy[VIEW_LINE_BUF];
  int cells;
  int hadNL;
  int i;

  vreader_close();
  if (!vreader_open(g_viewPath)) return 0;
  for (i = 1; i < target; i++) {
    if (!view_read_line(dummy, sizeof(dummy), &cells, &hadNL)) break; /* EOFより後ろ */
  }
  return 1;
}

/* VROW_HEADER行：左に"File name: <path>"（反転）、右に
   "Line No: N/M"（ラベルはシアン／ATTR_LABEL、値は黄色／
   ATTR_TITLE――docs/filer-measure-07.md参照）。以下の4つの
   vram_puts_cells()/vram_ank()呼び出しのうち、いずれか正確に
   1つだけがすべてのセルを書く（同じフレーム内で1つのセルが
   2つの異なる値で二度書かれることは決して無い）――0行目は、
   このファイル自身の歴史の中で、1フレームの中でセルの内容が
   2回に分けて決められると実際にちらつくことが分かっている
   唯一の行なので（上のdraw_title_row()の第7マイルストーン
   修正コメント参照）、ビューア自身の0行目ヘッダも、他の行で
   draw_path_line()/draw_disk_line()が使っている「まず値、次に
   ラベルを重ね塗り」という2パス方式ではなく、同じ単一パスの
   規律に従っている。 */
void draw_view_header(void)
{
  char leftBuf[VIEW_PATH_BUF + 16];
  char labelPart[16];
  char valuePart[24];
  char numbuf[12];
  int p;
  int leftWidth;
  int labelCells;
  int valueCells;
  int rightCells;
  int rightStart;
  int gapCol;

  p = 0;
  sappend(leftBuf, &p, MSG(MSG_VIEW_FILENAME_LABEL), sizeof(leftBuf));
  sappend(leftBuf, &p, g_viewPath, sizeof(leftBuf));

  p = 0;
  sappend(labelPart, &p, MSG(MSG_VIEW_LINENO_LABEL), sizeof(labelPart));
  labelCells = text_width(labelPart);

  p = 0;
  format_u32_plain(0, (unsigned int)g_viewLineNo, numbuf);
  sappend(valuePart, &p, numbuf, sizeof(valuePart));
  sappend(valuePart, &p, "/", sizeof(valuePart));
  format_u32_plain(0, (unsigned int)g_viewTotalLines, numbuf);
  sappend(valuePart, &p, numbuf, sizeof(valuePart));
  valueCells = text_width(valuePart);

  rightCells = labelCells + valueCells;
  rightStart = VRAM_COLS - rightCells;
  if (rightStart < 0) rightStart = 0;
  gapCol = rightStart - 1;
  leftWidth = (gapCol > 0) ? gapCol : 0;

  vram_puts_cells(VROW_HEADER, 0, leftBuf, ATTR_REV, leftWidth);
  if (gapCol >= 0) vram_ank(VROW_HEADER, gapCol, ' ', ATTR_BASE);
  vram_puts_cells(VROW_HEADER, rightStart, labelPart, ATTR_LABEL, labelCells);
  vram_puts_cells(VROW_HEADER, rightStart + labelCells, valuePart, ATTR_TITLE, valueCells);
}

/* VROW_CONTENT_TOP ～ VROW_CONTENT_TOP+VIEW_CONTENT_ROWS-1行：
   ファイルの内容そのものを、既に折り返し済みの表示行1行ずつを
   ATTR_VALUE（普通の白――docs/filer-measure-07.md参照）で表示する。
   行末マーク（上のVIEW_EOL_MARK_B1/B2）は、vram_puts_cells()へ
   渡す同じ行バッファに追記される――その関数にとっては、この
   ファイルの他の全角テキストと全く同様に、ただの2バイトのCP932
   文字である――ただし、VRAM_COLSを超えずに収まる余地がある場合
   のみ。表示行がちょうどその行を埋めきり、かつ本物の改行で
   終わっている場合はマークを置く場所が無く、その場合は黙って
   省略される――このファイルの他所でsappend()/vram_puts_cells()が
   既に使っている防御的な切り詰め方針と同じである。 */
void draw_view_content(void)
{
  char buf[VIEW_LINE_BUF];
  int cells;
  int hadNL;
  int row;
  int i;
  int got;

  for (i = 0; i < VIEW_CONTENT_ROWS; i++) {
    row = VROW_CONTENT_TOP + i;
    got = view_read_line(buf, sizeof(buf), &cells, &hadNL);
    if (!got) {
      vram_puts_cells(row, 0, "", ATTR_VALUE, VRAM_COLS);
      continue;
    }
    if (hadNL && cells + 2 <= VRAM_COLS) {
      buf[cells] = (char)VIEW_EOL_MARK_B1;
      buf[cells + 1] = (char)VIEW_EOL_MARK_B2;
      buf[cells + 2] = 0;
    }
    vram_puts_cells(row, 0, buf, ATTR_VALUE, VRAM_COLS);
  }
}

/* VROW_CMD行：ファイラ自身のファンクションキー行と物理的に
   同じ行だが、あの10フィールドの割り当てではなく、ビューア専用の
   小さなヒントを表示する――このプログラムには実測済みの
   「ビューアモード」用ファンクションキーレイアウトが無い
   （実測されているのは本家自身のeXec/Copy/Delete/…行だけ。
   docs/filer-measure-05.md参照）ため、意図的にg_fkeyLabel[]/
   g_fkeyCol[]/draw_cmdline()を再利用しない。最低限の要件：
   ESCで一覧へ戻れることが視覚的に明らかであること。 */
void draw_view_cmdline(void)
{
  int col;
  int cells;

  for (col = 0; col < VRAM_COLS; col++) {
    vram_ank(VROW_CMD, col, ' ', ATTR_BASE);
  }
  cells = text_width(MSG(MSG_VIEW_CMDLINE));
  vram_puts_cells(VROW_CMD, 0, MSG(MSG_VIEW_CMDLINE), ATTR_VALUE, cells);
}

/* ビューアの1フレーム全体を描画する。常にまずリーダーを
   g_viewLineNoへ再配置し（view_goto_line()のコメント参照――
   「リーダーは前回呼び出し以降の変化に対しても既に正しい位置に
   ある」という永続的な不変条件は保持していないので、これにより
   前回のフレーム以降g_viewLineNoが何によって変わっていても常に
   正しくなる）、その後そこからVIEW_CONTENT_ROWS行ぶんを内容行用に
   前方向に読み込む。 */
void view_render(void)
{
  view_goto_line(g_viewLineNo);
  draw_view_header();
  vram_puts_cells(VROW_BLANK, 0, "", ATTR_BASE, VRAM_COLS);
  draw_view_content();
  draw_view_cmdline();
}

/* 非ディレクトリかつ非COM/EXEのエントリでEnter（第8マイルストーン）：
   カーソル位置のファイルを読み取り専用のフルスクリーンビューアで
   開く――全体の設計についてはg_fkeyHiPos[]の上にある第8
   マイルストーンのコメント、実際に何を実測したか（画面
   レイアウト・キー動作・タブ／折り返し／行末の扱い）については
   docs/filer-measure-07.mdを参照。Down/Up/HOME/ESCに加え、第11
   マイルストーン以降はROLL UP/ROLL DOWN（上のKEY_ROLLUP/
   KEY_ROLLDOWNのコメントとスキャンコードについては
   docs/bios-key-measure-01.md参照）を実装している。（ESCで）戻ると
   ディレクトリ一覧を以前と全く同じように再描画する――このファイルの
   他のあらゆるモーダル経路と同様、外部からの完全な再描画でしか
   直せないような状態を画面に残すことは決して無い。 */
void do_view(char *name)
{
  int key;
  int viewing;

  build_full_path(g_viewPath, sizeof(g_viewPath), name);

  if (!vreader_open(g_viewPath)) {
    draw_dialog(MSG(MSG_VIEW_ERR_OPEN), 0);
    dos_getch();
    draw_screen();
    return;
  }

  /* 表示行の総数を得るための1回の前方向全走査――view_count_lines()の
     コメント参照。大きなファイルのサイズを完全に訪れるのはここ
     だけで、ハングしないよう、1バイトずつではなくバッファ付きの
     リーダーを通す――その後、実際の表示のために1行目からやり
     直す。 */
  g_viewTotalLines = view_count_lines();
  vreader_close();

  g_viewLineNo = 1;
  view_goto_line(1);

  viewing = 1;
  while (viewing) {
    view_render();
    key = dos_getch();

    if (key == KEY_ESC) {
      viewing = 0;
    } else if (key == KEY_DOWN) {
      if (g_viewLineNo < g_viewTotalLines) g_viewLineNo++;
    } else if (key == KEY_UP) {
      if (g_viewLineNo > 1) g_viewLineNo--;
    } else if (key == KEY_HOME) {
      g_viewLineNo = 1;
    } else if (key == KEY_ROLLUP) {
      /* 22行(VIEW_CONTENT_ROWS)先へ進む。実測どおり総行数を超えない
         （filer-measure-07.md：内容が22行なので重なりは無い） */
      g_viewLineNo += VIEW_CONTENT_ROWS;
      if (g_viewLineNo > g_viewTotalLines) g_viewLineNo = g_viewTotalLines;
    } else if (key == KEY_ROLLDOWN) {
      /* 22行戻る。先頭(1行目)より前には行かない（実測どおり） */
      g_viewLineNo -= VIEW_CONTENT_ROWS;
      if (g_viewLineNo < 1) g_viewLineNo = 1;
    }
    /* 上記以外のキー：何もしない */
  }

  vreader_close();
  draw_screen();
}

/* ---- コピー／移動 --------------------------------------------------------- */

/* destDir + "\" + name をout[]へ組み立てる（常にg_pathに対して
   結合するbuild_full_path()とは異なる――Copy/Moveの移動先は
   カレントディレクトリではなく、ユーザーがたった今入力した
   ディレクトリである）。destDirが既に区切りのバックスラッシュで
   終わっていない場合にのみそれを追加するので、"B:\SUBDIR"と
   "B:\SUBDIR\"のどちらを入力しても動作する。境界の規律は
   このファイルの他所のbuild_full_path()/sappend()と同じ。 */
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

/* pathに（非ディレクトリの）ファイルが既に存在すれば真。
   dos_setdta()/dos_findfirst()を一回限りの検索として再利用する
   ――read_dir()もFindFirst/FindNextの自前のスキャンでg_dtaを
   使っているが、この2つは決して同時に走らないので安全である
   （read_dir()は次に走るときには必ず自分でdos_setdta()を
   呼び直す）。検索属性マスクは意図的にATTR_DIRを含めていない：
   DOSのFindFirstはマスクに関わらず常に普通のファイルにマッチ
   するが、ディレクトリにマッチするのはATTR_DIRがマスクに含まれて
   いる場合だけなので、これは「この名前のファイル」を探して
   おり、これはまさにCopy/Moveが答えを必要としている上書きの
   問いそのものである。 */
int file_exists(char *path)
{
  int ok;

  dos_setdta(g_dta);
  ok = dos_findfirst(path, ATTR_RDONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE);
  return (ok == 0) ? 1 : 0;
}

/* 入力されたdestDir文字列が意味する移動先のドライブ文字：
   自分自身に"X:"接頭辞があればそれ、無ければカレント
   ディレクトリのドライブ（g_path[0]、常に大文字――read_path()
   参照）。do_move()が同一ドライブ内リネームかドライブをまたぐ
   コピー＋削除かを判断するために使う。 */
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

/* 大文字小文字を区別しないパス比較（このプログラムのstring.h
   サブセットにはstricmp()が無い）。Moveの移動先が結局のところ
   移動元と同一になっているケース（例えばユーザーがカレント
   ディレクトリをそのまま打ち直した場合）だけを防ぐために使う
   ――このチェックが無いと、move_confirm_and_move()の上書き処理が
   ファイルを削除してから、再作成に失敗してしまう。 */
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

/* srcPathからdstPathへ1つのファイルをコピーする（INT 21h
   AH=3Dh open / 3Fh read / 3Ch create / 40h write / 3Eh close）。
   その後、AH=57hでコピー元のタイムスタンプをコピー先へ移す――
   まだ開いているコピー元ハンドルから取得し、まだ開いている
   コピー先ハンドルへ設定する、両方を閉じる前に。単一の
   グローバルなg_copybuf（宣言部分参照）をCOPY_BUF_SIZEバイト単位
   で使い、スタックバッファは使わない：SmallerCのスモールモデルは
   全データを1つの64KBセグメントに収めており、このプログラムの
   ディレクトリ一覧用配列（g_nameなど、MAX_ENTRIESでサイズ決め）が
   既にその大きな割合を占めているため、もう一つの既に大きな
   バッファをスタック上にも重複して置いてはならない。
   成功時0、失敗時は非0を返す。失敗時、部分的に書き込まれた
   コピー先ファイルは意図的に削除せずそのままにする――do_move()は
   ここでの非0の戻り値を「コピー元は削除してはならない」という
   意味として頼りにしており、ここで後片付けを勝手に推測すると
   かえってデータを壊す危険がある。 */
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
    if (n == 0) break; /* ファイル終端 */
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

/* do_copy()の「カーソルのみ」と「マーク集合」双方の経路が共用する
   ファイルごとのコピー処理：コピー元／コピー先のパスを組み立て、
   既存のコピー先ファイルを上書きする前にY/N/ESCで確認する
   （本家は同名コピーを確認なしで一律拒否する――この実装が代わりに
   尋ねる理由はdo_copy()冒頭コメント参照）。その後コピーする。
   戻り値：1＝コピーした、0＝スキップした（上書き確認でNだった。
   エラーではない）、-1＝失敗した（既にエラーダイアログを表示
   済み）、-2＝上書き確認でユーザーがESCを押した。これは「その
   1ファイルだけでなく、操作全体を中止する」ことを意味する。 */
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

/* ファイルごとのMove処理――戻り値の意味はcopy_confirm_and_copy()
   と同じ。sameDriveはdo_move()が一度だけ計算する（1回のMove操作の
   間、destDirはすべてのファイルで同じなので、そこから示される
   ドライブ文字をファイルごとに計算し直す必要はない）。同じ
   ドライブならINT 21h AH=56hでその場でリネームし、データは
   一切コピーしない。ドライブをまたぐ場合はcopy_one_file()に
   フォールバックし、コピーが実際に成功した場合にのみコピー元を
   削除するので、失敗したドライブをまたぐMoveで元のファイルを
   失うことは決して無い。 */
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

  if (path_eq(srcPath, dstPath)) return 1; /* 既にそこにある。何もすることは無い */

  if (file_exists(dstPath)) {
    p = 0;
    sappend(msg, &p, MSG(MSG_OVERWRITE_PRE), sizeof(msg));
    sappend(msg, &p, name, sizeof(msg));
    sappend(msg, &p, MSG(MSG_OVERWRITE_SUF), sizeof(msg));
    draw_dialog(msg, 0);
    key = dos_getch();
    if (key == KEY_ESC) return -2;
    if (key != 'y' && key != 'Y') return 0;
    /* AH=56h（リネーム）は移動先の名前が既に存在すると失敗するので、
       同一ドライブでの上書きが確認された場合、まずそれを消して
       おく必要がある。ドライブをまたぐ経路ではこれは不要
       ――copy_one_file()内のdos_create()が既存ファイルを
       自力で上書きする。 */
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

/* C/c：マークされたファイル、あるいは（何もマークされて
   いなければ）カーソル位置の1件をコピーする――do_delete()と同じ
   ターゲットルール：「マークが空でなければマーク集合、空なら
   カーソル位置のエントリ」であり、食い違いうる2つのケースでは
   ない。ディレクトリは決してマーク集合に入らない
   （mark_cursor()が拒否する）ので、ディレクトリが「対象」に
   なりうるのはマーク無し・カーソルがディレクトリ上にあるケース
   だけであり、do_delete()と同様、それは事前にとらえて明示的な
   エラーダイアログで拒否する。
   確認なしで一律に同名コピーを拒否する本家のファイラとは異なり
   （実測。READMEの独立性に関する注記参照）、これは衝突する
   ファイルごとにY/N/ESCの上書き確認を行う――黙って上書きする
   ことも黙ってスキップすることも、尋ねることよりは悪い選択で
   ある。成功時には完了ダイアログを表示する（これもDeleteとは
   異なる。Deleteは何も表示しない――それぞれのコマンドについて
   本家の挙動に合わせている）。 */
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

/* M/m：マークされたファイル、あるいは（何もマークされて
   いなければ）カーソル位置の1件を移動する――do_copy()（その
   冒頭コメント参照）と同じターゲットルール、同じ事前の
   ディレクトリ拒否、同じファイルごとのY/N/ESC上書き確認、
   同じ完了ダイアログ。唯一の違いは
   move_confirm_and_move()の同一ドライブリネームかドライブを
   またぐコピー＋削除かという選択である。タイムスタンプは
   常に元のファイルのものであり、「今」にはならない――同じ
   ドライブではAH=56hのその場リネームの結果として自然にそうなり、
   ドライブをまたぐ場合はcopy_one_file()が明示的に引き継いで
   いる。 */
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

/* ---- 画面描画 ------------------------------------------------------------ */

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
  /* 既定では'.'を付けない。実際に拡張子があるときだけ表示する */

  /* マークの印：0桁目に'*'。色や反転無しの普通の文字（本家の
     マーク表示も普通の文字であることをピクセル単位の実測で
     確認済み）。ディレクトリは決してマークされないので、
     この分岐がディレクトリ行で発火することは決して無い。 */
  if (g_marked[idx]) buf[0] = '*';

  rawname = &g_name[idx * NAME_LEN];

  /* "."と".."は8.3の名前＋拡張子のペアではない――これらを'.'で
     分割すると、空の名前と拡張子"."になってしまい、".."が
     10-11桁目（拡張子欄）に来て名前欄が空白になる。本家はこれらを
     分割せずに名前欄にそのまま表示するので、通常のドット検索の
     前にここで特別扱いしている。 */
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

  /* ファイル一覧のサイズ欄：本家では','区切りを入れない。カンマ
     区切りになっているのはヘッダの合計欄（draw_disk_line）だけ */
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

/* ディスクの合計／使用／空きの行（ヘッダボックスのROW_DISK行）を
   box_row()用のプレーンなバッファへ組み立てる。ファイル一覧の
   サイズとは異なりカンマ区切り――これはヘッダの合計行の一つ
   だから。 */
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
  int labelCells;

  drive = dos_getdrive();
  secPerClus = dos_diskfree(drive + 1, &availClus, &bytesPerSec, &totalClus);

  p = 0;
  if (secPerClus == 0xFFFF) {
    sappend(row, &p, MSG(MSG_DISK_UNAVAIL), sizeof(row));
    box_row(ROW_DISK, BOXCH_V, BOXCH_V, row, ATTR_LABEL);
    return; /* フィールドが無いので区切り線も無い――行は単なる平文のまま */
  }

  cLo = umul32(secPerClus, bytesPerSec, &cHi);            /* クラスタあたりのバイト数 */
  totLo = mul32x16(cHi, cLo, totalClus, &totHi);          /* 合計バイト数   */
  freeLo = mul32x16(cHi, cLo, availClus, &freeHi);        /* 空きバイト数    */
  sub32(totHi, totLo, freeHi, freeLo, &usedHi, &usedLo);  /* 使用バイト数    */

  format_u32(totHi, totLo, totalBuf);
  format_u32(usedHi, usedLo, usedBuf);
  format_u32(freeHi, freeLo, freeBuf);

  /* 右詰め、単位サフィックス無し――本家自身のディスク合計行を
     実測したもの（上のDISK_SEP_COL1/DISK_SEP_COL2のコメントと
     sappend_field_rj()参照）。各フィールドはそれぞれ導出された
     幅を使うので、3列は3列目だけ空白に流れ込むのではなく
     ほぼ等しい幅になる。 */
  sappend_field_rj(row, &p, MSG(MSG_DISK_TOTAL), totalBuf, DISK_FIELD1_WIDTH, sizeof(row));
  sappend(row, &p, " ", sizeof(row)); /* 区切り線のセル。下で上書きされる */

  sappend_field_rj(row, &p, MSG(MSG_DISK_USED), usedBuf, DISK_FIELD2_WIDTH, sizeof(row));
  sappend(row, &p, " ", sizeof(row)); /* 区切り線のセル。下で上書きされる */

  sappend_field_rj(row, &p, MSG(MSG_DISK_FREE), freeBuf, DISK_FIELD3_WIDTH, sizeof(row));

  /* まず行全体を白（値）で塗り、その後各フィールド自身のラベルを
     シアンで上塗りする（実測。docs/filer-measure-06.md参照）――
     sappend_field_rj()が数値の前にラベルを置くので、ラベルは
     常にそのフィールドの最初の桁から始まり、上で既に定義した
     フィールド境界以上の位置追跡は不要。 */
  box_row(ROW_DISK, BOXCH_V, BOXCH_V, row, ATTR_VALUE);

  labelCells = text_width(MSG(MSG_DISK_TOTAL));
  vram_puts_cells(ROW_DISK, 1, MSG(MSG_DISK_TOTAL), ATTR_LABEL, labelCells);
  labelCells = text_width(MSG(MSG_DISK_USED));
  vram_puts_cells(ROW_DISK, DISK_SEP_COL1 + 1, MSG(MSG_DISK_USED), ATTR_LABEL, labelCells);
  labelCells = text_width(MSG(MSG_DISK_FREE));
  vram_puts_cells(ROW_DISK, DISK_SEP_COL2 + 1, MSG(MSG_DISK_FREE), ATTR_LABEL, labelCells);

  /* 0x96自体がSJIS先頭バイトの範囲に入っているため、'row'に
     埋め込んでvram_puts_cells()へ渡すことができない（ファイル
     冒頭のVRAMコメント参照）――このファイルの他所のBOXCH_*の
     角／枠文字と同様、代わりに後から直接書き込んでいる。 */
  vram_ank(ROW_DISK, DISK_SEP_COL1, BOXCH_V, ATTR_BORDER);
  vram_ank(ROW_DISK, DISK_SEP_COL2, BOXCH_V, ATTR_BORDER);
}

/* カレントパスの行（ヘッダボックスのROW_PATH行）をbox_row()用の
   プレーンなバッファへ組み立てる。draw_screen()から切り出した
   もので、ヘッダボックスの内容組み立てをすべて「文字列を組み立て、
   その後box_row()」という同じ形に揃えるため。 */
void draw_path_line(void)
{
  char row[256];
  int p;
  int pathLabelCells;
  int markedLabelStart;
  int markedLabelCells;

  p = 0;
  sappend(row, &p, MSG(MSG_PATH_PREFIX), sizeof(row));
  pathLabelCells = text_width(row); /* "Path=" 単体は常に1桁目から始まる */
  sappend(row, &p, g_path, sizeof(row));
  if (g_truncated) {
    sappend(row, &p, MSG(MSG_TRUNC_PREFIX), sizeof(row));
    sappend_uint(row, &p, (unsigned int)MAX_ENTRIES, sizeof(row));
    sappend(row, &p, MSG(MSG_TRUNC_SUFFIX), sizeof(row));
  }
  sappend(row, &p, "  ", sizeof(row));
  markedLabelStart = text_width(row);
  sappend(row, &p, MSG(MSG_MARKED_LABEL), sizeof(row));
  markedLabelCells = text_width(row) - markedLabelStart;
  sappend_uint(row, &p, (unsigned int)count_marked(), sizeof(row));

  /* まず行全体を白（値）で塗り、その後"Path="/"Marked:"の
     ラベルをシアンで上塗りする――'row'を組み立てている最中に
     文字ごとの属性を追跡するのではなく、こうして2回目に狙い
     撃ちで塗る理由はdraw_disk_line()のコメント参照。 */
  box_row(ROW_PATH, BOXCH_V, BOXCH_V, row, ATTR_VALUE);
  vram_puts_cells(ROW_PATH, 1, MSG(MSG_PATH_PREFIX), ATTR_LABEL, pathLabelCells);
  vram_puts_cells(ROW_PATH, 1 + markedLabelStart, MSG(MSG_MARKED_LABEL), ATTR_LABEL, markedLabelCells);
}

/* 選択中エントリの情報行（ヘッダボックスのROW_INFO行）を
   組み立てる。draw_disk_line()と同様カンマ区切りのサイズを
   保つ――これはファイル一覧のグリッドではなく同じヘッダ
   ボックスの一部だから。 */
void draw_info_line(int visibleCount)
{
  char entrybuf[40];
  char attrbuf[8];
  char sizebuf[16];
  char datebuf[9];
  char timebuf[6];
  char row[128];
  int p;
  int infoLabelCells;
  int attrLabelStart;
  int attrLabelCells;

  p = 0;
  if (visibleCount == 0) {
    sappend(row, &p, MSG(MSG_INFO_PREFIX), sizeof(row));
    infoLabelCells = text_width(row);
    sappend(row, &p, MSG(MSG_INFO_EMPTY), sizeof(row));
    box_row(ROW_INFO, BOXCH_V, BOXCH_V, row, ATTR_VALUE);
    vram_puts_cells(ROW_INFO, 1, MSG(MSG_INFO_PREFIX), ATTR_LABEL, infoLabelCells);
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
  infoLabelCells = text_width(row);
  sappend(row, &p, &g_name[g_cursor * NAME_LEN], sizeof(row));
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, sizebuf, sizeof(row));
  /* "<DIR>"はバイト数ではないので、bytesサフィックスのラベル
     （"bytes"/"バイト"）は実際のファイルのときだけ追記する――
     ファイル一覧のサイズ欄（build_entry_text）が"<DIR>"の隣に
     単位を一切表示しないのと同じ扱い。 */
  if (!(g_attr[g_cursor] & ATTR_DIR)) {
    sappend(row, &p, MSG(MSG_BYTES_SUFFIX), sizeof(row));
  }
  sappend(row, &p, "  ", sizeof(row));
  sappend(row, &p, datebuf, sizeof(row));
  sappend(row, &p, " ", sizeof(row));
  sappend(row, &p, timebuf, sizeof(row));
  sappend(row, &p, "  ", sizeof(row));
  attrLabelStart = text_width(row);
  sappend(row, &p, MSG(MSG_ATTR_LABEL), sizeof(row));
  attrLabelCells = text_width(row) - attrLabelStart;
  sappend(row, &p, attrbuf, sizeof(row));

  /* まず行全体を白（値）で塗り、その後"Info:"/"Attr:"の
     ラベルをシアンで上塗りする――上のdraw_disk_line()/
     draw_path_line()と同じ2パス方式。 */
  box_row(ROW_INFO, BOXCH_V, BOXCH_V, row, ATTR_VALUE);
  vram_puts_cells(ROW_INFO, 1, MSG(MSG_INFO_PREFIX), ATTR_LABEL, infoLabelCells);
  vram_puts_cells(ROW_INFO, 1 + attrLabelStart, MSG(MSG_ATTR_LABEL), ATTR_LABEL, attrLabelCells);
}

/* 最下段（24行目）を、実測したPC-98のファンクションキー割り当てと
   して描画する：まず行全体をATTR_BASEで塗りつぶす（これが
   フィールド間の1セルの隙間、F5/F6間の4セルの隙間、予約済み
   （未実装）のフィールドで結局表示されるものになる――
   docs/filer-measure-05.mdの0x91「フィールドの外」の実測参照）。
   その後、空でない各g_fkeyLabel[i]をg_fkeyCol[i]の位置に、
   FKEY_FIELD_WIDTHセルの中でおおよそ中央寄せして配置する
   （ラベルの長さが奇数のときは左寄りになる、整数除算と同じ――
   本家自身の中央寄せも厳密ではない）。ラベルの各セルは、
   g_fkeyHiPos[i]の1文字だけを除いてATTR_FKEY_LABEL（反転）を
   使い、その1文字だけはATTR_FKEY_KEY（同じく反転だが色が異なる）
   を使う――「ラベル全体を反転し、キー文字だけ色を変える」という
   実測した慣習に合わせたもので、第4/5マイルストーンの「キー文字
   だけ反転」という方式ではない。ラベルは普通のASCII（g_fkeyLabel[]
   参照）なので、これはSJISを意識したセル数ではなくバイト単位で
   インデックスしている。check.pyは、すべてのg_fkeyLabel[]の
   要素がFKEY_FIELD_WIDTHセルに収まること、すべての
   g_fkeyCol[]/FKEY_FIELD_WIDTHのフィールドがVRAM_COLS内に
   収まることを検証する。 */
void draw_cmdline(void)
{
  int i;
  int col;
  int len;
  int pad;
  int c;
  char *label;
  unsigned char ch;
  unsigned int attr;

  for (col = 0; col < VRAM_COLS; col++) {
    vram_ank(ROW_CMD, col, ' ', ATTR_BASE);
  }

  for (i = 0; i < FKEY_COUNT; i++) {
    label = g_fkeyLabel[i];
    len = (int)strlen(label);
    if (len == 0) continue; /* 予約済み（まだ未実装）：空白のままにする */
    pad = (FKEY_FIELD_WIDTH - len) / 2;
    if (pad < 0) pad = 0;
    /* FKEY_FIELD_WIDTHセルのフィールド全体が反転する。ラベル
       文字自身だけでなく、その周りの空白パディングセルも含めて
       ――実機に対する実測ではフィールドごとに6セル反転しており、
       ラベル文字だけではない（docs/filer-measure-05.md参照）。 */
    for (c = 0; c < FKEY_FIELD_WIDTH; c++) {
      if (c >= pad && c < pad + len) {
        ch = (unsigned char)label[c - pad];
        attr = ((c - pad) == g_fkeyHiPos[i]) ? ATTR_FKEY_KEY : ATTR_FKEY_LABEL;
      } else {
        ch = ' ';
        attr = ATTR_FKEY_LABEL;
      }
      vram_ank(ROW_CMD, g_fkeyCol[i] + c, ch, attr);
    }
  }
}


/* エントリの種類ごとのファイル一覧行の色（実測。
   docs/filer-measure-06.md参照）：ディレクトリはシアン、通常
   ファイルは黄色、システムまたは隠し属性のファイルはマゼンタ。
   カーソル行はこの同じ色を保ち、反転ビットを足すだけ――同じ
   ファイルについて実測したカーソル行／非カーソル行の属性
   バイト（例えば0x65と0x61）はちょうどそのビットだけ異なり、
   色は決して変わらない。したがって、旧来の「カーソル＝黄色／
   それ以外＝白」という方式のように、カーソル行に別の色を
   代わりに使うことは決してあってはならない。 */
unsigned int entry_attr(int idx, int isCursor)
{
  unsigned int base;

  if (g_attr[idx] & ATTR_DIR) {
    base = ATTR_LIST_DIR;
  } else if (g_attr[idx] & (ATTR_SYSTEM | ATTR_HIDDEN)) {
    base = ATTR_LIST_SYS;
  } else {
    base = ATTR_LIST_FILE;
  }
  if (isCursor) base |= ATTR_CURSOR_BIT;
  return base;
}

void draw_screen_frame(void)
{
  int leftCount;
  int rightCount;
  int visibleCount;
  int row;
  int leftIdx;
  int rightIdx;
  int col;
  char entrybuf[40];

  visibleCount = g_count;
  if (visibleCount > VISIBLE_MAX) visibleCount = VISIBLE_MAX;
  leftCount = (visibleCount > LEFT_ROWS) ? LEFT_ROWS : visibleCount;
  rightCount = visibleCount - leftCount;

  /* ここではESC[2Jのような画面全体のクリアは行わない（上の
     vram_*節参照）：その代わり、この関数自身が毎回25×80セル
     全部を必ず再訪する――ヘッダボックス（0-5行目、罫線含む）、
     一覧の2列（6-22行目、隙間の39桁目と最終列の79桁目も含む）、
     23行目（ROW_GAP）、コマンド行（24行目）。内蔵ビューアは
     0-79桁・2-23行目を使うので、ファイラの描画がこの全体を
     覆っていないと、ビューアからESCで戻ったときにその内容が
     ファイラの描画が再訪しないセルに残ってしまう（実機で確認
     済み）。個々の値についてはvram_ank()/vram_puts_cells()が
     vram_set_cell()経由で「変化があったセルだけ実際に書く」
     ため、全セルを対象にしてもちらつきや速度の問題にはならない。
     ヘッダボックス（0-5行目）：半角罫線の枠。 */
  draw_title_row();

  draw_disk_line();

  box_dash_row(ROW_SEP1, BOXCH_LT, BOXCH_RT);
  vram_ank(ROW_SEP1, DISK_SEP_COL1, BOXCH_HJ, ATTR_BORDER); /* ディスク行の区切り線の
     下――上のDISK_SEP_COL1/2のコメント参照 */
  vram_ank(ROW_SEP1, DISK_SEP_COL2, BOXCH_HJ, ATTR_BORDER);

  draw_path_line();

  draw_info_line(visibleCount);

  box_dash_row(ROW_SEP2, BOXCH_BL, BOXCH_BR);

  for (row = 0; row < LEFT_ROWS; row++) {
    leftIdx = row;
    rightIdx = LEFT_ROWS + row;

    if (leftIdx < leftCount) {
      build_entry_text(leftIdx, entrybuf);
      vram_puts_cells(ROW_LIST_TOP + row, COL_LEFT, entrybuf,
                       entry_attr(leftIdx, leftIdx == g_cursor), 39);
    } else {
      /* ここにはもう何も無い――明示的に空白にする。縮小した
         ディレクトリ一覧（Deleteの後、あるいはより小さな
         ディレクトリへ移動した後）では、前のフレームの行が
         ここに残ったままになってはならない。毎フレームの
         ESC[2Jがそれをタダでやってくれる時代はもう終わった
         ため。 */
      vram_puts_cells(ROW_LIST_TOP + row, COL_LEFT, "", ATTR_BASE, 39);
    }
    if (rightIdx < visibleCount) {
      build_entry_text(rightIdx, entrybuf);
      vram_puts_cells(ROW_LIST_TOP + row, COL_RIGHT, entrybuf,
                       entry_attr(rightIdx, rightIdx == g_cursor), 39);
    } else {
      vram_puts_cells(ROW_LIST_TOP + row, COL_RIGHT, "", ATTR_BASE, 39);
    }

    /* 39桁目（左右2列の隙間）と79桁目（画面の最終列）は、上の
       どちらのvram_puts_cells()呼び出し（幅39セル）も届かない。
       内蔵ビューアは0～79桁を丸ごと使うので、ここを塗らずに
       放置するとESCで一覧へ戻ったときにビューアの残骸がここに
       残る（実機で確認済み）。方針A：「戻ってくる経路」ごとに
       個別対策を積み増すのではなく、ファイラの通常描画自体が
       毎フレーム25×80セル全部を必ず覆うようにして、根本を断つ。
       vram_ank()はvram_set_cell()経由なので、値が変わらない
       限り実際の書き込みは起きず、差分更新の性質は保たれる。 */
    vram_ank(ROW_LIST_TOP + row, COL_GAP, ' ', ATTR_BASE);
    vram_ank(ROW_LIST_TOP + row, COL_LAST, ' ', ATTR_BASE);
  }

  /* 23行目（ROW_GAP）：一覧（6～22行目）とファンクションキー行
     （24行目）の間にあり、どちらの通常描画も一度も触れない。
     同じ理由でここも明示的に空白にする。 */
  for (col = 0; col < VRAM_COLS; col++) {
    vram_ank(ROW_GAP, col, ' ', ATTR_BASE);
  }

  draw_cmdline();
}

void draw_screen(void)
{
  draw_screen_frame();
}

/* ---- 入力／カーソル移動 -------------------------------------------------- */

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
    /* 左：既に左の列にいるので何もしない */
  } else {
    row = g_cursor - leftCount;
    if (dir == DIR_UP) { if (row > 0) g_cursor = leftCount + row - 1; }
    else if (dir == DIR_DOWN) { if (row < rightCount - 1) g_cursor = leftCount + row + 1; }
    else if (dir == DIR_LEFT) { g_cursor = row; }
    /* 右：既に右の列にいるので何もしない */
  }
}

/* ---- コマンドラインスイッチ ----------------------------------------------- */

void parse_args(int argc, char *argv[])
{
  int i;

  g_lang = LANG_JA; /* docs/i18n-design.mdに従い既定値 */
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
    write_str("SUMIRE: message table error (JA/EN mismatch)\r\n");
    return 1;
  }

  parse_args(argc, argv);

  write_str("\x1b[>1h"); /* 最下段のファンクションキー行を解放する */
  write_str("\x1b[>5h"); /* テキストカーソルを隠す */

  g_viewHandle = -1; /* 第8マイルストーン：まだビューアのファイルは開いていない */

  vram_shadow_init();
  vram_clear_all(); /* ブート／DOSプロンプトが残していたものを一度だけ
                        クリアする。これ以降の再描画はすべて実際に
                        変化したセルだけに触れる――上のvram_*節参照。 */

  read_path();
  read_dir();
  g_cursor = 0;
  draw_screen();

  running = 1;
  while (running) {
    /* dos_getch()（INT 18h AH=00h）はキーが押されるまでブロック
       するので、アイドル中は0行目の時計が止まってしまう。代わりに
       dos_kbhit()（INT 18h AH=01h）をポーリングし、ポーリングの
       合間にタイトル行（アイドル中に変化する唯一のもの）を
       再描画する。vram_set_cell()の「変化があったときだけ書く」
       性質により、何も変化していないと分かった再描画は
       ハードウェアに一切触れない。IDLE_POLL_SPINは、AH=01h呼び出し
       の間の単純なビジーウェイトである――リアルモードDOSには、
       このプログラムが他で既に使っているような「キー入力かNティック
       のどちらか早い方まで待つ」というブロッキングのプリミティブが
       無く、固定回数のスピンで、INT 18hを全力で呼び続けることを
       防ぎつつ、時計を1秒以内に十分収まる頻度で更新するには
       これで足りる。#define参照。 */
    while (!dos_kbhit()) {
      int spin;
      draw_title_row();
      for (spin = 0; spin < IDLE_POLL_SPIN; spin++) { }
    }
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
    } else if (key == 'x' || key == 'X') {
      do_exec();
    } else if (key == 'l' || key == 'L') {
      do_logdsk();
    } else if (key == 'q' || key == 'Q') {
      running = 0;
    } else if (key == KEY_F1) {
      do_logdsk();
    } else if (key == KEY_F2) {
      do_exec();
    } else if (key == KEY_F3) {
      do_copy();
    } else if (key == KEY_F4) {
      do_delete();
    } else if (key == KEY_F5) {
      do_rename();
    } else if (key == KEY_F6) {
      do_move();
    } else if (key == KEY_F7) {
      do_mkdir();
    } else if (key == KEY_F10) {
      running = 0;
      /* F8/F9（およびそれ以外すべて）：まだコマンドが割り当てられて
         いない――g_fkeyLabel[]の予約済み（""）エントリ参照――ので
         無視する。F2は上で処理済み（do_exec()）。第9マイルストーン：
         INT 18hはファンクションキーをそれぞれ独立したスキャンコード
         として報告してくるので、削除済みの第6マイルストーンの
         DOSコンソール方式とは異なり、素のESCキー押下とファンクション
         キーを区別するためにESCの後でdos_kbhit()を先読みする必要が
         ない。 */
    } else if (key == KEY_ESC) {
      running = 0; /* 単独のESC：Qと同じく終了 */
    }
  }

  /* DOSがまた使える状態に画面を戻す：このプログラムの他の
     すべてが使っているのと同じ直接VRAM書き込みで画面をクリアし
     （ESC[2J/ANSI.SYSは使わない――このプログラムはDOSコンソール
     自身の「現在の色」という概念を一度も設定していないので、
     「現在の属性で」クリアするようそれに頼るのは、ここで何も
     設定していない状態に頼ることになってしまう）、カーソルを
     表示する前にホーム位置へ戻す。 */
  vram_clear_all();
  ansi_goto(0, 0);
  write_str("\x1b[>5l"); /* テキストカーソルを再び表示する */
  write_str("\x1b[>1l"); /* ファンクションキー行を復元する */

  return 0;
}
