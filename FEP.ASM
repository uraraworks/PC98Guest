; FEP.COM - PC-98 / MS-DOS 用 常駐ローマ字かな漢字変換 FEP v1
;
; 目的:
;   INT 21h の AH=0Ah(行入力)をフックし、ハンドラ内で自前のローマ字->かな
;   ->辞書変換つき行入力を行う。
;
;   常駐・INT 21hフックの作法は ESCT2.ASM をそのまま踏襲する
;   (ハンドラ内では DS を CS に張り替え、旧INT21hは pushf + call far で呼ぶ)。
;   行入力の自前実装(BS処理・溢れ処理・CR/LFの後始末)は
;   WebNP2/tools/paste-tsr/paste.asm の .buffered を踏襲する。
;
;   v0 との違い: v0 は変換したかなを即 lbuf(行バッファ)へ積んで画面に
;   エコーしていた。v1 では未確定のかなを kanabuf に貯めてステータス行
;   にだけ表示し、確定して初めて lbuf に積む。加えて Space で kanabuf を
;   辞書引きし、候補を切り替えて確定できる(かな漢字変換)。
;
;   辞書検索(二分探索・DOSファイルI/O)のアルゴリズムは DICTEST.ASM で
;   実機動作確認済みのものをそのまま流用している(DICTEST.ASM 自体は
;   変更しない)。ただしハンドラ内から呼ぶため、DICTEST.ASM の
;   `int 0x21` 直呼びはすべて `pushf`+`call far [cs:oldint21]` に置き換え、
;   ES に依存する `rep movsb`/`rep cmpsb` の類は使わず、CS: 明示の
;   バイト単位ループに書き換えている(ハンドラ内では ES が呼び出し元の
;   セグメントのままのことがあるため)。
;
; 多重常駐チェック: AX=E7E9h で呼ぶと AX=5046h('FP') を返す。
;
; ビルド: nasm -f bin FEP.ASM -o FEP.COM

        cpu     8086
        org     0x100

ESC             equ     0x1B
IDENT_FUNC      equ     0xE7E9
IDENT_REPLY     equ     0x5046

LBUFSIZE        equ     128
ROMABUFSIZE     equ     8
KANABUFSIZE     equ     64
CANDBUFSIZE     equ     128
CANDMAX         equ     16
STATUSWIDTH     equ     60      ; ステータス行の埋め幅(桁=画面上のカラム数)

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint21:       dd      0

callerseg:      dw      0       ; AH=0Ah呼び出し元の DS (行入力バッファのセグメント)
calleroff:      dw      0       ; 同 DX (行入力バッファのオフセット)
roomremaining:  db      0       ; lbuf に積める最大バイト数 (呼び出し元maxlen-1と127の小さい方)

mode:           db      0       ; 0=直接入力, 1=かな入力

romabuf:        times   ROMABUFSIZE db 0
romalen:        db      0

lbuf:           times   LBUFSIZE db 0
lwid:           times   LBUFSIZE db 0
llen:           db      0       ; lbuf の使用バイト数
lcnt:           db      0       ; 文字数 (lwid の使用個数)

; try_append の作業用スクラッチ
ta_count:       db      0
ta_wcount:      db      0
ta_wval:        db      0
ta_oldlen:      db      0

scratchch:      db      0       ; try_append_ascii / kana_append_ascii が
                                 ; 1バイトを指すために使う

; 「ん」「っ」の Shift_JIS バイト列 (テーブル検索を経ない特殊確定用)
sound_n:        db      0x82, 0xF1      ; "ん"
sound_sokuon:   db      0x82, 0xC1      ; "っ"

; --- v1: 未確定かなバッファ(ステータス行にのみ表示) ---
kanabuf:        times   KANABUFSIZE db 0
kanawid:        times   KANABUFSIZE db 0
kanalen:        db      0       ; kanabuf の使用バイト数
kanacnt:        db      0       ; 文字数 (kanawid の使用個数)

; kana_append の作業用スクラッチ
ka_count:       db      0
ka_wcount:      db      0
ka_wval:        db      0

; --- v1: 変換候補の状態 ---
cand_count:     db      0       ; 0 = 変換していない
cand_index:     db      0       ; 0起点
cand_buf:       times   CANDBUFSIZE db 0
cand_off:       times   CANDMAX dw 0    ; 各候補の cand_buf 内オフセット
cand_len:       times   CANDMAX db 0    ; 各候補のバイト数

; --- v1: 辞書 ---
dic_path:       db      'X:\FEP.DIC', 0 ; 先頭の 'X' はインストール時に実ドライブへ書き換える
dic_handle:     dw      0
dic_magic:      db      'FEPDIC01'
dic_count:      dw      0
dic_index_off:  dw      0

yomi_field:     times   16 db 0
header_buf:     times   16 db 0
entry_buf:      times   20 db 0
small_buf:      db      0

search_lo:      dw      0
search_hi:      dw      0
search_mid:     dw      0

cand_read_i:    db      0
cand_cur_len:   db      0
cb_pos:         dw      0

pd_digits:      db      0       ; print_decimal_status の作業用

;--------------------------------------------------------------------
; 内部ヘルパー (ハンドラ内から旧INT 21hを呼ぶ。int 21hは絶対に使わない)
;--------------------------------------------------------------------

; DS:DX = $終端文字列を AH=09h で出力する。
put_str:
        push    ax
        mov     ah, 0x09
        pushf
        call    far [cs:oldint21]
        pop     ax
        ret

; AL = 1バイトを AH=02h で出力する。
put_char:
        push    ax
        push    dx
        mov     dl, al
        mov     ah, 0x02
        pushf
        call    far [cs:oldint21]
        pop     dx
        pop     ax
        ret

; AL = 0-255 の値を10進数で put_char により表示する。CL = 印字した桁数を返す。
print_decimal_status:
        push    ax
        push    bx
        push    dx
        xor     ah, ah
        mov     bx, 10
        xor     cx, cx
.pd_div:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .pd_div
        mov     [cs:pd_digits], cl
.pd_print:
        pop     dx
        mov     al, dl
        add     al, '0'
        call    put_char
        loop    .pd_print
        mov     cl, [cs:pd_digits]
        pop     dx
        pop     bx
        pop     ax
        ret

; AL = バイト値。Shift_JIS の先頭バイト(0x81-0x9F または 0xE0-0xFC)なら
; AL=2 で CF=1、そうでなければ AL=1 で CF=0 を返す。
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
; ROMAJI.INC テーブル検索
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
        ; CL バイト比較
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
        ; 完全一致
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
        add     si, ax                  ; ローマ字バイト分を進める(alは温存されている)
        add     si, dx                  ; かなバイト分を進める
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
        jbe     .skip                   ; 自分以下の長さは prefix 候補にならない
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
        ; prefix 一致
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
; lbuf/lwid への確定出力 (溢れたら黙って捨てる)
;--------------------------------------------------------------------

; SI = 追加するバイト列 (cs segment)、CL = バイト数、
; DH = lwid に積む個数、DL = その値(幅)。
try_append:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     al, [cs:llen]
        xor     ah, ah
        mov     bl, cl
        xor     bh, bh
        add     ax, bx
        mov     bl, [cs:roomremaining]
        xor     bh, bh
        cmp     ax, bx
        ja      .full

        mov     [cs:ta_count], cl
        mov     [cs:ta_wcount], dh
        mov     [cs:ta_wval], dl

        mov     al, [cs:llen]
        mov     [cs:ta_oldlen], al
        xor     ah, ah
        mov     di, lbuf
        add     di, ax
.copyloop:
        jcxz    .copydone
        mov     al, [cs:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .copyloop
.copydone:

        mov     al, [cs:lcnt]
        xor     ah, ah
        mov     di, lwid
        add     di, ax
        mov     cl, [cs:ta_wcount]
        xor     ch, ch
        mov     al, [cs:ta_wval]
.widloop:
        jcxz    .widdone
        mov     [cs:di], al
        inc     di
        loop    .widloop
.widdone:

        mov     al, [cs:llen]
        add     al, [cs:ta_count]
        mov     [cs:llen], al
        mov     al, [cs:lcnt]
        add     al, [cs:ta_wcount]
        mov     [cs:lcnt], al

        ; いま追加したバイトをエコー
        mov     si, lbuf
        mov     al, [cs:ta_oldlen]
        xor     ah, ah
        add     si, ax
        mov     cl, [cs:ta_count]
        xor     ch, ch
.echoloop:
        jcxz    .full
        mov     al, [cs:si]
        call    put_char
        inc     si
        loop    .echoloop

.full:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; AL = 1バイト(ASCII、幅1)を追加する。
try_append_ascii:
        push    si
        push    cx
        push    dx
        mov     [cs:scratchch], al
        mov     si, scratchch
        mov     cl, 1
        mov     dh, 1
        mov     dl, 1
        call    try_append
        pop     dx
        pop     cx
        pop     si
        ret

;--------------------------------------------------------------------
; kanabuf/kanawid への確定出力 (v1: 未確定かなバッファ。エコーはしない。
; try_append と同じ考え方で、対象と上限を kanabuf/KANABUFSIZE に変えただけ)
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
; romabuf の先頭 BL バイトを捨てて詰める (romalen -= BL)
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
; ローマ字->かな 変換本体 (romabuf に1文字足した直後に呼ぶ。確定先は kanabuf)
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
        ; buf が "nn" で始まる
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

        ; 完全一致でも prefix でもない
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
        ; SI = かなバイト列, AL = かなバイト数 (find_exact の戻り値)
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
; v1: 確定処理 (「確定」= バイト列を lbuf に積み、lwid に幅を記録し、
; AH=02h でエコーし、状態をクリアする)
;--------------------------------------------------------------------

; kanabuf(未確定のかな)をそのまま lbuf へ確定する。kanawid の幅列を
; そのまま使って1グリフずつ try_append する(幅混在に対応するため)。
confirm_kanabuf:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        xor     bx, bx                  ; bx = kanabuf 内バイトオフセット
        xor     di, di                  ; di = kanawid 内インデックス(0起点)
        mov     al, [cs:kanacnt]
        xor     ah, ah
        mov     dx, ax                  ; dx = ループ上限(文字数)
.loop:
        cmp     di, dx
        jae     .done
        mov     al, [cs:kanawid + di]   ; al = この1グリフの幅(1 or 2)
        mov     si, kanabuf
        add     si, bx
        mov     cl, al
        xor     ch, ch
        push    dx
        mov     dh, 1
        mov     dl, al
        call    try_append
        pop     dx
        add     bx, cx
        inc     di
        jmp     .loop
.done:
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; 現在の候補(cand_buf の cand_off[cand_index] から cand_len[cand_index]
; バイト)を lbuf へ確定する。バイト列に事前の幅情報が無いため、
; Shift_JIS 先頭バイト判定(sjis_width)で1文字ずつ幅を決めながら積む。
confirm_candidate:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     al, [cs:cand_index]
        xor     ah, ah
        mov     di, ax
        mov     si, di
        add     si, si                  ; si = index*2 (cand_off は word 配列)
        mov     bx, [cs:cand_off + si]
        mov     si, cand_buf
        add     si, bx                  ; si -> 候補バイト列先頭
        mov     al, [cs:cand_len + di]
        xor     ah, ah
        mov     dx, ax                  ; dx = 残りバイト数(ループ限界)

.loop:
        or      dx, dx
        jz      .done
        mov     al, [cs:si]
        call    sjis_width              ; al = 1 or 2 (幅)
        mov     bl, al
        xor     bh, bh                  ; bx = この文字の幅

        push    dx
        mov     cl, bl
        xor     ch, ch
        mov     dh, 1
        mov     dl, bl
        call    try_append
        pop     dx

        add     si, bx
        sub     dx, bx
        jmp     .loop
.done:
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; 変換を取り消して kanabuf 編集状態に戻る(kanabuf 自体はそのまま残す)。
cancel_conversion:
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0
        ret

;--------------------------------------------------------------------
; v1: 辞書引き (DICTEST.ASM の二分探索・ファイルI/Oを流用。
; ただしハンドラ内なので int 21h は使わず pushf+call far [cs:oldint21]、
; ES 依存の文字列命令(movsb/cmpsb)は使わず CS: 明示のバイトループにする)
;
; kanabuf(kanalen バイト)をよみとして FEP.DIC を引く。
; 見つかれば CF=1 で cand_count/cand_index/cand_buf/cand_off/cand_len を
; 設定する(cand_index=0)。見つからない・開けない・壊れている場合は
; CF=0 で cand_count=0 のまま(=変換を開始しない)。
;--------------------------------------------------------------------
dict_lookup:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     al, [cs:kanalen]
        cmp     al, 16
        ja      .notfound_noclose

        ; --- よみフィールド(16バイト、0埋め)を作る ---
        mov     di, yomi_field
        mov     cx, 16
.zero_loop:
        mov     byte [cs:di], 0
        inc     di
        loop    .zero_loop

        mov     al, [cs:kanalen]
        mov     cl, al
        xor     ch, ch
        mov     si, kanabuf
        mov     di, yomi_field
.copy_loop:
        jcxz    .copy_done
        mov     al, [cs:si]
        mov     [cs:di], al
        inc     si
        inc     di
        loop    .copy_loop
.copy_done:

        ; --- FEP.DIC を開く ---
        mov     dx, dic_path
        mov     ax, 0x3D00              ; open, read-only
        pushf
        call    far [cs:oldint21]
        jc      .notfound_noclose
        mov     [cs:dic_handle], ax

        ; ヘッダ(16バイト)を読む
        mov     bx, [cs:dic_handle]
        mov     cx, 16
        mov     dx, header_buf
        mov     ah, 0x3F
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close
        cmp     ax, 16
        jne     .notfound_close

        ; マジック確認 (CS:明示バイト比較。repe cmpsb は ES依存のため使わない)
        mov     si, header_buf
        mov     di, dic_magic
        mov     cx, 8
.magic_loop:
        mov     al, [cs:si]
        cmp     al, [cs:di]
        jne     .notfound_close
        inc     si
        inc     di
        loop    .magic_loop

        mov     ax, [cs:header_buf + 8]
        mov     [cs:dic_count], ax
        mov     ax, [cs:header_buf + 10]
        mov     [cs:dic_index_off], ax

        ; --- 二分探索 ---
        mov     word [cs:search_lo], 0
        mov     ax, [cs:dic_count]
        dec     ax
        mov     [cs:search_hi], ax

.search_loop:
        mov     ax, [cs:search_lo]
        cmp     ax, [cs:search_hi]
        jg      .notfound_close         ; lo > hi (signed) -> 見つからない

        mov     ax, [cs:search_lo]
        add     ax, [cs:search_hi]
        shr     ax, 1
        mov     [cs:search_mid], ax

        mov     ax, [cs:search_mid]
        mov     cx, 20
        mul     cx                      ; dx:ax = mid * 20
        add     ax, [cs:dic_index_off]
        mov     dx, ax
        xor     cx, cx
        mov     bx, [cs:dic_handle]
        mov     ax, 0x4200              ; seek, SEEK_SET
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close

        mov     bx, [cs:dic_handle]
        mov     cx, 20
        mov     dx, entry_buf
        mov     ah, 0x3F
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close
        cmp     ax, 20
        jne     .notfound_close

        ; entry_buf(先頭16バイト=よみ) と yomi_field を比較する
        mov     si, entry_buf
        mov     di, yomi_field
        mov     cx, 16
.cmp16:
        mov     al, [cs:si]
        cmp     al, [cs:di]
        jne     .cmp16_diff
        inc     si
        inc     di
        loop    .cmp16
        jmp     .found                  ; 16バイト全一致
.cmp16_diff:
        cmp     al, [cs:di]             ; al=entry側バイト, [cs:di]=目的側バイト
        jb      .go_upper               ; entry側が小さい -> 後半を探す
        mov     ax, [cs:search_mid]
        dec     ax
        mov     [cs:search_hi], ax
        jmp     .search_loop
.go_upper:
        mov     ax, [cs:search_mid]
        inc     ax
        mov     [cs:search_lo], ax
        jmp     .search_loop

.found:
        mov     ax, [cs:entry_buf + 16] ; 候補ブロックの絶対オフセット(下位ワード)
        mov     dx, ax
        xor     cx, cx
        mov     bx, [cs:dic_handle]
        mov     ax, 0x4200
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close

        mov     bx, [cs:dic_handle]
        mov     cx, 1
        mov     dx, small_buf
        mov     ah, 0x3F
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close
        cmp     ax, 1
        jne     .notfound_close
        mov     al, [cs:small_buf]
        cmp     al, CANDMAX
        jbe     .cc_ok
        mov     al, CANDMAX
.cc_ok:
        mov     [cs:cand_count], al
        mov     byte [cs:cand_index], 0

        mov     byte [cs:cand_read_i], 0
        mov     word [cs:cb_pos], 0

.cand_loop:
        mov     al, [cs:cand_read_i]
        cmp     al, [cs:cand_count]
        jae     .cand_done

        mov     bx, [cs:dic_handle]
        mov     cx, 1
        mov     dx, small_buf
        mov     ah, 0x3F
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close
        cmp     ax, 1
        jne     .notfound_close
        mov     al, [cs:small_buf]
        mov     [cs:cand_cur_len], al

        ; 収まりきるか確認 (cb_pos + cand_cur_len <= CANDBUFSIZE)
        mov     ax, [cs:cb_pos]
        mov     bl, [cs:cand_cur_len]
        xor     bh, bh
        add     ax, bx
        cmp     ax, CANDBUFSIZE
        ja      .cand_overflow

        ; cand_off[i] = cb_pos
        mov     al, [cs:cand_read_i]
        xor     ah, ah
        mov     di, ax
        mov     si, di
        add     si, si                  ; si = i*2
        mov     ax, [cs:cb_pos]
        mov     [cs:cand_off + si], ax

        ; cand_len[i] = cand_cur_len
        mov     al, [cs:cand_cur_len]
        mov     [cs:cand_len + di], al

        ; 候補本体を cand_buf + cb_pos へ読み込む
        mov     bx, [cs:dic_handle]
        mov     cl, [cs:cand_cur_len]
        xor     ch, ch
        mov     ax, [cs:cb_pos]
        mov     dx, cand_buf
        add     dx, ax
        mov     ah, 0x3F
        pushf
        call    far [cs:oldint21]
        jc      .notfound_close
        mov     cl, [cs:cand_cur_len]
        xor     ch, ch
        cmp     ax, cx
        jne     .notfound_close

        mov     al, [cs:cand_cur_len]
        xor     ah, ah
        add     [cs:cb_pos], ax

        inc     byte [cs:cand_read_i]
        jmp     .cand_loop

.cand_overflow:
        ; ここまでに読めた分だけを候補数として使う(それでも0件ならnotfound扱い)
        mov     al, [cs:cand_read_i]
        mov     [cs:cand_count], al
        cmp     byte [cs:cand_count], 0
        je      .notfound_close
        jmp     .cand_done

.cand_done:
        mov     bx, [cs:dic_handle]
        mov     ah, 0x3E
        pushf
        call    far [cs:oldint21]

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        stc
        ret

.notfound_close:
        mov     bx, [cs:dic_handle]
        mov     ah, 0x3E
        pushf
        call    far [cs:oldint21]
.notfound_noclose:
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc
        ret

;--------------------------------------------------------------------
; ステータス行描画
;--------------------------------------------------------------------
draw_status:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     dx, msg_save_cursor
        call    put_str
        mov     dx, msg_release_line
        call    put_str
        mov     dx, msg_goto_line25
        call    put_str
        mov     dx, msg_reverse
        call    put_str

        cmp     byte [cs:mode], 0
        je      .direct_prefix
        mov     dx, msg_prefix_kana
        mov     bl, 7
        jmp     .prefix_out
.direct_prefix:
        mov     dx, msg_prefix_direct
        mov     bl, 9
.prefix_out:
        call    put_str
        ; bl = 使用済み桁数

        cmp     byte [cs:mode], 0
        je      .content_done           ; 直接モード: プレフィクスのみ(v0のまま)

        cmp     byte [cs:cand_count], 0
        je      .show_kanabuf

        ; --- 変換中: 現在の候補 + " (i/n)" ---
        mov     al, [cs:cand_index]
        xor     ah, ah
        mov     di, ax
        mov     si, di
        add     si, si                  ; si = index*2
        mov     bx, [cs:cand_off + si]
        mov     si, cand_buf
        add     si, bx
        mov     cl, [cs:cand_len + di]
        xor     ch, ch
.cand_print_loop:
        jcxz    .cand_print_done
        mov     al, [cs:si]
        call    sjis_width              ; al = 1 or 2
        cmp     al, 2
        je      .cand_wide
        mov     al, [cs:si]
        call    put_char
        inc     si
        dec     cx
        inc     bl
        jmp     .cand_print_loop
.cand_wide:
        mov     al, [cs:si]
        call    put_char
        mov     al, [cs:si+1]
        call    put_char
        add     si, 2
        sub     cx, 2
        add     bl, 2
        jmp     .cand_print_loop
.cand_print_done:

        mov     al, ' '
        call    put_char
        inc     bl
        mov     al, '('
        call    put_char
        inc     bl
        mov     al, [cs:cand_index]
        inc     al
        call    print_decimal_status
        add     bl, cl
        mov     al, '/'
        call    put_char
        inc     bl
        mov     al, [cs:cand_count]
        call    print_decimal_status
        add     bl, cl
        mov     al, ')'
        call    put_char
        inc     bl
        jmp     .content_done

.show_kanabuf:
        ; kanabuf(kanalen バイト = kanalen 桁。かなの各グリフは
        ; 「バイト数==幅」で追加しているので不変式が成り立つ)
        mov     cl, [cs:kanalen]
        xor     ch, ch
        mov     si, kanabuf
.kb_loop:
        jcxz    .kb_done
        mov     al, [cs:si]
        call    put_char
        inc     si
        loop    .kb_loop
.kb_done:
        mov     al, [cs:kanalen]
        add     bl, al

        ; romabuf(未確定のローマ字。ASCIIのみなのでバイト数==桁数)
        mov     cl, [cs:romalen]
        xor     ch, ch
        mov     si, romabuf
.rb_loop:
        jcxz    .rb_done
        mov     al, [cs:si]
        call    put_char
        inc     si
        loop    .rb_loop
.rb_done:
        mov     al, [cs:romalen]
        add     bl, al

.content_done:
        mov     al, STATUSWIDTH
        sub     al, bl                  ; bl = 使用済み桁数
        jbe     .pad_done
        mov     cl, al
        xor     ch, ch
.ploop:
        jcxz    .pad_done
        mov     al, ' '
        call    put_char
        loop    .ploop
.pad_done:

        mov     dx, msg_normal
        call    put_str
        mov     dx, msg_restore_cursor
        call    put_str

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;--------------------------------------------------------------------
; INT 21h ハンドラ
;--------------------------------------------------------------------
newint21:
        cmp     ax, IDENT_FUNC
        je      .ident
        cmp     ah, 0x0A
        je      .buffered
        jmp     far [cs:oldint21]

.ident:
        mov     ax, IDENT_REPLY
        iret

; AH=0Ah(行入力)を自前で完結させる。元のAH=0Ahは呼ばない。
.buffered:
        push    ds                      ; 呼び出し元DSを復帰用に保存
        mov     [cs:calleroff], dx
        mov     ax, ds
        mov     [cs:callerseg], ax

        push    ax
        mov     ax, cs
        mov     ds, ax                  ; ハンドラ内は DS=CS に張り替える
        pop     ax

        call    do_buffered_input

        pop     ds                      ; 呼び出し元DSを復帰
        iret

; メインループ本体。DS=CS の前提で動く。
do_buffered_input:
        ; 呼び出し元バッファの最大長から roomremaining を決める
        mov     ax, [cs:callerseg]
        mov     es, ax
        mov     bx, [cs:calleroff]
        mov     al, [es:bx]             ; maxlen
        or      al, al
        jz      .rr_zero
        dec     al                      ; CR終端用に1バイト残す
        cmp     al, LBUFSIZE - 1
        jbe     .rr_store
        mov     al, LBUFSIZE - 1
.rr_store:
        mov     [cs:roomremaining], al
        jmp     .rr_done
.rr_zero:
        mov     byte [cs:roomremaining], 0
.rr_done:

        mov     byte [cs:llen], 0
        mov     byte [cs:lcnt], 0
        mov     byte [cs:romalen], 0
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:cand_count], 0
        mov     byte [cs:cand_index], 0

        call    draw_status

.key_loop:
        mov     ah, 0x08                ; エコー無し1文字入力
        pushf
        call    far [cs:oldint21]
        or      al, al
        jnz     .not_ext
        mov     ah, 0x08                ; 拡張キー: 続くスキャンコードを捨てる
        pushf
        call    far [cs:oldint21]
        jmp     .after_key

.not_ext:
        cmp     al, 0x0D
        je      .do_cr
        cmp     al, 0x08
        je      .do_bs
        cmp     al, 0x0B
        je      .do_ctrlk

        cmp     byte [cs:mode], 0
        je      .direct_char            ; 直接モードはv0のまま変えない

        ; --- かな入力モード ---
        cmp     al, 0x20
        je      .do_space

        ; 大文字だけ小文字化する
        cmp     al, 'A'
        jb      .kchk_conv
        cmp     al, 'Z'
        ja      .kchk_conv
        add     al, 0x20
.kchk_conv:
        cmp     byte [cs:cand_count], 0
        je      .romaji_char
        ; 変換中に別の文字が来た: まず現在の候補を確定してから
        ; その文字をローマ字処理へ回す
        call    confirm_candidate
        jmp     .romaji_char

.direct_char:
        call    try_append_ascii
        jmp     .after_key

.romaji_char:
        cmp     byte [cs:romalen], ROMABUFSIZE
        jae     .after_key              ; 安全策: 溢れたら捨てる
        push    bx
        mov     bl, [cs:romalen]
        xor     bh, bh
        mov     si, romabuf
        add     si, bx
        mov     [cs:si], al
        inc     byte [cs:romalen]
        pop     bx
        call    resolve_romaji
        jmp     .after_key

.do_space:
        cmp     byte [cs:cand_count], 0
        jne     .space_next
        ; 変換中でない: kanabuf を辞書引きして変換開始 (見つからなければ何もしない)
        call    dict_lookup
        jmp     .after_key
.space_next:
        ; 変換中: 次候補へ(末尾まで行ったら先頭へ戻る)
        mov     al, [cs:cand_index]
        inc     al
        cmp     al, [cs:cand_count]
        jb      .space_set
        xor     al, al
.space_set:
        mov     [cs:cand_index], al
        jmp     .after_key

.do_ctrlk:
        cmp     byte [cs:mode], 0
        je      .ck_toggle              ; 直接->かな: 何もフラッシュする必要がない
        ; かなモード
        cmp     byte [cs:cand_count], 0
        je      .ck_flush_kana
        ; 変換中: 変換を取り消してから直接モードへ(kanabufは残さず捨てる)
        call    cancel_conversion
        mov     byte [cs:kanalen], 0
        mov     byte [cs:kanacnt], 0
        mov     byte [cs:romalen], 0
        jmp     .ck_toggle
.ck_flush_kana:
        ; 変換中でない: romabuf残りをkanabufへ回収してから、
        ; kanabuf を素通しで確定してから直接モードへ
        call    finalize_romabuf
        call    confirm_kanabuf
.ck_toggle:
        xor     byte [cs:mode], 1
        jmp     .after_key

.do_bs:
        cmp     byte [cs:mode], 0
        je      .bs_line                ; 直接モード: v0のまま
        ; かなモード
        cmp     byte [cs:cand_count], 0
        je      .bs_kana_edit
        ; 変換中: 変換を取り消して kanabuf に戻る
        call    cancel_conversion
        jmp     .after_key
.bs_kana_edit:
        cmp     byte [cs:romalen], 0
        je      .bs_kanabuf_tail
        dec     byte [cs:romalen]
        jmp     .after_key
.bs_kanabuf_tail:
        cmp     byte [cs:kanacnt], 0
        je      .bs_line                ; kanabufも空ならlbufの末尾を削る(v0の動作)
        mov     al, [cs:kanacnt]
        dec     al
        mov     [cs:kanacnt], al
        push    bx
        mov     bl, al
        xor     bh, bh
        mov     si, kanawid
        add     si, bx
        mov     al, [cs:si]             ; 削る幅(1 or 2)
        pop     bx
        sub     [cs:kanalen], al
        jmp     .after_key
.bs_line:
        cmp     byte [cs:lcnt], 0
        je      .after_key
        mov     al, [cs:lcnt]
        dec     al
        mov     [cs:lcnt], al
        push    bx
        mov     bl, al
        xor     bh, bh
        mov     si, lwid
        add     si, bx
        mov     al, [cs:si]             ; 幅 (1 or 2)
        pop     bx
        sub     [cs:llen], al
        mov     cl, al
        xor     ch, ch
.bs_erase:
        jcxz    .after_key
        push    cx
        mov     al, 0x08
        call    put_char
        mov     al, 0x20
        call    put_char
        mov     al, 0x08
        call    put_char
        pop     cx
        loop    .bs_erase
        jmp     .after_key

.after_key:
        call    draw_status
        jmp     .key_loop

.do_cr:
        cmp     byte [cs:mode], 0
        je      .cr_direct              ; 直接モード: v0のまま
        ; かなモード
        cmp     byte [cs:cand_count], 0
        jne     .cr_confirm_candidate
        call    finalize_romabuf        ; romabuf残りをkanabufへ回収
        cmp     byte [cs:kanalen], 0
        je      .cr_direct              ; 何も残っていなければ行を確定する
        call    confirm_kanabuf         ; 文節を確定するだけ(行はまだ確定しない)
        jmp     .after_key
.cr_confirm_candidate:
        call    confirm_candidate       ; 候補を確定するだけ(行はまだ確定しない)
        jmp     .after_key

.cr_direct:
        call    finalize_romabuf

        mov     ax, [cs:callerseg]
        mov     es, ax
        mov     bx, [cs:calleroff]
        mov     al, [cs:llen]
        mov     [es:bx+1], al
        mov     si, lbuf
        mov     di, bx
        add     di, 2
        mov     cl, [cs:llen]
        xor     ch, ch
        jcxz    .cr_nobody
        rep     movsb                   ; ds:si(=cs,lbuf) -> es:di(呼び出し元バッファ)
.cr_nobody:
        mov     byte [es:di], 0x0D

        mov     al, 0x0D
        call    put_char
        mov     al, 0x0A
        call    put_char

        mov     dx, msg_release_off
        call    put_str
        ret

;--------------------------------------------------------------------
; メッセージ
;--------------------------------------------------------------------
msg_save_cursor:        db      ESC, '[s', '$'
msg_release_line:       db      ESC, '[>1h', '$'
msg_goto_line25:        db      ESC, '[25;1H', '$'
msg_reverse:             db      ESC, '[7m', '$'
msg_prefix_kana:         db      '[KANA] $'
msg_prefix_direct:       db      '[DIRECT] $'
msg_normal:              db      ESC, '[0m', '$'
msg_restore_cursor:      db      ESC, '[u', '$'
msg_release_off:         db      ESC, '[>1l', '$'

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
        ; 辞書ファイル名を確定する ("X:\FEP.DIC" の X を現在のドライブへ)
        mov     ah, 0x19                ; カレントドライブ取得 (0=A, 1=B, ...)
        int     0x21
        add     al, 'A'
        mov     [dic_path], al

        mov     ax, 0x3521              ; 元の INT 21h ベクタを取得
        int     0x21
        mov     [oldint21], bx
        mov     [oldint21 + 2], es

        mov     dx, newint21            ; 自前ハンドラを設定
        mov     ax, 0x2521
        int     0x21

        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     dx, (install - start + 0x100 + 15) / 16 ; PSPを含む常駐パラグラフ数
        mov     ax, 0x3100              ; 常駐終了
        int     0x21

msg_ok:         db      'FEP v1 resident. Ctrl-K: kana, Space: convert, Enter: commit.', 0x0D, 0x0A, '$'
msg_already:    db      'FEP already resident.', 0x0D, 0x0A, '$'

;--------------------------------------------------------------------
; ローマ字->かな 変換テーブル (tools/gen_romaji.py の生成物)
;--------------------------------------------------------------------
%include "ROMAJI.INC"
