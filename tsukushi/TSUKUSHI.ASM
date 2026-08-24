; TSUKUSHI.COM - PC-98 / MS-DOS 用 常駐ローマ字かな漢字変換 FEP (段階B: 辞書あり)
;
; 目的:
;   INT 18h(キーボードBIOS) の AH=00h(キー入力待ち)/AH=01h(キーセンス) を
;   フックし、ハンドラ内で完結するローマ字->ひらがな->かな漢字変換を行う。
;
;   guest/docs/fep-measure-05-int18.md の実測により、FreeCOM(FreeDOS)も
;   COMMAND.COM(NEC MS-DOS)も最終的に INT 18h AH=00h でキーを読んでいる
;   ことが分かっている。INT 18h をフックすれば入力の窓口を1か所に
;   集約できる。
;
;   段階A = 辞書引きなし。ローマ字->ひらがな変換のロジック・かなバッファ・
;   BS処理は FEP.ASM(INT 21h版 v1)をそのまま流用する。ただし FEP.ASM の
;   lbuf/try_append(行バッファへの確定+エコー)は使わない。かなが確定したら
;   このハンドラの外(呼び出し元)へ返すための outq(確定済みバイトの待ち行列)
;   へ積む。
;
;   段階B = かな漢字変換を追加する。INT 18h ハンドラの中から辞書ファイルを
;   DOS 経由(INT 21h)で開くことはできない(下記の絶対厳守事項)。当初は
;   辞書をインストール時に丸ごとメモリへ読み込んでいたが、実辞書
;   (SKK-JISYO.L 由来)は数百KB〜数MBあり常駐には載らない。
;
;   段階C(現在) = guest/docs/fep-measure-06-int1b.md の実測により、
;   INT 18h ハンドラの中からでも INT 1Bh(ディスクBIOS生セクタ読み込み)
;   なら呼べることが分かった。そこで辞書は**ディスク上に置いたまま**にし、
;   変換のたびに必要なセクタだけ INT 1Bh で読む。辞書サイズの上限は
;   事実上なくなり、常駐に載せる必要も無い。
;
;   辞書の位置解決(TSUKUSHI.DIC がディスクのどこにあるか)は DICLOC.ASM で
;   確立した手順(BPB・FAT・ルートディレクトリを読んでFAT12クラスタ
;   チェーンを辿る)と同じ考え方をインストール時に行う(DICLOC.ASM
;   自体は変更しない。同じ手順を本ファイル内に書く)。断片化した辞書
;   ファイルにも対応する(段階E)。連続する区間(最大32個)のリストを
;   常駐させ、区間数がそれを超える場合だけ常駐せずに終了する。
;
;   辞書形式は v2(疎索引。tools/mkdic2.py が生成する FEPDIC02)。
;   検索は tools/dic2_ref.py の docstring の手順(疎索引を二分探索して
;   ブロックを絞り、ブロック内を最大 SPARSE 件だけ線形走査する)を
;   そのままディスクI/O版にしたもの。
;
;   確定先が lbuf ではなく outq である点は段階Bと同じ
;   (FEP.ASM(INT 21h版 v1)の cand_*/confirm_candidate の考え方を踏襲)。
;
;   *** 絶対厳守: INT 18h のハンドラの中から INT 21h を呼んではいけない ***
;   INT 18h は DOS の CON ドライバから呼ばれることがあるため、ハンドラは
;   「DOS の中」で走っている可能性がある。DOS を再入させると壊れる。
;   画面出力もファイル入出力も DOS 経由(INT 21h)は禁止。
;   -> ステータス行はテキスト VRAM (A000:0000 文字 / A000:2000 属性) への
;      直書きで描く。書籍 2-6-1 の内容に基づく(本ソースでの実装は未検証、
;      報告に明記する)。
;
;   *** 常駐サイズは resident_end: から計算する ***
;   install: より後ろに、常駐後に参照するデータを一切置かない
;   (FEP.ASM でテーブルが常駐範囲外になるバグを出した実績があるため)。
;
;   ハイレゾ(A000:0000より上位に切り替わるモード)は対象外。ノーマルモード
;   (640x400)のみ対応する。
;
; 多重常駐チェック: INT 18h を AX=E7ECh で呼ぶと AX=4638h('F8') を返す。
;
; ビルド: nasm -f bin TSUKUSHI.ASM -o TSUKUSHI.COM

        cpu     8086
        org     0x100

IDENT_FUNC      equ     0xE7EC
IDENT_REPLY     equ     0x4638

ROMABUFSIZE     equ     8
KANABUFSIZE     equ     64
OUTQSIZE        equ     64
STATUSCOLS      equ     80             ; ステータス行の埋め幅(桁=画面上のカラム数)
BODYCOLS        equ     64             ; 本文(prefix+かな/候補)を打ち切る桁数。
                                        ; 桁64は区切り空白、桁65-79は製品名ラベル
MSG_LABEL_LEN   equ     15             ; "FEP by Tsukushi" の桁数(桁65-79)
CANDBUFSIZE     equ     128
CANDMAX         equ     16

; --- キーボードBIOSの作業領域 (物理アドレス。セグメント0000h) ---
; CON ドライバは INT 18h AH=00h を呼ぶ前に 0x528(件数) を見て「入力が
; あるか」を判断している。TSUKUSHI の outq は自前バッファなので 0x528 が
; 増えず、CON が待ち続けてしまう(実測で確定)。確定したバイトはここへ
; 直接積む。レイアウトは NP2kai sdl/webnp2api.c の
; webnp2_push_key_buffer() と同一 (bios/bios09.c 準拠)。
KBSEG           equ     0x0000
KBBUF_START     equ     0x502
KBBUF_END       equ     0x522          ; 0x502-0x521 が実データ(排他上限)
KBCOUNT         equ     0x528
KBTAIL          equ     0x526

; TSUKUSHI が積んだバイトの印。AH=00h ハンドラが oldint18 から読み戻した
; ときにこのキーコードを見て「自分が積んだものだから再変換しない」と
; 判定する。0xFF は実測した実キー(0x00-0x7F、修飾付きで0xA5/0xB1/0xB5)
; とは衝突しないが、これはあくまで実測範囲内での仮定であり未証明。
INJECT_KEYCODE  equ     0xFF

TEXT_BASE       equ     0x0000         ; A000:0000 文字エリア先頭 (セグメントは別途 ES=0xA000)
ATTR_BASE       equ     0x2000         ; A000:2000 属性エリア先頭
LINE_BYTES      equ     160            ; 1行 = 160バイト
STATUS_ROW      equ     24             ; Y=24 (25行目、0起点)
STATUS_OFF      equ     (STATUS_ROW * LINE_BYTES)      ; = 3840
REVERSE_ATTR    equ     0xE5           ; 白(G/R/B=1)+リバース+表示。実測で確定。
                                       ; 0x05 だと色ビットが全部0で黒になり、画面に
                                       ; 何も見えない(TVRAMにはデータが入っているのに)。
                                       ; DOSの通常表示は 0xE1(白)。書籍2-6-1の図2-25より
                                       ; D7=G D6=R D5=B / D2=RV / D0=ST(1で表示)

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint18:       dd      0

mode:           db      0       ; 0=直接入力, 1=かな入力

; カタカナ変換(F7でカタカナ、F6でひらがなに戻す)。kanabuf 自体は書き換えず、
; 表示・確定の直前だけ変換する(打ち足し・BS では維持する)。
kata_flag:      db      0       ; 0=ひらがな表示, 1=カタカナ表示

romabuf:        times   ROMABUFSIZE db 0
romalen:        db      0

kanabuf:        times   KANABUFSIZE db 0
kanawid:        times   KANABUFSIZE db 0
kanalen:        db      0       ; kanabuf の使用バイト数
kanacnt:        db      0       ; 文字数 (kanawid の使用個数)

; kana_append の作業用スクラッチ
ka_count:       db      0
ka_wcount:      db      0
ka_wval:        db      0

scratchch:      db      0       ; kana_append_ascii が1バイトを指すために使う

; 「ん」「っ」の Shift_JIS バイト列 (テーブル検索を経ない特殊確定用)
sound_n:        db      0x82, 0xF1      ; "ん"
sound_sokuon:   db      0x82, 0xC1      ; "っ"

; --- 段階C: 辞書はディスク上(TSUKUSHI.DIC)に置いたまま。常駐データには
; ディスク上の位置・BPB情報・1〜2セクタ分の読み込みバッファだけを持つ。
; インストール時に DICLOC.ASM と同じ手順で解決する(下記 install: 参照。
; DICLOC.ASM 自体は変更しない)。

; --- 段階B/C共通: 変換候補の状態 (FEP.ASM v1 の cand_* と同一の考え方) ---
cand_count:     db      0       ; 0 = 変換していない
cand_index:     db      0       ; 0起点
cand_buf:       times   CANDBUFSIZE db 0
cand_off:       times   CANDMAX dw 0    ; 各候補の cand_buf 内オフセット
cand_len:       times   CANDMAX db 0    ; 各候補のバイト数

; dict_lookup(v2, ディスク版)の作業用スクラッチ
cand_read_i:    db      0
cand_cur_len:   db      0
cb_pos:         dw      0

;--------------------------------------------------------------------
; 段階C: 辞書のディスク上の位置・アクセス情報 (インストール時に解決)
;--------------------------------------------------------------------
DIC_READBUF_SIZE equ    2048    ; 2セクタ分。bps<=1024を前提とする
                                 ; (install時に 2*bps がこれを超えないか検査する)

dic_data_off:       dd   0      ; ヘッダ+0x14: データ部先頭(ファイル先頭からの相対バイト)
dic_data_end:       dd   0      ; data_off + data_len (データ部終端。同上)
dic_sparse_off:     dd   0      ; ヘッダ+0x10: 疎索引先頭(ファイル先頭からの相対バイト)
dic_sparse_count:   dw   0      ; ヘッダ+0x0C下位ワード: 疎索引レコード数(65536件以上は対象外)
dic_sparse:         dw   0      ; ヘッダ+0x1C: 疎索引の間隔(ブロックあたりの最大エントリ数)

; --- 段階D: 辞書はFDでもHDDでもディスク上に置ける。インストール時に
; 5通りの読み出し方式を順に試し、実際に読めた方式をここへ控える。
; dic_kind = 0: FD(INT 1Bh CHS。MF/f0/SEEK1固定=AH 0x76)
;            1: HDD SASI相対アドレス指定(AH 0x06、DA/UA=0x00〜0x03)
dic_kind:           db   0
; --- 段階E: 断片化した辞書に対応するため、単一の dic_start_lba ではなく
; 「連続する区間」のリストを持つ。ほとんどの断片化は少数の連続区間に
; 分かれるだけなので、区間を最大 MAXRUNS(=32) 個まで常駐させる。
; 各区間は { 開始セクタ(dic_run_start), セクタ数(dic_run_sectors) } の組。
; 単位は dic_kind によって違う(旧 dic_start_lba と同じ考え方):
;   FD  : BPBの論理セクタ番号(BPBのbps単位)。従来どおり0〜65535に収まる前提。
;   HDD : ディスク先頭からの物理256バイトセクタの線形番号。
;         hidden(BPBのhidden sectors、物理セクタ単位) + 論理セクタ番号×(BPBのbps/256)。
;         実測した33MB級HDDイメージで既に65536を超えるため32bitにした。
; 区間はファイル内の出現順(FATチェーンを辿った順)に並んでいる。
; 連続配置のファイルは区間が1個になるだけで、従来の動作と同じになる。
MAXRUNS             equ  32
dic_nruns:          db   0
dic_run_start:      times MAXRUNS dd 0
dic_run_sectors:    times MAXRUNS dd 0
dic_da_ua:          db   0      ; INT 1Bh の DA/UA。FDなら 0x90+ドライブ番号、
                                 ; HDD(SASI相対)なら 0x00〜0x03(候補ごと)
dic_bps:            dw   0      ; 転送に使う物理セクタ長。FDはBPBのbps、HDDは256固定
dic_spt:            dw   0      ; BPBのセクタ/トラック(FD候補のCHS計算専用)
dic_heads:          dw   0      ; BPBのヘッド数(同上)
dic_sector_n:       db   0      ; FD専用: INT 1BhのCH用セクタ長コード(1024→3/512→2/256→1)

; 1〜2セクタ読み込みキャッシュ(直近に読んだセクタを覚えておき、同じ
; セクタへの再アクセスでは INT 1Bh を呼び直さない)。dic_start_lbaを32bit化
; したのに合わせて、キャッシュのタグとなる先頭セクタ番号(dic_buf_lba)と
; 対象セクタ番号(dic_target_lba)も32bitにした。
dic_buf_lba:        dd   0xFFFFFFFF ; ロード済みの先頭セクタ番号(0xFFFFFFFFは「未ロード」の印)
dic_buf_sectors:    db   0      ; ロード済みセクタ数(1 or 2)
dic_read_failed:    db   0      ; INT 1Bh が失敗したら1を立てる(以後notfound扱い)
dic_read_buf:       times DIC_READBUF_SIZE db 0

; dict_lookup(v2)本体の作業用スクラッチ
dic_lo:             dw   0
dic_hi:             dw   0
dic_mid:            dw   0
dic_best_block:     dw   0
dic_entry_off:      dd   0      ; エントリの絶対(ファイル先頭からの)バイトオフセット
dic_pos:            dd   0      ; 現在読んでいる位置(同上)
dic_block_end:      dd   0      ; 現在のブロックの終端位置(同上、排他上限)
dic_steps_left:     dw   0
dic_cand_count_off: dd   0      ; 現在のエントリの候補数バイトの絶対位置
dic_skip_cnt:       dw   0

; read_dict_range の作業用スクラッチ
dic_within0:        dw   0
dic_target_lba:     dd   0      ; dic_start_lbaと同じ単位の32bit
dic_need2:          db   0
dic_quot_tmp:       dw   0      ; オフセット/dic_bps の商(word)。32bit加算の一時置き場

; read_entry_at の結果置き場
dic_cur_yomi_len:   db   0      ; エントリの実際のよみバイト長
dic_cur_cmp_len:    db   0      ; 比較に使ったバイト数 = min(よみ長, kanalen)
dic_entry_yomi_buf: times 64 db 0 ; よみの比較用コピー(先頭 dic_cur_cmp_len バイトのみ有効)

; --- 確定済みバイトの待ち行列 (AH=00h/01h がここから返す) ---
outq:           times   OUTQSIZE db 0
outq_head:      db      0
outq_tail:      db      0
outq_count:     db      0

; --- ステータス行(25行目)の退避領域。文字160+属性160=320バイト。
; 直接入力中はFEPの情報が不要なので、かなモードに入る直前の内容を
; ここへ退避しておき、直接モードへ戻るときに書き戻す。
status_save:    times   320 db 0
status_col:     db      0       ; draw_status 作業用: 使用済み桁数
status_saved:   db      0       ; 1=status_saveに退避済み(かなモード中に
                                 ; 何度描いても自分のステータス行で退避
                                 ; 内容を汚さないためのフラグ)

;--------------------------------------------------------------------
; outq (確定済みバイトの待ち行列。円環バッファ)
;--------------------------------------------------------------------

; AL = 積むバイト。満杯なら黙って捨てる(他バッファと同じ流儀)。
outq_push:
        push    ax
        push    bx
        cmp     byte [cs:outq_count], OUTQSIZE
        jae     .full
        mov     bl, [cs:outq_tail]
        xor     bh, bh
        mov     [cs:scratchch], al
        mov     al, [cs:scratchch]
        mov     [cs:outq + bx], al
        inc     byte [cs:outq_tail]
        cmp     byte [cs:outq_tail], OUTQSIZE
        jne     .noWrapT
        mov     byte [cs:outq_tail], 0
.noWrapT:
        inc     byte [cs:outq_count]
.full:
        pop     bx
        pop     ax
        ret

; AL = 取り出したバイトを返す(outq_count>0であることは呼び出し側が保証する)。
outq_pop:
        push    bx
        mov     bl, [cs:outq_head]
        xor     bh, bh
        mov     al, [cs:outq + bx]
        inc     byte [cs:outq_head]
        cmp     byte [cs:outq_head], OUTQSIZE
        jne     .noWrapH
        mov     byte [cs:outq_head], 0
.noWrapH:
        dec     byte [cs:outq_count]
        pop     bx
        ret

; kanabuf(kanalen バイト)を先頭から outq へ積み、kanabuf を空にする。
; kata_flag=1 のときは kanawid で幅2(ひらがな候補)のグリフだけ
; kata_convert してから積む(kanabuf 自体は書き換えない)。
push_kanabuf_to_outq:
        push    ax
        push    bx
        push    cx
        push    si
        push    di
        cmp     byte [cs:kata_flag], 0
        jne     .kata
        mov     cl, [cs:kanalen]
        xor     ch, ch
        mov     si, kanabuf
.loop:
        jcxz    .after
        mov     al, [cs:si]
        call    outq_push
        inc     si
        loop    .loop
        jmp     .after
.kata:
        mov     si, kanabuf
        mov     di, kanawid
        xor     ch, ch
        mov     cl, [cs:kanacnt]
.kloop:
        jcxz    .after
        mov     al, [cs:di]
        cmp     al, 2
        je      .kwide
        mov     al, [cs:si]
        call    outq_push
        inc     si
        jmp     .knext
.kwide:
        mov     bl, [cs:si]
        mov     bh, [cs:si+1]
        call    kata_convert
        mov     al, bl
        call    outq_push
        mov     al, bh
        call    outq_push
        add     si, 2
.knext:
        inc     di
        loop    .kloop
.after:
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:kata_flag], 0
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret

; outq_pop の取り消し。AL = 戻すバイト。head を1つ戻して差し込み、
; count を+1する(popの逆操作。データの重複も欠落もしない)。
outq_unpop:
        push    bx
        cmp     byte [cs:outq_head], 0
        jne     .noWrapH
        mov     byte [cs:outq_head], OUTQSIZE
.noWrapH:
        dec     byte [cs:outq_head]
        mov     bl, [cs:outq_head]
        xor     bh, bh
        mov     [cs:outq + bx], al
        inc     byte [cs:outq_count]
        pop     bx
        ret

;--------------------------------------------------------------------
; キーボードBIOSリングバッファ(物理 0x502-0x528)への直接投入
;--------------------------------------------------------------------

; AL = 積むバイト(文字コード)。キーコードは INJECT_KEYCODE(0xFF)固定。
; 件数・head・tail はキーボード割り込みからも書かれるため cli/sti
; (pushf/popf で元の割り込み許可状態を保存・復元。勝手に sti しない)
; で保護する。
; 出力: CF=1で投入成功、CF=0でリングバッファが満杯(投入せず)。
; 破壊: 呼び出し規約上 AX 以外(BX,DX,ES,フラグ)を保存して復元する。
kb_ring_push:
        push    bx
        push    dx
        push    es
        mov     dl, al                  ; dl = 投入する文字コード
        pushf
        cli
        xor     bx, bx
        mov     es, bx                  ; es = 0000h (物理アドレス直参照)
        cmp     byte [es:KBCOUNT], 0x10
        jae     .full
        mov     bx, [es:KBTAIL]
        cmp     bx, KBBUF_START
        jae     .chk_hi
        mov     bx, KBBUF_START         ; 不正値の安全策(webnp2api.cと同じ)
        jmp     .store
.chk_hi:
        cmp     bx, KBBUF_END
        jb      .store
        mov     bx, KBBUF_START
.store:
        mov     byte [es:bx], dl        ; 下位バイト = 文字コード
        mov     byte [es:bx+1], INJECT_KEYCODE ; 上位バイト = キーコード(印)
        add     bx, 2
        cmp     bx, KBBUF_END
        jb      .nowrap
        mov     bx, KBBUF_START
.nowrap:
        mov     [es:KBTAIL], bx
        inc     byte [es:KBCOUNT]
        popf
        pop     es
        pop     dx
        pop     bx
        stc
        ret
.full:
        popf
        pop     es
        pop     dx
        pop     bx
        clc
        ret

; outq (自前の「まだリングバッファへ積めていない残り」置き場) から
; キーボードBIOSリングバッファへ、空きがある限り移す。
; AH=00h/AH=01h ハンドラの入口、および outq へ新規に積んだ直後に呼ぶ。
drain_outq_to_ring:
        push    ax
.loop:
        cmp     byte [cs:outq_count], 0
        je      .done
        call    outq_pop                ; al = 取り出したバイト
        call    kb_ring_push
        jc      .loop                   ; 積めた: 続けて次を試す
        call    outq_unpop              ; 満杯: 戻して打ち切る
.done:
        pop     ax
        ret

;--------------------------------------------------------------------
; Shift_JIS 先頭バイト幅判定 (FEP.ASM sjis_width と同じ)
; AL = バイト値。SJIS先頭バイト(0x81-0x9F または 0xE0-0xFC)なら AL=2,CF=1、
; そうでなければ AL=1,CF=0。
;--------------------------------------------------------------------
sjis_width:
        cmp     al, 0x81
        jb      .w1
        cmp     al, 0x9F
        jbe     .w2
        cmp     al, 0xE0
        jb      .w1
        cmp     al, 0xFC
        ja      .w1
.w2:
        mov     al, 2
        stc
        ret
.w1:
        mov     al, 1
        clc
        ret

;--------------------------------------------------------------------
; ひらがな -> カタカナ (Shift_JIS、実測で確認済み。詳細は README 参照)
; IN: BL=SJIS上位バイト(b1), BH=SJIS下位バイト(b2)
; OUT: BL/BH = カタカナのSJIS(範囲外ならそのまま変更しない)
; 破壊: なし(AX,DXは呼び出し内で退避)
;   0x829F(ぁ) <= v <= 0x82DD(む) : v += 0xA1
;   0x82DE(め) <= v <= 0x82F1(ん) : v += 0xA2  (0x7Fの欠番を飛び越すため)
;   それ以外                      : 変換しない
;--------------------------------------------------------------------
kata_convert:
        push    ax
        mov     ah, bl                  ; ax = (b1<<8)|b2
        mov     al, bh
        cmp     ax, 0x829F
        jb      .no
        cmp     ax, 0x82DD
        ja      .chk2
        add     ax, 0xA1
        jmp     .store
.chk2:
        cmp     ax, 0x82F1
        ja      .no
        add     ax, 0xA2
.store:
        mov     bl, ah
        mov     bh, al
.no:
        pop     ax
        ret

;--------------------------------------------------------------------
; Shift_JIS -> JIS 変換 (未検証。書籍の一般式をそのまま実装したもの)
; 入力: BL=SJIS上位バイト(b1), BH=SJIS下位バイト(b2)
; 出力: BL=JIS上位バイト(j1), BH=JIS下位バイト(j2)
;--------------------------------------------------------------------
sjis_to_jis:
        push    ax
        mov     al, bl                  ; al = b1
        cmp     al, 0x9F
        ja      .hi
        sub     al, 0x71
        jmp     .have_j1
.hi:
        sub     al, 0xB1
.have_j1:
        add     al, al                  ; j1 = (b1計算値)*2
        inc     al                      ; j1 = j1+1
        mov     bl, al                  ; bl = j1 (仮。下で+1されうる)
        mov     al, bh                  ; al = b2
        cmp     al, 0x7F
        jbe     .no_adj
        dec     al
.no_adj:
        cmp     al, 0x9E
        jb      .low
        sub     al, 0x7D
        inc     bl                      ; j1 = j1+1
        jmp     .set_j2
.low:
        sub     al, 0x1F
.set_j2:
        mov     bh, al                  ; bh = j2
        pop     ax
        ret

;--------------------------------------------------------------------
; ROMAJI.INC テーブル検索 (FEP.ASM find_exact/find_prefix と同一ロジック)
;--------------------------------------------------------------------

; CL = 照合するバイト数(romabuf の先頭 CL バイトを見る)。
; 完全一致: CF=1, SI = かなバイト列へのポインタ, AL = かなのバイト数。
; 不一致  : CF=0
find_exact:
        push    bx
        push    cx
        push    dx
        push    di
        push    bp
        mov     bl, cl                  ; bl = 目的の長さ
        mov     si, romaji_table
.rec:
        mov     al, [cs:si]             ; romlen
        or      al, al
        jz      .nf
        mov     dl, [cs:si+1]           ; kanalen
        xor     dh, dh
        add     si, 2                   ; si -> ローマ字バイト列先頭
        cmp     al, bl
        jne     .skip
        mov     di, si
        mov     bp, romabuf
        mov     cl, al
        xor     ch, ch
.cmp:
        mov     ah, [cs:bp]
        cmp     ah, [cs:di]
        jne     .skip
        inc     bp
        inc     di
        loop    .cmp
        mov     si, di                  ; si -> かなバイト列先頭
        mov     al, dl                  ; al = かなのバイト数
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        stc
        ret
.skip:
        xor     ah, ah
        add     si, ax
        add     si, dx
        jmp     .rec
.nf:
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        clc
        ret

; CL = romabuf の先頭 CL バイト。これが「CLバイトより長い」エントリの
; 先頭一致(prefix)になっているエントリが1つでもあれば CF=1。
find_prefix:
        push    ax
        push    bx
        push    cx
        push    dx
        push    di
        push    bp
        mov     bl, cl
        mov     si, romaji_table
.rec:
        mov     al, [cs:si]
        or      al, al
        jz      .nf
        mov     dl, [cs:si+1]
        xor     dh, dh
        add     si, 2
        cmp     al, bl
        jbe     .skip
        mov     di, si
        mov     bp, romabuf
        mov     cl, bl
        xor     ch, ch
.cmp:
        mov     ah, [cs:bp]
        cmp     ah, [cs:di]
        jne     .skip
        inc     bp
        inc     di
        loop    .cmp
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        stc
        ret
.skip:
        xor     ah, ah
        add     si, ax
        add     si, dx
        jmp     .rec
.nf:
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc
        ret

;--------------------------------------------------------------------
; kanabuf/kanawid への確定出力 (FEP.ASM kana_append と同一ロジック)
;--------------------------------------------------------------------

; SI = 追加するバイト列 (cs segment)、CL = バイト数、
; DH = kanawid に積む個数、DL = その値(幅)。
kana_append:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     al, [cs:kanalen]
        xor     ah, ah
        mov     bl, cl
        xor     bh, bh
        add     ax, bx
        cmp     ax, KANABUFSIZE
        ja      .full

        mov     [cs:ka_count], cl
        mov     [cs:ka_wcount], dh
        mov     [cs:ka_wval], dl

        mov     al, [cs:kanalen]
        xor     ah, ah
        mov     di, kanabuf
        add     di, ax
.copyloop:
        jcxz    .copydone
        mov     al, [cs:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .copyloop
.copydone:

        mov     al, [cs:kanacnt]
        xor     ah, ah
        mov     di, kanawid
        add     di, ax
        mov     cl, [cs:ka_wcount]
        xor     ch, ch
        mov     al, [cs:ka_wval]
.widloop:
        jcxz    .widdone
        mov     [cs:di], al
        inc     di
        loop    .widloop
.widdone:

        mov     al, [cs:kanalen]
        add     al, [cs:ka_count]
        mov     [cs:kanalen], al
        mov     al, [cs:kanacnt]
        add     al, [cs:ka_wcount]
        mov     [cs:kanacnt], al

.full:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; AL = 1バイト(ASCII、幅1)を kanabuf に追加する。
kana_append_ascii:
        push    si
        push    cx
        push    dx
        mov     [cs:scratchch], al
        mov     si, scratchch
        mov     cl, 1
        mov     dh, 1
        mov     dl, 1
        call    kana_append
        pop     dx
        pop     cx
        pop     si
        ret

;--------------------------------------------------------------------
; romabuf の先頭 BL バイトを捨てて詰める (FEP.ASM shift_romabuf_left と同一)
;--------------------------------------------------------------------
shift_romabuf_left:
        push    ax
        push    bx
        push    cx
        push    si
        push    di
        xor     bh, bh
        mov     al, [cs:romalen]
        sub     al, bl
        mov     cl, al
        xor     ch, ch
        mov     si, romabuf
        add     si, bx
        mov     di, romabuf
.loop:
        jcxz    .done
        mov     al, [cs:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .loop
.done:
        mov     al, [cs:romalen]
        sub     al, bl
        mov     [cs:romalen], al
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret

; AH = 文字。母音(a/i/u/e/o)なら CF=1。
is_vowel:
        cmp     ah, 'a'
        je      .yes
        cmp     ah, 'i'
        je      .yes
        cmp     ah, 'u'
        je      .yes
        cmp     ah, 'e'
        je      .yes
        cmp     ah, 'o'
        je      .yes
        clc
        ret
.yes:
        stc
        ret

; AL = 文字。子音(a-z、母音・n以外)なら CF=1。
is_consonant_al:
        cmp     al, 'a'
        jb      .no
        cmp     al, 'z'
        ja      .no
        cmp     al, 'a'
        je      .no
        cmp     al, 'i'
        je      .no
        cmp     al, 'u'
        je      .no
        cmp     al, 'e'
        je      .no
        cmp     al, 'o'
        je      .no
        cmp     al, 'n'
        je      .no
        stc
        ret
.no:
        clc
        ret

emit_n_sound:
        push    si
        push    cx
        push    dx
        mov     si, sound_n
        mov     cl, 2
        mov     dh, 1
        mov     dl, 2
        call    kana_append
        pop     dx
        pop     cx
        pop     si
        ret

emit_sokuon_sound:
        push    si
        push    cx
        push    dx
        mov     si, sound_sokuon
        mov     cl, 2
        mov     dh, 1
        mov     dl, 2
        call    kana_append
        pop     dx
        pop     cx
        pop     si
        ret

; romabuf の中身(romalen バイト)をそのまま素通しで全部 kanabuf へ確定し、
; romalen=0にする。
flush_romabuf_literal:
        push    ax
        push    cx
        push    si
        mov     cl, [cs:romalen]
        xor     ch, ch
        mov     si, romabuf
.loop:
        jcxz    .done
        mov     al, [cs:si]
        push    cx
        push    si
        call    kana_append_ascii
        pop     si
        pop     cx
        inc     si
        loop    .loop
.done:
        mov     byte [cs:romalen], 0
        pop     si
        pop     cx
        pop     ax
        ret

; CR/文節確定時: romabuf の残りを kanabuf へ確定する。
;   残り == "n" (1バイト)  -> ん
;   残り == "nn"(2バイト)  -> ん
;   それ以外               -> 素通し
finalize_romabuf:
        mov     al, [cs:romalen]
        cmp     al, 1
        jne     .chk2
        cmp     byte [cs:romabuf], 'n'
        jne     .passall
        call    emit_n_sound
        mov     byte [cs:romalen], 0
        ret
.chk2:
        cmp     al, 2
        jne     .passall
        cmp     byte [cs:romabuf], 'n'
        jne     .passall
        cmp     byte [cs:romabuf+1], 'n'
        jne     .passall
        call    emit_n_sound
        mov     byte [cs:romalen], 0
        ret
.passall:
        call    flush_romabuf_literal
        ret

;--------------------------------------------------------------------
; ローマ字->かな 変換本体 (FEP.ASM resolve_romaji と同一)
;--------------------------------------------------------------------
resolve_romaji:
.loop:
        mov     al, [cs:romalen]
        or      al, al
        jz      .ret

        cmp     al, 2
        jb      .no_nn
        mov     ah, [cs:romabuf]
        cmp     ah, 'n'
        jne     .no_nn
        mov     ah, [cs:romabuf+1]
        cmp     ah, 'n'
        jne     .no_nn
        cmp     al, 2
        je      .ret                    ; ちょうど"nn"なら3文字目待ち
        mov     ah, [cs:romabuf+2]
        call    is_vowel
        jc      .nn_vowel
        call    emit_n_sound
        mov     bl, 2
        call    shift_romabuf_left
        jmp     .loop
.nn_vowel:
        call    emit_n_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.no_nn:
        mov     cl, [cs:romalen]
        call    find_exact
        jc      .exact_found
        mov     cl, [cs:romalen]
        call    find_prefix
        jc      .ret                    ; 続き待ち

        mov     al, [cs:romalen]
        cmp     al, 2
        jb      .passthrough
        cmp     byte [cs:romabuf], 'n'
        jne     .chk_double
        call    emit_n_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop
.chk_double:
        mov     al, [cs:romabuf]
        mov     ah, [cs:romabuf+1]
        cmp     al, ah
        jne     .passthrough
        call    is_consonant_al
        jnc     .passthrough
        call    emit_sokuon_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.passthrough:
        mov     al, [cs:romabuf]
        call    kana_append_ascii
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.exact_found:
        mov     cl, al
        shr     al, 1
        mov     dh, al
        mov     dl, 2
        call    kana_append
        mov     byte [cs:romalen], 0
        jmp     .loop
.ret:
        ret

;--------------------------------------------------------------------
; 段階C: かな漢字変換 (FEP.ASM v1 の confirm_candidate/cancel_conversion/
; dict_lookup を移植したもの。ただし:
;   - 確定先は lbuf ではなく outq(kb_ring_push経由でキーボードBIOSへ)
;   - 辞書はディスク上(TSUKUSHI.DIC, v2/FEPDIC02形式)にあり、INT 1Bh で
;     必要なセクタだけをそのつど読む(常駐メモリには載せない)
;--------------------------------------------------------------------

; 現在の候補(cand_buf の cand_off[cand_index] から cand_len[cand_index]
; バイト)を outq へそのまま積み、kanabuf/候補状態をリセットする。
; (outq は幅情報を持たない生バイト列のキューなので sjis_width判定は不要。
; FEP.ASM の confirm_candidate と違い try_append/lbuf は使わない)
confirm_candidate:
        push    ax
        push    cx
        push    si
        push    di

        mov     al, [cs:cand_index]
        xor     ah, ah
        mov     di, ax                  ; di = index (cand_len用)
        mov     si, di
        add     si, si                  ; si = index*2 (cand_off用)
        mov     si, [cs:cand_off + si]
        add     si, cand_buf            ; si -> 候補バイト列先頭
        mov     cl, [cs:cand_len + di]
        xor     ch, ch
.loop:
        jcxz    .done
        mov     al, [cs:si]
        call    outq_push
        inc     si
        loop    .loop
.done:
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0
        mov     byte [cs:kata_flag], 0

        pop     di
        pop     si
        pop     cx
        pop     ax
        ret

; 変換を取り消して kanabuf 編集状態に戻る(kanabuf 自体はそのまま残す)。
cancel_conversion:
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0
        ret

; AL = 0-255 の値を10進数で vram_put_ascii により書く(DIはVRAMカーソル、
; 呼び出し側が維持する)。status_col の範囲を超える桁は書かずに捨てる。
; (呼び出し元は draw_status の本文区間のみ。BODYCOLS で打ち切る)
vram_put_decimal:
        push    ax
        push    bx
        push    cx
        push    dx
        xor     ah, ah
        mov     bx, 10
        xor     cx, cx
.divloop:
        xor     dx, dx
        div     bx                      ; ax = ax/10, dx = 余り(1桁)
        push    dx
        inc     cx
        or      ax, ax
        jnz     .divloop
.printloop:
        jcxz    .pdone
        pop     dx
        cmp     byte [cs:status_col], BODYCOLS
        jae     .skip
        mov     al, dl
        add     al, '0'
        call    vram_put_ascii
        inc     byte [cs:status_col]
.skip:
        dec     cx
        jmp     .printloop
.pdone:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;--------------------------------------------------------------------
; 段階C/D: LBA→CHS変換(FD用)・LBA→相対アドレス変換(HDD用)・ディスク読み込み
;--------------------------------------------------------------------

; IN: AX = LBA(word。FD候補はBPBの論理セクタ数に収まる前提で下位16bitのみ使う)
; OUT: CL=シリンダ(下位8bit。FD前提で77以下のため安全), DH=ヘッド, DL=セクタ(1起点)
;      (CHは書かない。CH=セクタ長Nは呼び出し側が別途 dic_sector_n からセットする)
; 破壊: AX,BX,DX。CL,DH,DLへ出力する関係でCXの下位バイトも書き換わる。
lba_to_chs:
        push    ax
        push    bx
        xor     dx, dx
        div     word [cs:dic_spt]       ; ax = lba/spt (トラック通番), dx = lba mod spt
        mov     bx, ax                  ; bx = トラック通番
        inc     dx
        mov     [cs:chs_sector_tmp], dl ; セクタ(1起点)
        xor     dx, dx
        mov     ax, bx
        div     word [cs:dic_heads]     ; ax = シリンダ, dx = ヘッド
        mov     cl, al                  ; シリンダ(下位8bit)
        mov     dh, dl                  ; ヘッド
        mov     dl, [cs:chs_sector_tmp] ; セクタ
        pop     bx
        pop     ax
        ret

chs_sector_tmp: db      0

; IN: DX:AX = 線形セクタ番号(32bit)
; OUT: CF=0のとき CL=bit0-7, CH=bit8-15, DL=bit16-23 (書籍2-9-4のHDD相対
;      アドレス指定形式そのまま。DL,CH,CLの24bitに線形セクタ番号、DLが
;      最上位バイト・CLが最下位バイト)。CF=1のとき24bitに収まらない(異常)。
; 破壊: AX,DX(DHは0であることを確認するだけで書き換えない)
lba_to_rel24:
        cmp     dh, 0
        jne     .overflow
        mov     cl, al
        mov     ch, ah
        clc
        ret
.overflow:
        stc
        ret

; 段階E: dic_quot_tmp(ファイル先頭からの相対セクタ番号、dic_bps単位)を
; 区間リスト(dic_run_start/dic_run_sectors, 計 dic_nruns 個)で引き、
; 絶対セクタ番号(dic_target_lbaと同じ単位)へ変換する。区間は出現順に
; 並んでいるので、先頭から累積セクタ数を引き算しながら該当区間を探す
; (区間は最大 MAXRUNS=32 個なので線形走査でよい)。
; IN: AX = dic_quot_tmp(word)
; OUT: DX:AX = 絶対セクタ番号(32bit)、CF=0のとき成功。
;      CF=1のとき該当区間なし(区間データとdic_quot_tmpが矛盾。想定外)。
; 破壊: BX,CX,SI
dic_locate_run:
        push    cx
        push    si
        mov     cl, [cs:dic_nruns]
        xor     ch, ch
        xor     si, si                  ; si = 区間インデックス×4 (dword配列オフセット)
.rloop:
        jcxz    .rfail
        mov     bx, [cs:dic_run_sectors + si]  ; 区間のセクタ数(下位ワード。
                                                ; dic_quot_tmpがwordである以上、
                                                ; 個々の区間長もword範囲に収まる)
        cmp     ax, bx
        jb      .rfound
        sub     ax, bx
        add     si, 4
        dec     cx
        jmp     .rloop
.rfound:
        mov     bx, ax                  ; bx = 区間内オフセット(word)
        mov     ax, [cs:dic_run_start + si]
        mov     dx, [cs:dic_run_start + si + 2]
        add     ax, bx
        adc     dx, 0
        clc
        pop     si
        pop     cx
        ret
.rfail:
        stc
        pop     si
        pop     cx
        ret

; IN: DX:AX = 辞書ファイル先頭からの絶対バイトオフセット(32bit), CX = 必要バイト数
;     (CX は dic_bps を超えないこと。呼び出し側はすべてこの前提で使う)
; OUT: DI = dic_read_buf 内のオフセット。そこから CX バイト連続で有効データがある
; 前提: 呼び出し側が ES=CS を設定済みであること(INT 1Bh の ES:BP用)
; 破壊: AX,BX,CX,DX,SI,DI,BP
read_dict_range:
        push    cx
        mov     bx, [cs:dic_bps]
        div     bx                      ; ax = 相対セクタ番号(オフセット/bps,word), dx = セクタ内オフセット
        mov     [cs:dic_within0], dx
        mov     [cs:dic_quot_tmp], ax

        ; dic_target_lba(32bit) = dic_quot_tmp(相対セクタ番号)を区間リストで
        ; 引いた絶対セクタ番号。断片化した辞書に対応するため、単一の
        ; dic_start_lbaへの加算ではなく区間ごとの開始位置+区間内オフセットに
        ; している(区間はインストール時にFATチェーンの連続区間としてまとめた
        ; もの。連続配置ならdic_nruns=1になり、従来の1本加算と同じ結果になる)。
        mov     ax, [cs:dic_quot_tmp]
        call    dic_locate_run          ; -> dx:ax = 絶対セクタ番号
        jnc     .loc_ok
        ; 区間データと矛盾(想定外)。読み込み失敗扱いにしてnotfound扱いへ倒す。
        mov     byte [cs:dic_read_failed], 1
        mov     di, [cs:dic_within0]
        pop     cx
        ret
.loc_ok:
        mov     [cs:dic_target_lba], ax
        mov     [cs:dic_target_lba+2], dx

        pop     cx
        push    cx
        mov     ax, [cs:dic_within0]
        add     ax, cx
        cmp     ax, [cs:dic_bps]
        jbe     .need1
        mov     byte [cs:dic_need2], 1
        jmp     .need_set
.need1:
        mov     byte [cs:dic_need2], 0
.need_set:
        pop     cx

        ; --- キャッシュ判定(32bit比較) ---
        mov     ax, [cs:dic_target_lba]
        mov     dx, [cs:dic_target_lba+2]
        cmp     dx, [cs:dic_buf_lba+2]
        jne     .miss
        cmp     ax, [cs:dic_buf_lba]
        jne     .miss
        cmp     byte [cs:dic_need2], 0
        je      .hit
        cmp     byte [cs:dic_buf_sectors], 2
        jae     .hit
.miss:
        call    .do_read
.hit:
        mov     di, [cs:dic_within0]
        ret

.do_read:
        push    ax
        push    bx
        push    cx
        push    dx

        cmp     byte [cs:dic_kind], 0
        jne     .hdd1

        ; --- FD: CHS指定(MF1/f0/SEEK1/コマンド6。実測(fep-measure-06)で確定) ---
        mov     ax, [cs:dic_target_lba]
        call    lba_to_chs              ; -> cl=cyl, dh=head, dl=sector
        mov     ch, [cs:dic_sector_n]
        mov     al, [cs:dic_da_ua]
        mov     ah, 0x76
        mov     bx, [cs:dic_bps]
        mov     bp, dic_read_buf
        int     0x1B
        jc      .read_err

        cmp     byte [cs:dic_need2], 0
        je      .one_sector

        mov     ax, [cs:dic_target_lba]
        inc     ax
        call    lba_to_chs
        mov     ch, [cs:dic_sector_n]
        mov     al, [cs:dic_da_ua]
        mov     ah, 0x76
        mov     bx, [cs:dic_bps]
        mov     bp, dic_read_buf
        add     bp, [cs:dic_bps]
        int     0x1B
        jc      .read_err
        jmp     .two_sectors

.hdd1:
        ; --- HDD: SASI相対アドレス指定(書籍2-9-4。MT/MF/SEEKビットは無い) ---
        mov     ax, [cs:dic_target_lba]
        mov     dx, [cs:dic_target_lba+2]
        call    lba_to_rel24            ; -> cl/ch/dl。CF=1なら24bit超過(異常)
        jc      .read_err
        mov     al, [cs:dic_da_ua]
        mov     ah, 0x06                ; コマンド6(読み出し)・r=0(リトライあり)
        mov     bx, [cs:dic_bps]
        mov     bp, dic_read_buf
        int     0x1B
        jc      .read_err

        cmp     byte [cs:dic_need2], 0
        je      .one_sector

        mov     ax, [cs:dic_target_lba]
        mov     dx, [cs:dic_target_lba+2]
        add     ax, 1
        adc     dx, 0
        call    lba_to_rel24
        jc      .read_err
        mov     al, [cs:dic_da_ua]
        mov     ah, 0x06
        mov     bx, [cs:dic_bps]
        mov     bp, dic_read_buf
        add     bp, [cs:dic_bps]
        int     0x1B
        jc      .read_err
        jmp     .two_sectors

.one_sector:
        mov     byte [cs:dic_buf_sectors], 1
        jmp     .read_ok
.two_sectors:
        mov     byte [cs:dic_buf_sectors], 2
.read_ok:
        mov     ax, [cs:dic_target_lba]
        mov     dx, [cs:dic_target_lba+2]
        mov     [cs:dic_buf_lba], ax
        mov     [cs:dic_buf_lba+2], dx
        jmp     .do_read_ret
.read_err:
        ; 読み取り失敗: キャッシュを無効化し、以後 dict_lookup 側で
        ; dic_read_failed を見て notfound 扱いにする(ハンドラ内で
        ; エラーメッセージを出す手段が無いため。未検証の異常系)。
        mov     word [cs:dic_buf_lba], 0xFFFF
        mov     word [cs:dic_buf_lba+2], 0xFFFF
        mov     byte [cs:dic_read_failed], 1
.do_read_ret:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; 比較: dic_entry_yomi_buf[0..dic_cur_cmp_len) と kanabuf[0..kanalen) を
; バイト列として memcmp 順で比較する(tools/mkdic2.py のソート順と同じ:
; 短い方が小さい)。
; OUT: AL = 0(一致) / 1(エントリ側が大きい) / 2(エントリ側が小さい)
compare_yomi_to_target:
        push    bx
        push    cx
        push    si
        push    di

        mov     cl, [cs:dic_cur_cmp_len]
        xor     ch, ch
        mov     si, dic_entry_yomi_buf
        mov     di, kanabuf
.cmp_loop:
        jcxz    .cmp_prefix_equal
        mov     al, [cs:si]
        mov     bl, [cs:di]
        cmp     al, bl
        jb      .entry_less
        ja      .entry_greater
        inc     si
        inc     di
        loop    .cmp_loop
.cmp_prefix_equal:
        mov     al, [cs:dic_cur_yomi_len]
        xor     ah, ah
        mov     bl, [cs:kanalen]
        xor     bh, bh
        cmp     ax, bx
        je      .equal
        jb      .entry_less
        jmp     .entry_greater
.equal:
        mov     al, 0
        jmp     .cmp_done
.entry_less:
        mov     al, 2
        jmp     .cmp_done
.entry_greater:
        mov     al, 1
.cmp_done:
        pop     di
        pop     si
        pop     cx
        pop     bx
        ret

; IN: DX:AX = エントリの絶対バイトオフセット(32bit)
; OUT: dic_cur_yomi_len = よみの実バイト長
;      dic_cur_cmp_len  = 比較に使うバイト数 = min(よみ長, kanalen)
;      dic_entry_yomi_buf の先頭 dic_cur_cmp_len バイトによみをコピー済み
;      dic_cand_count_off = このエントリの「候補数」バイトの絶対位置
; 前提: ES=CS(呼び出し側が設定済み)
; 破壊: AX,BX,CX,DX,SI,DI
read_entry_at:
        push    bx
        push    cx
        push    si
        push    di

        mov     [cs:dic_pos], ax
        mov     [cs:dic_pos+2], dx

        mov     cx, 1
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        mov     bx, di
        mov     al, [cs:dic_read_buf + bx]
        mov     [cs:dic_cur_yomi_len], al

        ; cmp_len = min(yomi_len, kanalen)
        xor     ah, ah
        mov     bl, [cs:kanalen]
        xor     bh, bh
        cmp     ax, bx
        jbe     .cmplen_is_yomi
        mov     ax, bx
.cmplen_is_yomi:
        mov     [cs:dic_cur_cmp_len], al

        cmp     byte [cs:dic_cur_cmp_len], 0
        je      .no_yomi_bytes

        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        add     ax, 1
        adc     dx, 0
        mov     cl, [cs:dic_cur_cmp_len]
        xor     ch, ch
        push    cx
        call    read_dict_range
        pop     cx
        mov     si, di
        mov     di, dic_entry_yomi_buf
.copy_yomi:
        jcxz    .copy_yomi_done
        mov     al, [cs:dic_read_buf + si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .copy_yomi
.copy_yomi_done:
.no_yomi_bytes:

        ; dic_cand_count_off = dic_pos + 1 + yomi_len(実長)
        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        add     ax, 1
        adc     dx, 0
        mov     bx, ax
        mov     al, [cs:dic_cur_yomi_len]
        xor     ah, ah
        add     bx, ax
        adc     dx, 0
        mov     [cs:dic_cand_count_off], bx
        mov     [cs:dic_cand_count_off+2], dx

        pop     di
        pop     si
        pop     cx
        pop     bx
        ret

; dic_cand_count_off にある候補群(候補数1バイト+各候補の長さ+本体)を
; 読み飛ばし、その直後(=次エントリの先頭)を dic_pos にセットする。
; 前提: ES=CS
; 破壊: AX,BX,CX,DX,SI,DI
skip_candidates_get_next:
        push    bx

        mov     ax, [cs:dic_cand_count_off]
        mov     dx, [cs:dic_cand_count_off+2]
        mov     cx, 1
        call    read_dict_range
        mov     bx, di
        mov     al, [cs:dic_read_buf + bx]
        xor     ah, ah
        mov     [cs:dic_skip_cnt], ax

        mov     ax, [cs:dic_cand_count_off]
        mov     dx, [cs:dic_cand_count_off+2]
        add     ax, 1
        adc     dx, 0
        mov     [cs:dic_pos], ax
        mov     [cs:dic_pos+2], dx

.skip_loop:
        cmp     word [cs:dic_skip_cnt], 0
        je      .skip_done
        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        mov     cx, 1
        call    read_dict_range
        mov     bx, di
        mov     al, [cs:dic_read_buf + bx]      ; 候補のバイト長
        xor     ah, ah
        inc     ax                              ; +1(長さバイト自身のぶん)
        add     [cs:dic_pos], ax
        adc     word [cs:dic_pos+2], 0
        dec     word [cs:dic_skip_cnt]
        jmp     .skip_loop
.skip_done:
        pop     bx
        ret

; dic_cand_count_off にある候補群を cand_buf/cand_off/cand_len へ読み込み、
; cand_count/cand_index を設定する(旧・常駐辞書版 dict_lookup の候補コピー
; ループと同じ考え方。CANDMAX/CANDBUFSIZEを超えたら安全側で打ち切る)。
; 前提: ES=CS
; 破壊: AX,BX,CX,DX,SI,DI,BP
load_candidates_from_dic:
        mov     ax, [cs:dic_cand_count_off]
        mov     dx, [cs:dic_cand_count_off+2]
        mov     cx, 1
        call    read_dict_range
        mov     bx, di
        mov     al, [cs:dic_read_buf + bx]
        cmp     al, CANDMAX
        jbe     .cc_ok
        mov     al, CANDMAX
.cc_ok:
        mov     [cs:cand_count], al
        mov     byte [cs:cand_index], 0

        mov     ax, [cs:dic_cand_count_off]
        mov     dx, [cs:dic_cand_count_off+2]
        add     ax, 1
        adc     dx, 0
        mov     [cs:dic_pos], ax
        mov     [cs:dic_pos+2], dx

        mov     word [cs:cb_pos], 0
        mov     byte [cs:cand_read_i], 0

.cand_loop:
        mov     al, [cs:cand_read_i]
        cmp     al, [cs:cand_count]
        jae     .cand_done

        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        mov     cx, 1
        call    read_dict_range
        mov     bx, di
        mov     al, [cs:dic_read_buf + bx]      ; 候補のバイト長
        mov     [cs:cand_cur_len], al

        mov     ax, [cs:cb_pos]
        mov     bl, [cs:cand_cur_len]
        xor     bh, bh
        add     ax, bx
        cmp     ax, CANDBUFSIZE
        ja      .cand_overflow

        mov     al, [cs:cand_read_i]
        xor     ah, ah
        mov     di, ax                  ; di = i (cand_len用インデックス)
        mov     bp, di
        add     bp, bp                  ; bp = i*2 (cand_off用インデックス)
        mov     ax, [cs:cb_pos]
        mov     [cs:cand_off + bp], ax
        mov     al, [cs:cand_cur_len]
        mov     [cs:cand_len + di], al

        ; 候補本体を読んで cand_buf + cb_pos へコピーする
        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        add     ax, 1
        adc     dx, 0                   ; 候補本体の開始位置(長さバイトの次)
        mov     cl, [cs:cand_cur_len]
        xor     ch, ch
        push    cx
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        pop     cx
        mov     si, di
        mov     bx, cand_buf
        add     bx, [cs:cb_pos]
.copy_cand:
        jcxz    .copy_cand_done
        mov     al, [cs:dic_read_buf + si]
        mov     [cs:bx], al
        inc     si
        inc     bx
        loop    .copy_cand
.copy_cand_done:

        mov     al, [cs:cand_cur_len]
        xor     ah, ah
        add     [cs:cb_pos], ax

        ; dic_pos を候補ぶん進める(長さバイト1 + 本体)
        mov     al, [cs:cand_cur_len]
        xor     ah, ah
        inc     ax
        add     [cs:dic_pos], ax
        adc     word [cs:dic_pos+2], 0

        inc     byte [cs:cand_read_i]
        jmp     .cand_loop

.cand_overflow:
        mov     al, [cs:cand_read_i]
        mov     [cs:cand_count], al
.cand_done:
        ret

; kanabuf(kanalen バイト)をよみとして、ディスク上の辞書(TSUKUSHI.DIC, v2形式)を
; 「疎索引を二分探索 -> ブロック内を最大SPARSE件だけ線形走査」で引く
; (手順は tools/dic2_ref.py の docstring と同一)。
; 見つかれば cand_count>0, cand_index=0 とし cand_buf/cand_off/cand_len を
; 設定する。見つからない・よみが空・ディスク読み取りに失敗した場合は
; cand_count=0 のまま(kanabuf は変えない)。
dict_lookup:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    es

        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0
        mov     byte [cs:dic_read_failed], 0

        mov     al, [cs:kanalen]
        or      al, al
        jz      .notfound               ; 空のよみは引かない

        mov     ax, cs
        mov     es, ax                  ; 以降 INT 1Bh の ES:BP はこの ES を使う

        ; --- 1) 疎索引を二分探索: 「先頭エントリの読み <= target」である
        ;        最後のブロックを探す ---
        mov     word [cs:dic_lo], 0
        mov     ax, [cs:dic_sparse_count]
        dec     ax
        mov     [cs:dic_hi], ax
        mov     word [cs:dic_best_block], 0xFFFF

.bsearch_loop:
        cmp     byte [cs:dic_read_failed], 0
        jne     .notfound

        mov     ax, [cs:dic_lo]
        cmp     ax, [cs:dic_hi]         ; 符号あり比較(hi=-1=0xFFFFの折返しも正しく扱える)
        jg      .bsearch_done

        mov     ax, [cs:dic_lo]
        add     ax, [cs:dic_hi]
        shr     ax, 1
        mov     [cs:dic_mid], ax

        ; sparse_entry_off(32bit) = dic_sparse_off + mid*4
        mov     ax, [cs:dic_mid]
        shl     ax, 1
        shl     ax, 1                   ; ax = mid*4 (mid<=65535/4なので安全)
        xor     dx, dx
        add     ax, [cs:dic_sparse_off]
        adc     dx, [cs:dic_sparse_off+2]
        mov     cx, 4
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        mov     bx, di
        mov     ax, [cs:dic_read_buf + bx]
        mov     [cs:dic_entry_off], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        mov     [cs:dic_entry_off+2], ax

        mov     ax, [cs:dic_entry_off]
        mov     dx, [cs:dic_entry_off+2]
        call    read_entry_at
        call    compare_yomi_to_target
        cmp     al, 1
        je      .go_lower

        ; entry <= target
        mov     ax, [cs:dic_mid]
        mov     [cs:dic_best_block], ax
        inc     ax
        mov     [cs:dic_lo], ax
        jmp     .bsearch_loop
.go_lower:
        mov     ax, [cs:dic_mid]
        dec     ax
        mov     [cs:dic_hi], ax
        jmp     .bsearch_loop

.bsearch_done:
        cmp     byte [cs:dic_read_failed], 0
        jne     .notfound
        cmp     word [cs:dic_best_block], 0xFFFF
        je      .notfound

        ; --- 2) ブロック内を最大SPARSE件だけ線形走査 ---
        mov     ax, [cs:dic_best_block]
        shl     ax, 1
        shl     ax, 1
        xor     dx, dx
        add     ax, [cs:dic_sparse_off]
        adc     dx, [cs:dic_sparse_off+2]
        mov     cx, 4
        call    read_dict_range
        mov     bx, di
        mov     ax, [cs:dic_read_buf + bx]
        mov     [cs:dic_pos], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        mov     [cs:dic_pos+2], ax

        ; ブロック終端: best_block+1 < sparse_count なら sparse[best_block+1]、
        ; そうでなければ dic_data_end
        mov     ax, [cs:dic_best_block]
        inc     ax
        cmp     ax, [cs:dic_sparse_count]
        jae     .use_data_end

        shl     ax, 1
        shl     ax, 1
        xor     dx, dx
        add     ax, [cs:dic_sparse_off]
        adc     dx, [cs:dic_sparse_off+2]
        mov     cx, 4
        call    read_dict_range
        mov     bx, di
        mov     ax, [cs:dic_read_buf + bx]
        mov     [cs:dic_block_end], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        mov     [cs:dic_block_end+2], ax
        jmp     .have_end
.use_data_end:
        mov     ax, [cs:dic_data_end]
        mov     [cs:dic_block_end], ax
        mov     ax, [cs:dic_data_end+2]
        mov     [cs:dic_block_end+2], ax
.have_end:

        mov     ax, [cs:dic_sparse]
        mov     [cs:dic_steps_left], ax

.lin_loop:
        cmp     byte [cs:dic_read_failed], 0
        jne     .notfound
        cmp     word [cs:dic_steps_left], 0
        je      .notfound

        ; pos >= block_end なら打ち切り(32bit比較)
        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        cmp     dx, [cs:dic_block_end+2]
        ja      .notfound
        jb      .pos_ok
        cmp     ax, [cs:dic_block_end]
        jae     .notfound
.pos_ok:
        dec     word [cs:dic_steps_left]

        mov     ax, [cs:dic_pos]
        mov     dx, [cs:dic_pos+2]
        call    read_entry_at
        call    compare_yomi_to_target
        cmp     al, 0
        je      .match
        cmp     al, 1
        je      .notfound               ; entry > target: これ以上進んでも見つからない

        ; entry < target: 候補群を読み飛ばして次のエントリへ
        call    skip_candidates_get_next
        jmp     .lin_loop

.match:
        cmp     byte [cs:dic_read_failed], 0
        jne     .notfound
        call    load_candidates_from_dic
        cmp     byte [cs:cand_count], 0
        je      .notfound
        pop     es
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        stc
        ret

.notfound:
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0
        pop     es
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc
        ret

;--------------------------------------------------------------------
; ステータス行描画 (テキストVRAM直書き。ノーマルモード640x400専用)
;--------------------------------------------------------------------

; AL = 書く文字コード(半角ASCII)、DI = 現在の文字プレーン内オフセット。
; 1桁書いて DI を+2して返す。
vram_put_ascii:
        push    ax
        mov     [es:di], al
        mov     byte [es:di+1], 0
        add     di, 2
        pop     ax
        ret

; BL/BH = SJIS(b1/b2)。JISへ変換して全角4バイトを書く。DIを+4して返す。
vram_put_wide:
        push    ax
        push    bx
        call    sjis_to_jis             ; bl=j1, bh=j2
        mov     al, bl
        sub     al, 0x20
        mov     [es:di], al             ; 左半分
        mov     al, bh
        mov     [es:di+1], al
        mov     al, bl
        sub     al, 0x20
        add     al, 0x80
        mov     [es:di+2], al           ; 右半分
        mov     al, bh
        mov     [es:di+3], al
        add     di, 4
        pop     bx
        pop     ax
        ret

draw_status:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        mov     ax, 0xA000
        mov     es, ax

        ; --- 25行目(文字160+属性160=320バイト)を退避 ---
        ; かなモード中に何度もdraw_statusが呼ばれても、退避内容が
        ; 自分の描いたステータス行で上書きされないよう、既に退避済み
        ; なら退避処理そのものをスキップする(status_savedで判定)。
        cmp     byte [cs:status_saved], 0
        jne     .save_skip
        mov     si, TEXT_BASE + STATUS_OFF
        mov     di, status_save
        mov     cx, 160
.save_text:
        jcxz    .save_text_done
        mov     al, [es:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .save_text
.save_text_done:
        mov     si, ATTR_BASE + STATUS_OFF
        mov     di, status_save + 160
        mov     cx, 160
.save_attr:
        jcxz    .save_attr_done
        mov     al, [es:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .save_attr
.save_attr_done:
        mov     byte [cs:status_saved], 1
.save_skip:

        ; --- 内容を描く ---
        mov     di, TEXT_BASE + STATUS_OFF
        mov     byte [cs:status_col], 0

        cmp     byte [cs:mode], 0
        je      .direct_prefix
        mov     si, msg_prefix_kana
        mov     cx, 7                   ; "[KANA] "
        jmp     .prefix_out
.direct_prefix:
        mov     si, msg_prefix_direct
        mov     cx, 9                   ; "[DIRECT] "
.prefix_out:
.prefix_loop:
        jcxz    .prefix_done
        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        inc     byte [cs:status_col]
        loop    .prefix_loop
.prefix_done:

        ; --- 変換中なら候補 + " (i/n)"、そうでなければ kanabuf を表示 ---
        cmp     byte [cs:cand_count], 0
        jne     .cand_display
        jmp     .show_kanabuf

.cand_display:
        mov     al, [cs:cand_index]
        xor     ah, ah
        mov     bx, ax
        mov     al, [cs:cand_len + bx]
        mov     [cs:ka_wcount], al      ; 空きスクラッチを一時的に流用(候補バイト数)
        add     bx, bx                  ; bx = index*2
        mov     bx, [cs:cand_off + bx]
        mov     si, cand_buf
        add     si, bx                  ; si -> 候補バイト列先頭
        mov     cl, [cs:ka_wcount]
        xor     ch, ch
.cand_loop2:
        jcxz    .cand_loop2_done
        mov     al, [cs:si]
        call    sjis_width              ; al=1 or 2, CF=1なら幅2
        jc      .cand_wide2
        cmp     byte [cs:status_col], BODYCOLS
        jae     .cand_loop2_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        dec     cx
        inc     byte [cs:status_col]
        jmp     .cand_loop2
.cand_wide2:
        mov     al, [cs:status_col]
        add     al, 2
        cmp     al, BODYCOLS
        ja      .cand_loop2_done
        mov     bl, [cs:si]
        mov     bh, [cs:si+1]
        call    vram_put_wide
        add     si, 2
        sub     cx, 2
        add     byte [cs:status_col], 2
        jmp     .cand_loop2
.cand_loop2_done:

        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, ' '
        call    vram_put_ascii
        inc     byte [cs:status_col]

        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, '('
        call    vram_put_ascii
        inc     byte [cs:status_col]

        mov     al, [cs:cand_index]
        inc     al
        call    vram_put_decimal

        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, '/'
        call    vram_put_ascii
        inc     byte [cs:status_col]

        mov     al, [cs:cand_count]
        call    vram_put_decimal

        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, ')'
        call    vram_put_ascii
        inc     byte [cs:status_col]
        jmp     .content_done

.show_kanabuf:
        ; --- kanabuf(幅混在。kanawidで1グリフずつ判定)
        ; si = kanabuf内ポインタ, bp = kanawid内ポインタ, cx = 残りグリフ数 ---
        mov     si, kanabuf
        mov     bp, kanawid
        xor     ch, ch
        mov     cl, [cs:kanacnt]
.kb_loop:
        jcxz    .kb_done
        mov     al, [cs:bp]             ; al = このグリフの幅(1 or 2)
        cmp     al, 2
        je      .kb_wide
        ; 幅1(ASCII素通し文字)
        cmp     byte [cs:status_col], BODYCOLS
        jae     .kb_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        inc     byte [cs:status_col]
        jmp     .kb_next
.kb_wide:
        mov     al, [cs:status_col]
        add     al, 2
        cmp     al, BODYCOLS            ; 全角の途中で切らない: 現在桁+2がBODYCOLSを
        ja      .kb_done                ; 超えるなら1文字手前で打ち切り、残りは空白パッドに任せる
        mov     bl, [cs:si]             ; bl = SJIS上位バイト(b1)
        mov     bh, [cs:si+1]           ; bh = SJIS下位バイト(b2)
        cmp     byte [cs:kata_flag], 0
        je      .kb_wide_noconv
        call    kata_convert            ; kanabuf自体は変えず、表示だけカタカナ化
.kb_wide_noconv:
        call    vram_put_wide
        add     si, 2
        add     byte [cs:status_col], 2
.kb_next:
        inc     bp
        loop    .kb_loop
.kb_done:

        ; --- romabuf(未確定のローマ字。ASCIIのみ) ---
        mov     si, romabuf
        xor     ch, ch
        mov     cl, [cs:romalen]
.rb_loop:
        jcxz    .content_done
        cmp     byte [cs:status_col], BODYCOLS
        jae     .content_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        inc     byte [cs:status_col]
        loop    .rb_loop

.content_done:
        ; --- 本文を桁BODYCOLS(64)まで空白で埋める ---
        mov     al, BODYCOLS
        sub     al, [cs:status_col]
        jbe     .pad_done
        xor     ch, ch
        mov     cl, al
.pad_loop:
        jcxz    .pad_done
        mov     al, ' '
        call    vram_put_ascii
        loop    .pad_loop
.pad_done:

        ; --- 桁64: 区切りの空白 ---
        mov     al, ' '
        call    vram_put_ascii

        ; --- 桁65-79: 製品名ラベルを右詰めで表示(常にMSG_LABEL_LEN桁ぴったり) ---
        mov     si, msg_label
        mov     cx, MSG_LABEL_LEN
.label_loop:
        jcxz    .label_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        loop    .label_loop
.label_done:

        ; --- 属性: 25行目全80桁を反転(0x05)にする ---
        mov     di, ATTR_BASE + STATUS_OFF
        mov     cx, STATUSCOLS
.attr_loop:
        jcxz    .attr_done
        mov     byte [es:di], REVERSE_ATTR
        add     di, 2
        loop    .attr_loop
.attr_done:

        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; --- 直接モードへ戻るとき: 退避しておいた25行目(文字160+属性160)を
; VRAMへ書き戻し、退避済みフラグを下ろす。status_savedが0(=未退避)の
; ときは呼ばれない想定だが、そのときは何もしない(ゼロ埋めの退避領域を
; 誤って書き戻さない)。 ---
restore_status:
        push    ax
        push    cx
        push    si
        push    di
        push    es

        cmp     byte [cs:status_saved], 0
        je      .rs_done

        mov     ax, 0xA000
        mov     es, ax

        mov     si, status_save
        mov     di, TEXT_BASE + STATUS_OFF
        mov     cx, 160
.rs_text:
        jcxz    .rs_text_done
        mov     al, [cs:si]
        mov     [es:di], al
        inc     si
        inc     di
        loop    .rs_text
.rs_text_done:
        mov     si, status_save + 160
        mov     di, ATTR_BASE + STATUS_OFF
        mov     cx, 160
.rs_attr:
        jcxz    .rs_attr_done
        mov     al, [cs:si]
        mov     [es:di], al
        inc     si
        inc     di
        loop    .rs_attr
.rs_attr_done:
        mov     byte [cs:status_saved], 0

.rs_done:
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     ax
        ret

;--------------------------------------------------------------------
; メッセージ (ステータス行プレフィクス。VRAM直書きで使うので '$' 終端は不要)
;--------------------------------------------------------------------
msg_prefix_kana:        db      '[KANA] '
msg_prefix_direct:      db      '[DIRECT] '
msg_label:              db      'FEP by Tsukushi'       ; 桁65-79(MSG_LABEL_LEN=15桁)

;--------------------------------------------------------------------
; INT 18h ハンドラ
;--------------------------------------------------------------------
;
; *** このハンドラの中から INT 21h を呼んではいけない ***
; (DOS の CON ドライバから呼ばれている可能性があるため。DOSの再入は禁止)
;
newint18:
        cmp     ax, IDENT_FUNC
        je      .ident
        cmp     ah, 0x00
        je      .ah00
        cmp     ah, 0x01
        je      .ah01
        jmp     far [cs:oldint18]       ; AH=02h その他は素通し(FreeCOMのシフト状態取得等)

.ident:
        mov     ax, IDENT_REPLY
        iret

;----------------------------------------------------------------
; AH=00h (キー入力・待ち)
;----------------------------------------------------------------
.ah00:
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp

        ; ハンドラ入口: 前回までに outq に溜まった残りをリングバッファへ
        ; 移す(CONが読んで空きができた分だけ入る)。
        call    drain_outq_to_ring

.ah00_loop:
        mov     ah, 0x00
        pushf
        call    far [cs:oldint18]       ; -> ah=キーコード, al=文字コード

        ; --- 自分がリングバッファへ積んだバイトの読み戻し ---
        ; drain_outq_to_ring で積んだバイトを、この oldint18 呼び出しが
        ; そのまま拾って返してくることがある。これをローマ字処理へ回すと
        ; 二重変換になるので、印(INJECT_KEYCODE)を見たら即座にそのまま返す。
        cmp     ah, INJECT_KEYCODE
        je      .ah00_injected

        ; --- 切り替えキー判定 (かなモードでも直接モードでも有効) ---
        cmp     ah, 0xB5                ; Ctrl+XFER
        je      .ah00_toggle
        cmp     al, 0x0B                ; Ctrl-K
        je      .ah00_toggle

        cmp     byte [cs:mode], 0
        je      .ah00_direct_return     ; 直接モード: 取得したキーをそのまま返す

        ; --- かなモード ---
        cmp     al, 0x0D
        je      .ah00_do_cr
        cmp     al, 0x08
        je      .ah00_do_bs
        cmp     al, 0x1B
        je      .ah00_do_esc
        cmp     al, 0x20
        je      .ah00_do_space
        cmp     al, 0x00
        jne     .ah00_notcursor
        ; --- 文字コードを持たないキー(カーソル等) ---
        ; F6/F7(カタカナ変換の切替)は変換中かどうかに関わらず判定する。
        ; F8(半角カタカナ)/F9/F10 は今回未対応。今までどおり素通しする。
        cmp     ah, 0x68                ; F7 = カタカナ化
        je      .ah00_f7
        cmp     ah, 0x67                ; F6 = ひらがなに戻す
        je      .ah00_f6
        ; 変換中(cand_count>0)のときだけ↑/↓/Shift+XFERを候補選択に横取りする。
        ; 変換中でなければ今までどおり素通し(DOS側のカーソル操作を壊さない)。
        cmp     byte [cs:cand_count], 0
        je      .ah00_direct_return
        cmp     ah, 0x3A                ; ↑ = 前候補へ
        je      .ah00_cand_prev
        cmp     ah, 0x3D                ; ↓ = 次候補へ(Spaceと同じ)
        je      .ah00_space_next
        cmp     ah, 0xA5                ; Shift+XFER = 前候補へ
        je      .ah00_cand_prev
        jmp     .ah00_direct_return
.ah00_notcursor:

        ; --- それ以外: ローマ字処理へ ---
        cmp     al, 'A'
        jb      .ah00_rj_noconv
        cmp     al, 'Z'
        ja      .ah00_rj_noconv
        add     al, 0x20                ; 大文字は小文字化する(FEP.ASMと同じ)
.ah00_rj_noconv:
        cmp     byte [cs:cand_count], 0
        je      .ah00_rj_go
        ; 変換中に別の文字が来た: まず現在の候補を確定してからローマ字処理へ回す
        ; (FEP.ASMのnihon不具合と同種の取りこぼしを避けるため確定を先に行う)
        call    confirm_candidate
.ah00_rj_go:
        cmp     byte [cs:romalen], ROMABUFSIZE
        jae     .ah00_redraw_continue   ; 安全策: 溢れたら捨てる
        mov     bl, [cs:romalen]
        xor     bh, bh
        mov     si, romabuf
        add     si, bx
        mov     [cs:si], al
        inc     byte [cs:romalen]
        call    resolve_romaji
        jmp     .ah00_redraw_continue

.ah00_toggle:
        cmp     byte [cs:cand_count], 0
        je      .ah00_toggle_simple
        call    cancel_conversion       ; 変換中: 取り消してから切替(かなは残す)
.ah00_toggle_simple:
        xor     byte [cs:mode], 1
        cmp     byte [cs:mode], 0
        je      .ah00_toggle_to_direct
        ; 直接->かな: ステータス行を描く(draw_status内で未退避なら退避する)
        call    draw_status
        jmp     .ah00_after_status
.ah00_toggle_to_direct:
        ; かな->直接: 退避しておいた内容を書き戻し、以後は描かない
        mov     byte [cs:kata_flag], 0
        call    restore_status
        jmp     .ah00_after_status

.ah00_do_space:
        cmp     byte [cs:cand_count], 0
        jne     .ah00_space_next
        ; 変換中でない: Spaceの前に未確定ローマ字を確定処理してから辞書引きする
        ; (n/nnの取りこぼし対策。FEP.ASMで実績のある不具合と同じ理由)
        ; カタカナ表示中だった場合は通常の変換に戻す(kata_flag=0)。
        mov     byte [cs:kata_flag], 0
        call    finalize_romabuf
        call    dict_lookup             ; 見つかれば cand_count>0, cand_index=0
        jmp     .ah00_redraw_continue
.ah00_space_next:
        ; 変換中: 次候補へ(末尾まで行ったら先頭へ戻る)
        mov     al, [cs:cand_index]
        inc     al
        cmp     al, [cs:cand_count]
        jb      .ah00_space_set
        xor     al, al
.ah00_space_set:
        mov     [cs:cand_index], al
        jmp     .ah00_redraw_continue

.ah00_f7:
        ; F7: kanabufが空なら何もせず素通し。変換中なら取り消してから
        ; kata_flag=1(カタカナ表示)にする。かなの中身自体は変えない。
        cmp     byte [cs:kanalen], 0
        je      .ah00_direct_return
        cmp     byte [cs:cand_count], 0
        je      .ah00_f7_setflag
        call    cancel_conversion
.ah00_f7_setflag:
        mov     byte [cs:kata_flag], 1
        jmp     .ah00_redraw_continue

.ah00_f6:
        ; F6: ひらがな表示に戻す。かなモードなら常に有効。
        mov     byte [cs:kata_flag], 0
        jmp     .ah00_redraw_continue

.ah00_cand_prev:
        ; 前候補へ(先頭で押したら末尾へ回る)。呼び出し元で cand_count>0 を保証済み。
        mov     al, [cs:cand_index]
        or      al, al
        jnz     .ah00_cand_prev_dec
        mov     al, [cs:cand_count]
.ah00_cand_prev_dec:
        dec     al
        mov     [cs:cand_index], al
        jmp     .ah00_redraw_continue

.ah00_do_cr:
        cmp     byte [cs:cand_count], 0
        je      .ah00_cr_not_converting
        ; 変換中: 現在の候補を確定する
        call    confirm_candidate
        jmp     .ah00_redraw_continue
.ah00_cr_not_converting:
        call    finalize_romabuf        ; romabuf残りをkanabufへ回収
        cmp     byte [cs:kanalen], 0
        jne     .ah00_cr_has_kana
        ; kanabufが空: そのままEnterを通す(outqへ0x0Dを積む)
        mov     al, 0x0D
        call    outq_push
        jmp     .ah00_redraw_continue
.ah00_cr_has_kana:
        ; kanabufの中身をoutqへ積む(=かなの確定。Enter自体は消費する)
        call    push_kanabuf_to_outq
        jmp     .ah00_redraw_continue

.ah00_do_bs:
        cmp     byte [cs:cand_count], 0
        je      .ah00_bs_edit
        ; 変換中: 変換を取り消して kanabuf に戻る(かなは削らない)
        call    cancel_conversion
        jmp     .ah00_redraw_continue
.ah00_bs_edit:
        cmp     byte [cs:romalen], 0
        je      .ah00_bs_kana
        dec     byte [cs:romalen]
        jmp     .ah00_redraw_continue
.ah00_bs_kana:
        cmp     byte [cs:kanacnt], 0
        je      .ah00_bs_passthrough    ; どちらも空: BSをそのまま返す
        dec     byte [cs:kanacnt]
        mov     al, [cs:kanacnt]
        xor     ah, ah
        mov     bx, ax
        mov     al, [cs:kanawid + bx]   ; 削る幅(1 or 2)
        sub     [cs:kanalen], al
        jmp     .ah00_redraw_continue
.ah00_bs_passthrough:
        jmp     .ah00_exit              ; ah/al は call far の結果のまま返す

.ah00_do_esc:
        cmp     byte [cs:cand_count], 0
        je      .ah00_esc_not_converting
        ; 変換中: 変換を取り消してかなに戻る(かなは残す)
        call    cancel_conversion
        jmp     .ah00_redraw_continue
.ah00_esc_not_converting:
        mov     byte [cs:romalen], 0
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:kata_flag], 0
        jmp     .ah00_redraw_continue

.ah00_direct_return:
        jmp     .ah00_exit

.ah00_redraw_continue:
        ; この経路は mode==1(かなモード)のときにしか通らない
        ; (直接モードの各キーは .ah00_direct_return で早期に返るため)。
        ; toggle だけは自分で .ah00_after_status へ直接ジャンプして
        ; ここを経由しない。
        call    draw_status
.ah00_after_status:
        ; ここまでで outq に新規確定バイトが積まれた可能性がある
        ; (かな確定/単独Enter)。次の .ah00_loop の oldint18 呼び出しが
        ; 実キー待ちでブロックしてしまう前に、リングバッファへ移しておく。
        call    drain_outq_to_ring
        jmp     .ah00_loop

.ah00_injected:
        ; drain_outq_to_ring が積んだバイトの読み戻し。何も処理せず
        ; AH=0, AL=そのバイトのまま即座に返す。
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        xor     ah, ah
        iret

.ah00_exit:
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        iret

;----------------------------------------------------------------
; AH=01h (キーセンス。絶対に待たないこと)
;----------------------------------------------------------------
.ah01:
        ; outq に溜まった残りをリングバッファへ移してから、元の
        ; AH=01h(BIOS本来のキーセンス)へそのまま素通しする。確定済み
        ; バイトはすでにリングバッファの実データなので、件数(0x528)を
        ; 見る元のキーセンスがそのまま正しく「入力あり」を返す。
        call    drain_outq_to_ring
        jmp     far [cs:oldint18]

;--------------------------------------------------------------------
; ローマ字->かな 変換テーブル (tools/gen_romaji.py の生成物)
; ハンドラ (find_exact/find_prefix) から参照されるため、常駐領域
; (install: より前) に置く必要がある。
;--------------------------------------------------------------------
%include "ROMAJI.INC"

;--------------------------------------------------------------------
; 常駐領域の終端 (常駐サイズはここまでで計算する)
;--------------------------------------------------------------------
resident_end:

%if (romaji_table_end - start) > (resident_end - start)
  %error "romaji table is outside the resident area"
%endif

;--------------------------------------------------------------------
; インストール処理 (常駐後は破棄される)
;--------------------------------------------------------------------
;--------------------------------------------------------------------
; インストール処理 (常駐後は破棄される)
;--------------------------------------------------------------------
install:
        mov     ax, IDENT_FUNC
        int     0x21
        cmp     ax, IDENT_REPLY
        jne     .install_now
        mov     dx, msg_already
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

.install_now:
        ; --- 段階D: TSUKUSHI.DIC のディスク上の位置を解決し、FDとHDDの両方に
        ; 対応する。BPB→FAT→ルートディレクトリでファイルの論理位置を
        ; 求める手順自体はFAT12/FAT16で共通(DICLOC.ASMで確立した考え方)。
        ; そのあと「その論理位置を実際にどう読むか」を5通り(FD/HDD SASI
        ; 相対 ua=00〜03)試し、辞書のマジック(FEPDIC02)が実際に出た
        ; ものを採用する(候補テーブルは下記 8) 参照)。
        ; INT 18h ハンドラの中から DOS(INT 21h)は呼べないので、この解決は
        ; インストール時にしかできない。区間数がMAXRUNS(32)を超えるほど
        ; 断片化していたら常駐せず終了する。

        ; ---- 1) カレントドライブ番号 ----
        mov     ah, 0x19                ; カレントドライブ取得 (0=A, 1=B, ...)
        int     0x21
        mov     [install_drive], al

        ; ---- 2) ブートセクタ(論理セクタ0)をINT 25hで読む ----
        mov     cx, 1
        mov     dx, 0
        mov     bx, install_bootsec_buf
        mov     al, [install_drive]
        xor     ah, ah
        int     0x25
        popf
        jc      .err_boot_read

        ; ---- 3) BPB解析 ----
        mov     ax, [install_bootsec_buf + 0x0B]
        mov     [install_bpb_bps], ax
        mov     al, [install_bootsec_buf + 0x0D]
        mov     [install_spc], al
        mov     ax, [install_bootsec_buf + 0x0E]
        mov     [install_rsvd], ax
        mov     al, [install_bootsec_buf + 0x10]
        mov     [install_nfat], al
        mov     ax, [install_bootsec_buf + 0x11]
        mov     [install_rootent], ax
        mov     ax, [install_bootsec_buf + 0x16]
        mov     [install_spf], ax
        mov     ax, [install_bootsec_buf + 0x18]
        mov     [dic_spt], ax
        mov     ax, [install_bootsec_buf + 0x1A]
        mov     [dic_heads], ax
        mov     ax, [install_bootsec_buf + 0x1C]
        mov     [install_hidden], ax
        mov     ax, [install_bootsec_buf + 0x1E]
        mov     [install_hidden + 2], ax

        ; セクタ長コード(候補1=FD専用。INT 1BhのCH用): 1024→3/512→2/256→1。
        ; これ以外のbps(HDDの論理セクタは2048等)は候補1(FD形式アクセス)
        ; を単に使えないだけとし、install全体は中止しない
        ; (HDD候補2〜5は独立して試す。「1は既に動いているので、FDの場合の
        ; 挙動を壊さないこと」を守るため、1024/512/256のときの動作は
        ; 従来の err_bps 相当のチェックも含め変えていない)。
        mov     byte [install_sector_n_valid], 0
        mov     ax, [install_bpb_bps]
        cmp     ax, 1024
        jne     .chk512
        mov     byte [install_sector_n], 3
        mov     byte [install_sector_n_valid], 1
        jmp     .n_done
.chk512:
        cmp     ax, 512
        jne     .chk256
        mov     byte [install_sector_n], 2
        mov     byte [install_sector_n_valid], 1
        jmp     .n_done
.chk256:
        cmp     ax, 256
        jne     .n_done
        mov     byte [install_sector_n], 1
        mov     byte [install_sector_n_valid], 1
.n_done:
        ; (2セクタぶんがDIC_READBUF_SIZEに収まるかは、上のbpsが1024/512/256の
        ; いずれかである時点で 2*bps<=2048 は自動的に成り立つので個別検査は不要)

        ; ---- 4) 位置計算 ----
        ; fat_start = rsvd
        mov     ax, [install_rsvd]
        mov     [install_fat_start], ax

        ; root_start = fat_start + nfat * spf
        xor     ah, ah
        mov     al, [install_nfat]
        mov     bx, ax
        mov     ax, [install_spf]
        mul     bx
        cmp     dx, 0
        jne     .err_overflow
        mov     bx, ax
        mov     ax, [install_fat_start]
        add     ax, bx
        jc      .err_overflow
        mov     [install_root_start], ax

        ; root_sects = ceil(rootent * 32 / bps)
        mov     ax, [install_rootent]
        mov     cx, 32
        mul     cx
        add     ax, [install_bpb_bps]
        adc     dx, 0
        sub     ax, 1
        sbb     dx, 0
        mov     cx, [install_bpb_bps]
        cmp     cx, 0
        je      .err_overflow
        div     cx
        mov     [install_root_sects], ax

        ; data_start = root_start + root_sects
        mov     ax, [install_root_start]
        add     ax, [install_root_sects]
        jc      .err_overflow
        mov     [install_data_start], ax

        ; ---- 4b) 総セクタ数(totsec16が0ならtotsec32を使う) ----
        mov     ax, [install_bootsec_buf + 0x13]
        or      ax, ax
        jnz     .totsec_word
        mov     ax, [install_bootsec_buf + 0x20]
        mov     dx, [install_bootsec_buf + 0x22]
        jmp     .totsec_done
.totsec_word:
        xor     dx, dx
.totsec_done:
        mov     [install_totsec], ax
        mov     [install_totsec + 2], dx

        ; ---- 4c) FAT12/FAT16判定 ----
        ; data_sectors = totsec - data_start
        ;   (data_start = rsvd + nfat*spf + root_sects なので、この1回の
        ;   引き算で「総セクタ数-予約-FAT×個数-ルート領域」を兼ねる)
        ; clusters = data_sectors / spc
        ; クラスタ数 < 4085 なら FAT12、それ以上なら FAT16 (指示どおりの式)
        mov     ax, [install_totsec]
        mov     dx, [install_totsec + 2]
        sub     ax, [install_data_start]
        sbb     dx, 0
        jc      .err_overflow
        mov     [install_data_sectors], ax
        mov     [install_data_sectors + 2], dx

        mov     ax, [install_data_sectors]
        mov     dx, [install_data_sectors + 2]
        xor     ch, ch
        mov     cl, [install_spc]
        cmp     cl, 0
        je      .err_overflow
        call    install_divide_dword32  ; dx:ax = clusters(32bit)
        mov     [install_clusters], ax
        mov     [install_clusters + 2], dx

        cmp     word [install_clusters + 2], 0
        jne     .is_fat16
        cmp     word [install_clusters], 4085
        jae     .is_fat16
        mov     byte [install_fat_type], 0
        mov     word [install_fat_term], 0x0FF8
        jmp     .fat_type_done
.is_fat16:
        mov     byte [install_fat_type], 1
        mov     word [install_fat_term], 0xFFF8
.fat_type_done:

        ; ---- 5) ルートディレクトリを1セクタずつ読みながら "TSUKUSHIDIC"
        ; を探す(ルートエントリが多いHDDでも1バッファに収めなくて済む
        ; ようにする。ディレクトリエントリ32バイトはBPBのbpsが32の倍数
        ; である限りセクタをまたがない) ----
        mov     word [install_root_sec_idx], 0
        mov     ax, [install_rootent]
        mov     [install_entries_left], ax
        mov     byte [install_file_found], 0

.root_sector_loop:
        mov     ax, [install_entries_left]
        cmp     ax, 0
        je      .search_done
        mov     ax, [install_root_sec_idx]
        cmp     ax, [install_root_sects]
        jae     .search_done

        add     ax, [install_root_start]
        mov     bx, install_root_buf
        call    install_read_sector_raw
        jc      .err_root_read

        mov     ax, [install_bpb_bps]
        mov     cl, 5
        shr     ax, cl                  ; ax = bps/32 (1セクタあたりのエントリ数)
        mov     [install_entries_in_sec], ax
        mov     si, install_root_buf
        mov     word [install_entry_in_sec], 0

.search_loop:
        mov     ax, [install_entries_left]
        cmp     ax, 0
        je      .search_done
        mov     ax, [install_entry_in_sec]
        cmp     ax, [install_entries_in_sec]
        jae     .next_sector

        mov     al, [si]
        cmp     al, 0
        je      .search_done            ; 0x00: これ以降エントリなし
        cmp     al, 0xE5
        je      .next_entry

        push    si
        mov     di, install_target_name
        mov     cx, 11
        repe    cmpsb
        pop     si
        je      .found_entry
        jmp     .next_entry

.found_entry:
        mov     ax, [si + 0x1A]
        mov     [install_file_clus], ax
        mov     byte [install_file_found], 1
        jmp     .search_done

.next_entry:
        add     si, 32
        mov     ax, [install_entry_in_sec]
        inc     ax
        mov     [install_entry_in_sec], ax
        mov     ax, [install_entries_left]
        dec     ax
        mov     [install_entries_left], ax
        jmp     .search_loop

.next_sector:
        mov     ax, [install_root_sec_idx]
        inc     ax
        mov     [install_root_sec_idx], ax
        jmp     .root_sector_loop

.search_done:
        cmp     byte [install_file_found], 0
        je      .err_not_found

        ; ---- 6) FATチェーンを辿り、連続する区間ごとにまとめる(段階E)。
        ; 断片化していても、連続する区間が MAXRUNS(=32) 個以内に収まれば
        ; 常駐する。install_get_fat_entry が install_fat_type を見て
        ; FAT12/FAT16を内部で切り替える ----
        mov     ax, [install_file_clus]
        mov     [install_cur_clus], ax
        mov     [install_run_start_clus], ax
        mov     word [install_run_len], 1
        mov     byte [install_nruns], 0
        mov     word [install_chain_count], 1
        mov     word [install_safety], INSTALL_CHAIN_SAFETY

.chain_loop:
        mov     ax, [install_cur_clus]
        call    install_get_fat_entry   ; -> ax = FATエントリ値, CF=1なら読み込み失敗
        jc      .err_fat_read
        mov     [install_next_clus], ax
        cmp     ax, [install_fat_term]
        jae     .chain_last             ; チェーン終端: 現在の区間を確定して終わる

        mov     ax, [install_cur_clus]
        inc     ax
        cmp     ax, [install_next_clus]
        je      .contig_ok

        ; 不連続: 現在の区間を確定し、次のクラスタから新しい区間を始める
        call    install_push_run
        cmp     al, 1
        je      .err_too_fragmented
        cmp     al, 2
        je      .err_overflow
        mov     ax, [install_next_clus]
        mov     [install_run_start_clus], ax
        mov     word [install_run_len], 1
        jmp     .after_step

.contig_ok:
        mov     ax, [install_run_len]
        inc     ax
        mov     [install_run_len], ax

.after_step:
        mov     ax, [install_chain_count]
        inc     ax
        mov     [install_chain_count], ax

        mov     ax, [install_next_clus]
        mov     [install_cur_clus], ax

        mov     ax, [install_safety]
        dec     ax
        mov     [install_safety], ax
        cmp     ax, 0
        je      .err_chain_runaway
        jmp     .chain_loop

.chain_last:
        ; 最後の区間を確定する
        call    install_push_run
        cmp     al, 1
        je      .err_too_fragmented
        cmp     al, 2
        je      .err_overflow

        ; ---- 8) 候補を順に試す。実際に読んで辞書のマジック(FEPDIC02)が
        ; 出た候補を採用する。決め打ちしない(環境で変わりうるため)。
        ;   候補1: FD (dic_da_ua=0x90+ドライブ番号。現行実装そのまま)
        ;   候補2〜5: HDD SASI相対 (dic_da_ua=0x00〜0x03、物理256バイト
        ;             セクタ、線形セクタ番号 = hidden + logical×(bps/256))
        ; 区間リスト(install_run_logical[]/install_run_sectors[]、計
        ; install_nruns個)を候補の単位に変換して dic_run_start[]/
        ; dic_run_sectors[]/dic_nruns へ控える。
        ; どれも当たらなければ常駐せずエラー終了する。読み込み自体は
        ; できたのにマジックが違う場合と、そもそもどの候補でも読めない
        ; 場合を区別してメッセージを分ける(install_any_read_ok) ----
        mov     byte [install_any_read_ok], 0
        mov     byte [install_cand_idx], 1
.cand_loop:
        cmp     byte [install_cand_idx], 1
        jne     .cand_setup_hdd

        ; --- 候補1: FD ---
        cmp     byte [install_sector_n_valid], 0
        je      .cand_skip              ; このBPBのbpsはFD向けの値ではない
        mov     byte [dic_kind], 0
        mov     ax, [install_bpb_bps]
        mov     [dic_bps], ax
        mov     al, [install_sector_n]
        mov     [dic_sector_n], al
        mov     al, [install_drive]
        add     al, 0x90
        mov     [dic_da_ua], al

        ; --- FDの単位は install_run_logical[]/install_run_sectors[] と
        ; 同じ(BPBの論理セクタ番号)なので、そのままword→dwordコピーする ---
        mov     cl, [install_nruns]
        mov     [dic_nruns], cl
        xor     ch, ch
        xor     si, si
        xor     di, di
.fdrun_loop:
        jcxz    .fdrun_done
        mov     ax, [install_run_logical + si]
        mov     [dic_run_start + di], ax
        mov     word [dic_run_start + di + 2], 0
        mov     ax, [install_run_sectors + si]
        mov     [dic_run_sectors + di], ax
        mov     word [dic_run_sectors + di + 2], 0
        add     si, 2
        add     di, 4
        loop    .fdrun_loop
.fdrun_done:
        jmp     .cand_try

.cand_setup_hdd:
        ; --- 候補2〜5: HDD SASI相対 (ua = cand_idx-2) ---
        mov     byte [dic_kind], 1
        mov     word [dic_bps], 256
        mov     al, [install_cand_idx]
        sub     al, 2
        mov     [dic_da_ua], al

        ; mul = bpb_bps/256 (256/512/1024/2048はすべて256の倍数)
        mov     ax, [install_bpb_bps]
        mov     cl, 8
        shr     ax, cl
        mov     [install_bps_mul], ax

        ; 各区間: start(32bit) = hidden(32bit) + install_run_logical[i]×mul
        ;         sectors(32bit) = install_run_sectors[i]×mul
        mov     cl, [install_nruns]
        mov     [dic_nruns], cl
        xor     ch, ch
        xor     si, si
        xor     di, di
.hddrun_loop:
        jcxz    .hddrun_done
        mov     ax, [install_run_logical + si]
        mul     word [install_bps_mul]  ; dx:ax = logical*mul(32bit)
        add     ax, [install_hidden]
        adc     dx, [install_hidden + 2]
        mov     [dic_run_start + di], ax
        mov     [dic_run_start + di + 2], dx
        mov     ax, [install_run_sectors + si]
        mul     word [install_bps_mul]  ; dx:ax = sectors*mul(32bit)
        mov     [dic_run_sectors + di], ax
        mov     [dic_run_sectors + di + 2], dx
        add     si, 2
        add     di, 4
        loop    .hddrun_loop
.hddrun_done:

.cand_try:
        mov     word [dic_buf_lba], 0xFFFF
        mov     word [dic_buf_lba + 2], 0xFFFF
        mov     byte [dic_buf_sectors], 0
        mov     byte [dic_read_failed], 0

        mov     ax, cs
        mov     es, ax
        mov     ax, 0
        mov     dx, 0
        mov     cx, 8
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        cmp     byte [dic_read_failed], 0
        jne     .cand_skip
        mov     byte [install_any_read_ok], 1  ; 読み込み自体は成功した

        mov     bx, di
        mov     si, install_dic_magic
        mov     cx, 8
.cand_magic_loop:
        mov     al, [dic_read_buf + bx]
        cmp     al, [si]
        jne     .cand_skip
        inc     bx
        inc     si
        loop    .cand_magic_loop
        jmp     .cand_found

.cand_skip:
        inc     byte [install_cand_idx]
        cmp     byte [install_cand_idx], 5
        jbe     .cand_loop
        cmp     byte [install_any_read_ok], 0
        jne     .err_bad_magic
        jmp     .err_all_candidates_failed

.cand_found:
        ; --- 当たった候補の設定(dic_kind/dic_bps/dic_da_ua/dic_start_lba等)
        ; はそのまま常駐データに残っている。ヘッダ(32バイト)をあらためて
        ; 読み、各フィールドを控える ---
        mov     ax, cs
        mov     es, ax
        mov     ax, 0
        mov     dx, 0
        mov     cx, 32
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        cmp     byte [dic_read_failed], 0
        jne     .err_header_read

        mov     bx, di
        add     bx, 0x14                ; data_off (u32)
        mov     ax, [dic_read_buf + bx]
        mov     [dic_data_off], ax
        mov     ax, [dic_read_buf + bx + 2]
        mov     [dic_data_off + 2], ax

        mov     bx, di
        add     bx, 0x18                ; data_len (u32)
        mov     ax, [dic_read_buf + bx]
        mov     [install_data_len], ax
        mov     ax, [dic_read_buf + bx + 2]
        mov     [install_data_len + 2], ax

        ; dic_data_end = dic_data_off + install_data_len
        mov     ax, [dic_data_off]
        add     ax, [install_data_len]
        mov     [dic_data_end], ax
        mov     ax, [dic_data_off + 2]
        adc     ax, [install_data_len + 2]
        mov     [dic_data_end + 2], ax

        mov     bx, di
        add     bx, 0x10                ; sparse_off (u32)
        mov     ax, [dic_read_buf + bx]
        mov     [dic_sparse_off], ax
        mov     ax, [dic_read_buf + bx + 2]
        mov     [dic_sparse_off + 2], ax

        mov     bx, di
        add     bx, 0x0C                ; sparse_count (u32)
        mov     ax, [dic_read_buf + bx]
        mov     [dic_sparse_count], ax
        mov     ax, [dic_read_buf + bx + 2]
        or      ax, ax
        jnz     .err_too_many_blocks    ; 疎索引が65536レコード以上は対象外

        mov     bx, di
        add     bx, 0x1C                ; sparse (u16)
        mov     ax, [dic_read_buf + bx]
        mov     [dic_sparse], ax

        ; --- 辞書ロード成功。INT 18h をフックして常駐する ---
        mov     ax, 0x3518              ; 元の INT 18h ベクタを取得
        int     0x21
        mov     [oldint18], bx
        mov     [oldint18 + 2], es

        mov     dx, newint18            ; 自前ハンドラを設定
        mov     ax, 0x2518
        int     0x21

        call    print_resident_message

        mov     dx, (resident_end - start + 0x100 + 15) / 16 ; PSPを含む常駐パラグラフ数
        mov     ax, 0x3100              ; 常駐終了
        int     0x21

.err_boot_read:
        mov     dx, msg_err_boot_read
        jmp     .abort
.err_overflow:
        mov     dx, msg_err_overflow
        jmp     .abort
.err_fat_read:
        mov     dx, msg_err_fat_read
        jmp     .abort
.err_root_read:
        mov     dx, msg_err_root_read
        jmp     .abort
.err_not_found:
        mov     dx, msg_err_not_found
        jmp     .abort
.err_chain_runaway:
        mov     dx, msg_err_chain_runaway
        jmp     .abort
.err_too_fragmented:
        mov     dx, msg_err_too_fragmented
        jmp     .abort
.err_header_read:
        mov     dx, msg_err_header_read
        jmp     .abort
.err_too_many_blocks:
        mov     dx, msg_err_too_many_blocks
        jmp     .abort
.err_bad_magic:
        mov     dx, msg_err_bad_magic
        jmp     .abort
.err_all_candidates_failed:
        mov     dx, msg_err_all_candidates_failed
        jmp     .abort

.abort:
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

;--------------------------------------------------------------------
; インストール時ディスクI/Oヘルパ (常駐後は破棄される)
;--------------------------------------------------------------------

; IN: AX = 論理セクタ番号(word。BPBの論理セクタ単位), BX = 読み込み先アドレス(DS:BX)
; OUT: CF=0で成功/CF=1で失敗(INT 25hの異常)
; 破壊: AX,CX,DX
install_read_sector_raw:
        mov     dx, ax
        mov     cx, 1
        mov     al, [install_drive]
        xor     ah, ah
        int     0x25
        popf
        ret

; 32bit除算: dx:ax(被除数) / cx(除数) -> dx:ax = 商, bx = 余り
; (DICLOC.ASM の divide_dword32 と同じ考え方)
; 破壊: AX,BX,DX
install_divide_dword32:
        mov     [install_dd_hi], dx
        mov     [install_dd_lo], ax
        mov     ax, [install_dd_hi]
        xor     dx, dx
        div     cx
        mov     [install_dd_qhi], ax
        mov     ax, [install_dd_lo]
        div     cx
        mov     bx, dx
        mov     dx, [install_dd_qhi]
        ret

; FAT領域を1〜2セクタぶんの窓(install_win_buf)にキャッシュしながら読む。
; ルートディレクトリと違い、FAT12のエントリ(3バイト/2クラスタ)はセクタ
; 境界をまたぐことがあるので2セクタぶんの窓を持つ(dict_lookupのread_dict_range
; と同じ考え方。ただしFAT/ルート領域はINT 25h、辞書本体はINT 1Bhと経路が違う
; ので別実装にした)。
; IN: DX:AX = FAT領域先頭からの絶対バイトオフセット(32bit), CX = 必要バイト数(1か2)
; OUT: CF=0のとき BX = install_win_buf内オフセット(そこからCXバイト連続で有効)
;      CF=1のとき読み込み失敗
; 破壊: AX,BX,CX,DX
install_fat_win_read:
        push    cx
        mov     bx, [install_bpb_bps]
        div     bx                      ; ax=セクタ相対番号(word), dx=セクタ内オフセット
        mov     [install_win_within], dx
        add     ax, [install_fat_start]
        mov     [install_win_target], ax

        pop     cx
        mov     ax, [install_win_within]
        add     ax, cx
        cmp     ax, [install_bpb_bps]
        jbe     .need1
        mov     byte [install_win_need2], 1
        jmp     .need_set
.need1:
        mov     byte [install_win_need2], 0
.need_set:

        mov     ax, [install_win_target]
        cmp     ax, [install_win_base]
        jne     .miss
        cmp     byte [install_win_need2], 0
        je      .hit
        cmp     byte [install_win_sectors], 2
        jae     .hit
.miss:
        mov     ax, [install_win_target]
        mov     bx, install_win_buf
        call    install_read_sector_raw
        jc      .err
        cmp     byte [install_win_need2], 0
        je      .one
        mov     ax, [install_win_target]
        inc     ax
        mov     bx, install_win_buf
        add     bx, [install_bpb_bps]
        call    install_read_sector_raw
        jc      .err
        mov     byte [install_win_sectors], 2
        jmp     .loaded
.one:
        mov     byte [install_win_sectors], 1
.loaded:
        mov     ax, [install_win_target]
        mov     [install_win_base], ax
.hit:
        mov     bx, [install_win_within]
        clc
        ret
.err:
        mov     word [install_win_base], 0xFFFF
        stc
        ret

; FATエントリ取得(install_fat_typeに応じてFAT12/FAT16を切り替える。
; DICLOC.ASM の get_fat12_entry と同じ考え方に、指示どおりFAT16(2バイト
; そのまま・終端0xFFF8以上)を追加したもの)
;   in:  ax = クラスタ番号
;   out: ax = FATエントリ値(FAT12なら12bit/FAT16なら16bit)、CF=1なら読み込み失敗
; 破壊: AX,BX,CX,DX
install_get_fat_entry:
        cmp     byte [install_fat_type], 0
        jne     .f16

        ; --- FAT12: byte_off = cluster*3/2 ---
        mov     cx, 3
        mul     cx                      ; dx:ax = cluster*3
        mov     cx, 2
        div     cx                      ; ax=byte_off(word), dx=cluster mod 2(3*c mod 2と同じ)
        mov     [install_fat_odd], dl
        xor     dx, dx                  ; byte_offの上位ワード=0
        mov     cx, 2
        call    install_fat_win_read
        jc      .fail
        mov     ax, [install_win_buf + bx]
        cmp     byte [install_fat_odd], 0
        je      .f12even
        mov     cl, 4
        shr     ax, cl
        jmp     .ok
.f12even:
        and     ax, 0x0FFF
        jmp     .ok

.f16:
        ; --- FAT16: byte_off = cluster*2 ---
        mov     cx, 2
        mul     cx                      ; dx:ax = cluster*2 (32bit。clusterが32767超でも正しい)
        mov     cx, 2
        call    install_fat_win_read
        jc      .fail
        mov     ax, [install_win_buf + bx]
.ok:
        clc
        ret
.fail:
        stc
        ret

; 段階E: 現在の区間(install_run_start_clus, install_run_len クラスタ数)を
; install_run_logical[]/install_run_sectors[](BPBの論理セクタ単位、word)
; へ install_nruns 番目の要素として追加する。
;   logical = (run_start_clus - 2) * spc + data_start
;   sectors = run_len(クラスタ数) * spc
; OUT: AL = 0(成功) / 1(区間数がMAXRUNSを超える) / 2(計算オーバーフロー)
; 破壊: AX,BX,DX
install_push_run:
        cmp     byte [install_nruns], MAXRUNS
        jae     .full
        mov     al, [install_nruns]
        xor     ah, ah
        mov     bx, ax
        add     bx, bx                  ; bx = インデックス×2 (word配列オフセット)

        mov     ax, [install_run_start_clus]
        sub     ax, 2
        mov     dl, [install_spc]
        xor     dh, dh
        mul     dx
        cmp     dx, 0
        jne     .overflow
        add     ax, [install_data_start]
        jc      .overflow
        mov     [install_run_logical + bx], ax

        mov     ax, [install_run_len]
        mov     dl, [install_spc]
        xor     dh, dh
        mul     dx
        cmp     dx, 0
        jne     .overflow
        mov     [install_run_sectors + bx], ax

        inc     byte [install_nruns]
        mov     al, 0
        ret
.full:
        mov     al, 1
        ret
.overflow:
        mov     al, 2
        ret

; どの候補で常駐したかを表示する(例: "Tsukushi FEP resident (dict: FD ua=91)."
; / "(dict: SASI ua=00)."。dic_kind/dic_da_uaは候補確定時に設定済み)
; 破壊: AX,DX
print_resident_message:
        mov     dx, msg_ok_prefix
        mov     ah, 0x09
        int     0x21

        cmp     byte [dic_kind], 0
        jne     .p_sasi
        mov     dx, msg_kind_fd
        jmp     .p_ua
.p_sasi:
        mov     dx, msg_kind_sasi
.p_ua:
        mov     ah, 0x09
        int     0x21

        mov     al, [dic_da_ua]
        call    install_print_hex8

        mov     dx, msg_ok_suffix
        mov     ah, 0x09
        int     0x21
        ret

; AL = 8bit値を16進2桁でAH=02出力(install時専用。DICLOC.ASM/HDDRD.ASMと同じ考え方)
; 破壊: なし(AX,CX,DXは呼び出し前の値に復元する)
install_print_hex8:
        push    ax
        push    cx
        push    dx
        mov     ah, al
        mov     cl, 4
        shr     al, cl
        call    .nib
        mov     al, ah
        and     al, 0x0F
        call    .nib
        pop     dx
        pop     cx
        pop     ax
        ret
.nib:
        push    ax
        cmp     al, 10
        jb      .digit
        add     al, 'A' - 10 - '0'
        jmp     .out
.digit:
        add     al, '0'
.out:
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        pop     ax
        ret

msg_ok_prefix:          db 'Tsukushi FEP resident (dict: $'
msg_kind_fd:            db 'FD ua=$'
msg_kind_sasi:          db 'SASI ua=$'
msg_ok_suffix:          db ').', 0x0D, 0x0A
                        db 'Ctrl+XFER or Ctrl-K toggles kana, Space converts, Enter commits.', 0x0D, 0x0A, '$'
msg_already:            db 'Tsukushi FEP already resident.', 0x0D, 0x0A, '$'
msg_err_boot_read:      db 'TSUKUSHI: boot sector read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_overflow:       db 'TSUKUSHI: value overflow while resolving TSUKUSHI.DIC location.', 0x0D, 0x0A, '$'
msg_err_fat_read:       db 'TSUKUSHI: FAT read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_root_read:      db 'TSUKUSHI: root directory read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_not_found:      db 'TSUKUSHI: TSUKUSHI.DIC not found on the current drive.', 0x0D, 0x0A, '$'
msg_err_chain_runaway:  db 'TSUKUSHI: FAT chain too long (possible loop).', 0x0D, 0x0A, '$'
msg_err_too_fragmented: db 'TSUKUSHI: TSUKUSHI.DIC is too fragmented (over 32 runs). Please defragment.', 0x0D, 0x0A, '$'
msg_err_header_read:    db 'TSUKUSHI: dictionary header read failed (int1Bh).', 0x0D, 0x0A, '$'
msg_err_too_many_blocks: db 'TSUKUSHI: dictionary sparse index has too many blocks (>65535).', 0x0D, 0x0A, '$'
msg_err_bad_magic:      db 'TSUKUSHI: TSUKUSHI.DIC is not a valid dictionary (bad magic).', 0x0D, 0x0A, '$'
msg_err_all_candidates_failed: db 'TSUKUSHI: Cannot read TSUKUSHI.DIC via disk BIOS.', 0x0D, 0x0A
                        db 'Supported: floppy disk, or SASI hard disk. SCSI is not supported.', 0x0D, 0x0A, '$'

; --- インストール時のみ使うデータ(常駐後は破棄される) ---
INSTALL_CHAIN_SAFETY   equ     8192    ; FATチェーンの暴走(循環等)検出用の上限反復回数

install_drive:          db      0

install_bpb_bps:         dw     0      ; BPBそのもののバイト/セクタ(候補ごとの dic_bps とは別)
install_spc:            db      0
install_rsvd:            dw     0
install_nfat:            db     0
install_rootent:         dw     0
install_spf:              dw    0
install_hidden:            dd   0      ; BPB+0x1C: hidden sectors(物理セクタ単位、u32)
install_totsec:             dd  0      ; 総セクタ数(totsec16が0ならtotsec32を使う)

install_fat_start:       dw     0
install_root_start:      dw     0
install_root_sects:      dw     0
install_data_start:      dw     0

install_data_sectors:      dd   0
install_clusters:           dd  0
install_fat_type:            db 0      ; 0=FAT12 / 1=FAT16
install_fat_term:             dw 0      ; チェーン終端しきい値(FAT12=0x0FF8/FAT16=0xFFF8)

install_sector_n:              db 0     ; FD候補用セクタ長コード(有効なのは下のvalid=1のときのみ)
install_sector_n_valid:         db 0

install_entries_left:    dw     0
install_file_found:      db     0
install_file_clus:       dw     0

install_root_sec_idx:      dw   0
install_entries_in_sec:     dw  0
install_entry_in_sec:        dw 0

install_cur_clus:        dw     0
install_next_clus:       dw     0
install_chain_count:     dw     0
install_safety:          dw     0
install_fat_odd:           db   0

; --- 段階E: FATチェーンを辿りながら集めた「連続する区間」のリスト。
; install_run_start_clus/install_run_len は走査中の現在の区間、
; install_run_logical[]/install_run_sectors[] は install_push_run で
; 確定した区間(BPBの論理セクタ単位、word。旧 install_logical と同じ単位)。
install_run_start_clus: dw     0      ; 現在の区間の開始クラスタ番号
install_run_len:        dw     0      ; 現在の区間のクラスタ数
install_nruns:           db    0      ; 確定した区間数(<=MAXRUNS)
install_run_logical:    times MAXRUNS dw 0
install_run_sectors:    times MAXRUNS dw 0

install_bps_mul:              dw 0      ; HDD候補用: bpb_bps/256
install_cand_idx:               db 0    ; 1=FD, 2〜5=HDD(ua=idx-2)
install_any_read_ok:            db 0    ; 候補のうちどれか1つでもディスク読み込み自体は成功したか

install_data_len:        dd     0

install_dd_hi:           dw     0
install_dd_lo:           dw     0
install_dd_qhi:          dw     0

install_win_base:          dw   0xFFFF ; FAT窓のロード済み先頭セクタ(未ロード=0xFFFF)
install_win_sectors:        db  0
install_win_within:          dw 0
install_win_target:           dw 0
install_win_need2:             db 0

install_dic_magic:      db      'FEPDIC02'
install_target_name:    db      'TSUKUSHIDIC'   ; 8.3の生の形(11バイト、TSUKUSHIが8文字なのでスペース埋め不要)

;--------------------------------------------------------------------
; ファイル像の末尾 (インストールコード・メッセージ・上のデータ・下記の
; バッファも含めてここまでがファイルに出力される。バッファは install:
; からのみ使い、常駐後は破棄される)
;--------------------------------------------------------------------
install_bootsec_buf:   times 2048 db 0
install_root_buf:      times 2048 db 0        ; ルートディレクトリは1セクタずつ読む(エントリはまたがない)
install_win_buf:       times 4096 db 0        ; FAT領域の2セクタぶんの窓(FAT12の境界またぎ対策)

image_end:
