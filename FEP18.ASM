; FEP18.COM - PC-98 / MS-DOS 用 常駐ローマ字かな変換 FEP (段階A: 辞書なし)
;
; 目的:
;   INT 18h(キーボードBIOS) の AH=00h(キー入力待ち)/AH=01h(キーセンス) を
;   フックし、ハンドラ内で完結するローマ字->ひらがな変換を行う。
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
; ビルド: nasm -f bin FEP18.ASM -o FEP18.COM

        cpu     8086
        org     0x100

IDENT_FUNC      equ     0xE7EC
IDENT_REPLY     equ     0x4638

ROMABUFSIZE     equ     8
KANABUFSIZE     equ     64
OUTQSIZE        equ     64
STATUSCOLS      equ     80             ; ステータス行の埋め幅(桁=画面上のカラム数)

; --- キーボードBIOSの作業領域 (物理アドレス。セグメント0000h) ---
; CON ドライバは INT 18h AH=00h を呼ぶ前に 0x528(件数) を見て「入力が
; あるか」を判断している。FEP18 の outq は自前バッファなので 0x528 が
; 増えず、CON が待ち続けてしまう(実測で確定)。確定したバイトはここへ
; 直接積む。レイアウトは NP2kai sdl/webnp2api.c の
; webnp2_push_key_buffer() と同一 (bios/bios09.c 準拠)。
KBSEG           equ     0x0000
KBBUF_START     equ     0x502
KBBUF_END       equ     0x522          ; 0x502-0x521 が実データ(排他上限)
KBCOUNT         equ     0x528
KBTAIL          equ     0x526

; FEP18 が積んだバイトの印。AH=00h ハンドラが oldint18 から読み戻した
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

; --- 確定済みバイトの待ち行列 (AH=00h/01h がここから返す) ---
outq:           times   OUTQSIZE db 0
outq_head:      db      0
outq_tail:      db      0
outq_count:     db      0

; --- ステータス行(25行目)の退避領域。文字160+属性160=320バイト。
; FEP18は常駐したままなので今回は復元しないが、将来の常駐解除に備えて
; 退避だけは行う(仕様指定通り)。
status_save:    times   320 db 0
status_col:     db      0       ; draw_status 作業用: 使用済み桁数

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
        cmp     al, 0x00
        je      .ah00_direct_return     ; 文字コードを持たないキー(カーソル等)は殺さない

        ; --- それ以外: ローマ字処理へ ---
        cmp     al, 'A'
        jb      .ah00_rj_noconv
        cmp     al, 'Z'
        ja      .ah00_rj_noconv
        add     al, 0x20                ; 大文字は小文字化する(FEP.ASMと同じ)
.ah00_rj_noconv:
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
        xor     byte [cs:mode], 1
        jmp     .ah00_redraw_continue

.ah00_do_cr:
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
        mov     byte [cs:romalen], 0
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        jmp     .ah00_redraw_continue

.ah00_direct_return:
        jmp     .ah00_exit

.ah00_redraw_continue:
        call    draw_status
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

msg_ok:         db      'FEP18 resident. Ctrl+XFER or Ctrl-K toggles kana.', 0x0D, 0x0A, '$'
msg_already:    db      'FEP18 already resident.', 0x0D, 0x0A, '$'
