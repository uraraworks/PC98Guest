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
;   辞書の位置解決(FEP.DIC がディスクのどこにあるか)は DICLOC.ASM で
;   確立した手順(BPB・FAT・ルートディレクトリを読んでFAT12クラスタ
;   チェーンを辿る)と同じ考え方をインストール時に行う(DICLOC.ASM
;   自体は変更しない。同じ手順を本ファイル内に書く)。断片化した辞書
;   ファイルは対象外とし、連続していなければ常駐せずに終了する。
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

; --- 段階C: 辞書はディスク上(FEP.DIC)に置いたまま。常駐データには
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
dic_start_lba:      dw   0      ; FEP.DIC の開始LBA(DICLOC.ASMと同じ手順で解決)
dic_da_ua:          db   0      ; INT 1Bh の DA/UA。現在のドライブ番号から
                                 ; 0x90+ドライブ番号 で作る(FD前提の暫定。
                                 ; HDD等では別のDA/UA体系になるはずで未対応)
dic_bps:            dw   0      ; BPBのバイト/セクタ
dic_spt:            dw   0      ; BPBのセクタ/トラック
dic_heads:          dw   0      ; BPBのヘッド数
dic_sector_n:       db   0      ; INT 1BhのCH用セクタ長コード(1024→3/512→2/256→1)

; 1〜2セクタ読み込みキャッシュ(直近に読んだセクタを覚えておき、同じ
; セクタへの再アクセスでは INT 1Bh を呼び直さない)
dic_buf_lba:        dw   0xFFFF ; ロード済みの先頭LBA(0xFFFFは「未ロード」の印)
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
dic_target_lba:     dw   0
dic_need2:          db   0

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

; kanabuf(kanalen バイト)をそのまま先頭から outq へ積み、kanabuf を空にする。
push_kanabuf_to_outq:
        push    ax
        push    cx
        push    si
        mov     cl, [cs:kanalen]
        xor     ch, ch
        mov     si, kanabuf
.loop:
        jcxz    .done
        mov     al, [cs:si]
        call    outq_push
        inc     si
        loop    .loop
.done:
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        pop     si
        pop     cx
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
;   - 辞書はディスク上(FEP.DIC, v2/FEPDIC02形式)にあり、INT 1Bh で
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
        cmp     byte [cs:status_col], STATUSCOLS
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
; 段階C: LBA→CHS変換・ディスク読み込みヘルパ
;--------------------------------------------------------------------

; IN: AX = LBA(word)
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

; IN: DX:AX = 辞書ファイル先頭からの絶対バイトオフセット(32bit), CX = 必要バイト数
;     (CX は dic_bps を超えないこと。呼び出し側はすべてこの前提で使う)
; OUT: DI = dic_read_buf 内のオフセット。そこから CX バイト連続で有効データがある
; 前提: 呼び出し側が ES=CS を設定済みであること(INT 1Bh の ES:BP用)
; 破壊: AX,BX,CX,DX,SI,DI,BP
read_dict_range:
        push    cx
        mov     bx, [cs:dic_bps]
        div     bx                      ; ax = lba相対(オフセット/bps), dx = セクタ内オフセット
        mov     [cs:dic_within0], dx
        add     ax, [cs:dic_start_lba]
        mov     [cs:dic_target_lba], ax

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

        ; --- キャッシュ判定 ---
        mov     ax, [cs:dic_target_lba]
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

        mov     ax, [cs:dic_target_lba]
        call    lba_to_chs              ; -> cl=cyl, dh=head, dl=sector
        mov     ch, [cs:dic_sector_n]
        mov     al, [cs:dic_da_ua]
        mov     ah, 0x76                ; MF1/f0/SEEK1/コマンド6(読み出し)。実測(fep-measure-06)で確定
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

        mov     byte [cs:dic_buf_sectors], 2
        jmp     .read_ok
.one_sector:
        mov     byte [cs:dic_buf_sectors], 1
.read_ok:
        mov     ax, [cs:dic_target_lba]
        mov     [cs:dic_buf_lba], ax
        jmp     .do_read_ret
.read_err:
        ; 読み取り失敗: キャッシュを無効化し、以後 dict_lookup 側で
        ; dic_read_failed を見て notfound 扱いにする(ハンドラ内で
        ; エラーメッセージを出す手段が無いため。未検証の異常系)。
        mov     word [cs:dic_buf_lba], 0xFFFF
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

; kanabuf(kanalen バイト)をよみとして、ディスク上の辞書(FEP.DIC, v2形式)を
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
        cmp     byte [cs:status_col], STATUSCOLS
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
        cmp     byte [cs:status_col], STATUSCOLS
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
        cmp     al, STATUSCOLS
        ja      .cand_loop2_done
        mov     bl, [cs:si]
        mov     bh, [cs:si+1]
        call    vram_put_wide
        add     si, 2
        sub     cx, 2
        add     byte [cs:status_col], 2
        jmp     .cand_loop2
.cand_loop2_done:

        cmp     byte [cs:status_col], STATUSCOLS
        jae     .content_done
        mov     al, ' '
        call    vram_put_ascii
        inc     byte [cs:status_col]

        cmp     byte [cs:status_col], STATUSCOLS
        jae     .content_done
        mov     al, '('
        call    vram_put_ascii
        inc     byte [cs:status_col]

        mov     al, [cs:cand_index]
        inc     al
        call    vram_put_decimal

        cmp     byte [cs:status_col], STATUSCOLS
        jae     .content_done
        mov     al, '/'
        call    vram_put_ascii
        inc     byte [cs:status_col]

        mov     al, [cs:cand_count]
        call    vram_put_decimal

        cmp     byte [cs:status_col], STATUSCOLS
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
        cmp     byte [cs:status_col], STATUSCOLS
        jae     .kb_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        inc     byte [cs:status_col]
        jmp     .kb_next
.kb_wide:
        mov     al, [cs:status_col]
        add     al, 2
        cmp     al, STATUSCOLS
        ja      .kb_done
        mov     bl, [cs:si]             ; bl = SJIS上位バイト(b1)
        mov     bh, [cs:si+1]           ; bh = SJIS下位バイト(b2)
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
        cmp     byte [cs:status_col], STATUSCOLS
        jae     .content_done
        mov     al, [cs:si]
        call    vram_put_ascii
        inc     si
        inc     byte [cs:status_col]
        loop    .rb_loop

.content_done:
        mov     al, STATUSCOLS
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
        call    restore_status
        jmp     .ah00_after_status

.ah00_do_space:
        cmp     byte [cs:cand_count], 0
        jne     .ah00_space_next
        ; 変換中でない: Spaceの前に未確定ローマ字を確定処理してから辞書引きする
        ; (n/nnの取りこぼし対策。FEP.ASMで実績のある不具合と同じ理由)
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
        ; --- 段階C: FEP.DIC のディスク上の位置を解決する ---
        ; DICLOC.ASM で確立した手順(BPB→FAT→ルートディレクトリ→FAT12
        ; チェーン)と同じ考え方をここで行う(DICLOC.ASM 自体は変更しない。
        ; 同じ手順を本ファイル内に独立して書く)。INT 18h ハンドラの中から
        ; DOS(INT 21h)は呼べないので、この解決はインストール時にしか
        ; できない。断片化していたら常駐せず終了する。

        ; ---- 1) カレントドライブ番号 ----
        mov     ah, 0x19                ; カレントドライブ取得 (0=A, 1=B, ...)
        int     0x21
        mov     [install_drive], al

        ; DA/UA = 0x90 + ドライブ番号。FD専用の暫定(下記コメント参照)。
        ; INT 1Bh の DA/UA は「1MBインターフェース・ユニット番号」であり、
        ; HDD等では別の体系になるはずだが未対応・未検証(このTSUKUSHIは
        ; FD起動を前提にした暫定実装であることをここに明記する)。
        mov     al, [install_drive]
        add     al, 0x90
        mov     [dic_da_ua], al

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
        mov     [dic_bps], ax
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

        ; セクタ長コード(INT 1Bh の CH用): 1024→3 / 512→2 / 256→1。
        ; ハードコードせず BPB のバイト/セクタから求める。それ以外の値は
        ; このTSUKUSHIでは対応していないので常駐せず終了する。
        mov     ax, [dic_bps]
        cmp     ax, 1024
        jne     .chk512
        mov     byte [dic_sector_n], 3
        jmp     .n_done
.chk512:
        cmp     ax, 512
        jne     .chk256
        mov     byte [dic_sector_n], 2
        jmp     .n_done
.chk256:
        cmp     ax, 256
        jne     .err_bps
        mov     byte [dic_sector_n], 1
.n_done:
        ; 2セクタぶんの読み込みバッファ(DIC_READBUF_SIZE)に収まるか確認
        mov     ax, [dic_bps]
        add     ax, ax                  ; ax = 2*bps
        cmp     ax, DIC_READBUF_SIZE
        ja      .err_bps

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
        add     ax, [dic_bps]
        adc     dx, 0
        sub     ax, 1
        sbb     dx, 0
        mov     cx, [dic_bps]
        cmp     cx, 0
        je      .err_overflow
        div     cx
        mov     [install_root_sects], ax

        ; data_start = root_start + root_sects
        mov     ax, [install_root_start]
        add     ax, [install_root_sects]
        jc      .err_overflow
        mov     [install_data_start], ax

        ; ---- 5a) FAT全体を読む ----
        mov     ax, [install_spf]
        mov     cx, [dic_bps]
        mul     cx
        cmp     dx, 0
        jne     .err_fat_too_big
        cmp     ax, INSTALL_FAT_BUF_SIZE
        ja      .err_fat_too_big

        mov     cx, [install_spf]
        mov     dx, [install_fat_start]
        mov     bx, install_fat_buf
        mov     al, [install_drive]
        xor     ah, ah
        int     0x25
        popf
        jc      .err_fat_read

        ; ---- 5b) ルートディレクトリ全体を読む ----
        mov     ax, [install_root_sects]
        mov     cx, [dic_bps]
        mul     cx
        cmp     dx, 0
        jne     .err_root_too_big
        cmp     ax, INSTALL_ROOT_BUF_SIZE
        ja      .err_root_too_big

        mov     cx, [install_root_sects]
        mov     dx, [install_root_start]
        mov     bx, install_root_buf
        mov     al, [install_drive]
        xor     ah, ah
        int     0x25
        popf
        jc      .err_root_read

        ; ---- 5c) "FEP     DIC" を探す ----
        mov     ax, [install_rootent]
        mov     [install_entries_left], ax
        mov     si, install_root_buf
        mov     byte [install_file_found], 0

.search_loop:
        mov     ax, [install_entries_left]
        cmp     ax, 0
        je      .search_done
        mov     al, [si]
        cmp     al, 0
        je      .search_done
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
        mov     ax, [install_entries_left]
        dec     ax
        mov     [install_entries_left], ax
        jmp     .search_loop

.search_done:
        cmp     byte [install_file_found], 0
        je      .err_not_found

        ; ---- 6) FAT12チェーンを辿り、連続しているか確認する ----
        mov     ax, [install_file_clus]
        mov     [install_cur_clus], ax
        mov     word [install_chain_count], 1
        mov     byte [install_chain_contig], 1
        mov     word [install_safety], INSTALL_CHAIN_SAFETY

.chain_loop:
        mov     ax, [install_cur_clus]
        call    install_get_fat12_entry ; -> ax = FATエントリ値(12bit)
        mov     [install_next_clus], ax
        cmp     ax, 0x0FF8
        jae     .chain_done

        mov     ax, [install_cur_clus]
        inc     ax
        cmp     ax, [install_next_clus]
        je      .contig_ok
        mov     byte [install_chain_contig], 0
.contig_ok:
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

.chain_done:
        cmp     byte [install_chain_contig], 0
        je      .err_fragmented         ; 断片化した辞書は今回対象外

        ; ---- 7) 開始LBAを計算する ----
        mov     ax, [install_file_clus]
        sub     ax, 2
        mov     bl, [install_spc]
        xor     bh, bh
        mul     bx
        cmp     dx, 0
        jne     .err_overflow
        add     ax, [install_data_start]
        jc      .err_overflow
        mov     [dic_start_lba], ax

        ; ---- ヘッダ(32バイト)を INT 1Bh 経由で読み、マジックを確認する ----
        ; (read_dict_range は常駐ルーチンだが、install: からもそのまま
        ; 呼べる。dic_start_lba/dic_bps/dic_spt/dic_heads/dic_da_ua/
        ; dic_sector_n は上でセット済み)
        mov     ax, cs
        mov     es, ax
        mov     ax, 0
        mov     dx, 0
        mov     cx, 32
        call    read_dict_range         ; -> di = dic_read_buf内オフセット
        cmp     byte [cs:dic_read_failed], 0
        jne     .err_header_read

        mov     bx, di
        mov     si, install_dic_magic
        mov     cx, 8
.magic_loop:
        mov     al, [cs:dic_read_buf + bx]
        cmp     al, [si]
        jne     .err_magic
        inc     bx
        inc     si
        loop    .magic_loop

        ; --- ヘッダの各フィールドを常駐データへ控える ---
        mov     bx, di
        add     bx, 0x14                ; data_off (u32)
        mov     ax, [cs:dic_read_buf + bx]
        mov     [dic_data_off], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        mov     [dic_data_off + 2], ax

        mov     bx, di
        add     bx, 0x18                ; data_len (u32)
        mov     ax, [cs:dic_read_buf + bx]
        mov     [install_data_len], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
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
        mov     ax, [cs:dic_read_buf + bx]
        mov     [dic_sparse_off], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        mov     [dic_sparse_off + 2], ax

        mov     bx, di
        add     bx, 0x0C                ; sparse_count (u32)
        mov     ax, [cs:dic_read_buf + bx]
        mov     [dic_sparse_count], ax
        mov     ax, [cs:dic_read_buf + bx + 2]
        or      ax, ax
        jnz     .err_too_many_blocks    ; 疎索引が65536レコード以上は対象外

        mov     bx, di
        add     bx, 0x1C                ; sparse (u16)
        mov     ax, [cs:dic_read_buf + bx]
        mov     [dic_sparse], ax

        ; --- 辞書ロード成功。INT 18h をフックして常駐する ---
        mov     ax, 0x3518              ; 元の INT 18h ベクタを取得
        int     0x21
        mov     [oldint18], bx
        mov     [oldint18 + 2], es

        mov     dx, newint18            ; 自前ハンドラを設定
        mov     ax, 0x2518
        int     0x21

        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     dx, (resident_end - start + 0x100 + 15) / 16 ; PSPを含む常駐パラグラフ数
        mov     ax, 0x3100              ; 常駐終了
        int     0x21

.err_boot_read:
        mov     dx, msg_err_boot_read
        jmp     .abort
.err_bps:
        mov     dx, msg_err_bps
        jmp     .abort
.err_overflow:
        mov     dx, msg_err_overflow
        jmp     .abort
.err_fat_too_big:
        mov     dx, msg_err_fat_too_big
        jmp     .abort
.err_fat_read:
        mov     dx, msg_err_fat_read
        jmp     .abort
.err_root_too_big:
        mov     dx, msg_err_root_too_big
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
.err_fragmented:
        mov     dx, msg_err_fragmented
        jmp     .abort
.err_header_read:
        mov     dx, msg_err_header_read
        jmp     .abort
.err_magic:
        mov     dx, msg_err_magic
        jmp     .abort
.err_too_many_blocks:
        mov     dx, msg_err_too_many_blocks
        jmp     .abort

.abort:
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

;--------------------------------------------------------------------
; FAT12エントリ取得 (install時のみ使用。DICLOC.ASM の get_fat12_entry と
; 同じ考え方を本ファイル内に独立して書いたもの)
;   in:  ax = クラスタ番号
;   out: ax = 12bit FATエントリ値
;--------------------------------------------------------------------
install_get_fat12_entry:
        push    cx
        push    dx
        push    bx
        mov     cx, 3
        mul     cx                      ; dx:ax = cluster*3
        mov     cx, 2
        div     cx                      ; ax = offset, dx = cluster mod 2
        mov     bx, ax
        mov     ax, [install_fat_buf + bx]
        cmp     dx, 0
        je      .even
        mov     cx, 4
        shr     ax, cl
        jmp     .done
.even:
        and     ax, 0x0FFF
.done:
        pop     bx
        pop     dx
        pop     cx
        ret

msg_ok:                 db 'Tsukushi FEP resident (dictionary on disk).', 0x0D, 0x0A
                        db 'Ctrl+XFER or Ctrl-K toggles kana, Space converts, Enter commits.', 0x0D, 0x0A, '$'
msg_already:            db 'Tsukushi FEP already resident.', 0x0D, 0x0A, '$'
msg_err_boot_read:      db 'TSUKUSHI: boot sector read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_bps:            db 'TSUKUSHI: unsupported bytes/sector (must be 1024, 512 or 256).', 0x0D, 0x0A, '$'
msg_err_overflow:       db 'TSUKUSHI: value overflow while resolving FEP.DIC location.', 0x0D, 0x0A, '$'
msg_err_fat_too_big:    db 'TSUKUSHI: FAT does not fit in the install-time buffer.', 0x0D, 0x0A, '$'
msg_err_fat_read:       db 'TSUKUSHI: FAT read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_root_too_big:   db 'TSUKUSHI: root directory does not fit in the install-time buffer.', 0x0D, 0x0A, '$'
msg_err_root_read:      db 'TSUKUSHI: root directory read failed (int25h).', 0x0D, 0x0A, '$'
msg_err_not_found:      db 'TSUKUSHI: FEP.DIC not found in root directory.', 0x0D, 0x0A, '$'
msg_err_chain_runaway:  db 'TSUKUSHI: FAT chain too long (possible loop).', 0x0D, 0x0A, '$'
msg_err_fragmented:     db 'TSUKUSHI: FEP.DIC is fragmented (not defragmented). Defragment the disk and try again.', 0x0D, 0x0A, '$'
msg_err_header_read:    db 'TSUKUSHI: dictionary header read failed (int1Bh).', 0x0D, 0x0A, '$'
msg_err_magic:          db 'TSUKUSHI: dictionary magic mismatch (not a FEPDIC02 file).', 0x0D, 0x0A, '$'
msg_err_too_many_blocks: db 'TSUKUSHI: dictionary sparse index has too many blocks (>65535).', 0x0D, 0x0A, '$'

; --- インストール時のみ使うデータ(常駐後は破棄される) ---
INSTALL_FAT_BUF_SIZE   equ     4096    ; FATがこのバイト数に収まらなければエラー終了
INSTALL_ROOT_BUF_SIZE  equ     8192    ; ルートディレクトリがこのバイト数に収まらなければエラー終了
INSTALL_CHAIN_SAFETY   equ     8192    ; FATチェーンの暴走(循環等)検出用の上限反復回数

install_drive:          db      0

install_spc:            db      0
install_rsvd:            dw     0
install_nfat:            db     0
install_rootent:         dw     0
install_spf:              dw    0

install_fat_start:       dw     0
install_root_start:      dw     0
install_root_sects:      dw     0
install_data_start:      dw     0

install_entries_left:    dw     0
install_file_found:      db     0
install_file_clus:       dw     0

install_cur_clus:        dw     0
install_next_clus:       dw     0
install_chain_count:     dw     0
install_chain_contig:    db     0
install_safety:          dw     0

install_data_len:        dd     0

install_dic_magic:      db      'FEPDIC02'
install_target_name:    db      'FEP     DIC'   ; 8.3の生の形(11バイト、スペース埋め)

;--------------------------------------------------------------------
; ファイル像の末尾 (インストールコード・メッセージ・上のデータ・下記の
; バッファも含めてここまでがファイルに出力される。バッファは install:
; からのみ使い、常駐後は破棄される)
;--------------------------------------------------------------------
install_bootsec_buf:   times 2048 db 0
install_fat_buf:       times INSTALL_FAT_BUF_SIZE db 0
install_root_buf:      times INSTALL_ROOT_BUF_SIZE db 0

image_end:
