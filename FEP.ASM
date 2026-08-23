; FEP.COM - PC-98 / MS-DOS 用 常駐ローマ字かな入力 FEP v0
;
; 目的:
;   INT 21h の AH=0Ah(行入力)をフックし、ハンドラ内で自前のローマ字->かな
;   変換つき行入力を行う。辞書変換はしない(ローマ字->ひらがなまで)。
;
;   常駐・INT 21hフックの作法は ESCT2.ASM をそのまま踏襲する
;   (ハンドラ内では DS を CS に張り替え、旧INT21hは pushf + call far で呼ぶ)。
;   行入力の自前実装(BS処理・溢れ処理・CR/LFの後始末)は
;   WebNP2/tools/paste-tsr/paste.asm の .buffered を踏襲する。
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
STATUSWIDTH     equ     40      ; ステータス行の埋め幅

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

scratchch:      db      0       ; try_append_ascii が1バイトを指すために使う

; 「ん」「っ」の Shift_JIS バイト列 (テーブル検索を経ない特殊確定用)
sound_n:        db      0x82, 0xF1      ; "ん"
sound_sokuon:   db      0x82, 0xC1      ; "っ"

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
        call    try_append
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
        call    try_append
        pop     dx
        pop     cx
        pop     si
        ret

; romabuf の中身(romalen バイト)をそのまま素通しで全部確定し、romalen=0にする。
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
        call    try_append_ascii
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

; CR確定時: romabuf の残りを確定する。
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
; ローマ字->かな 変換本体 (romabuf に1文字足した直後に呼ぶ)
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
        call    try_append_ascii
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.exact_found:
        ; SI = かなバイト列, AL = かなバイト数 (find_exact の戻り値)
        mov     cl, al
        shr     al, 1
        mov     dh, al
        mov     dl, 2
        call    try_append
        mov     byte [cs:romalen], 0
        jmp     .loop
.ret:
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
        mov     cl, 7
        jmp     .prefix_out
.direct_prefix:
        mov     dx, msg_prefix_direct
        mov     cl, 9
.prefix_out:
        call    put_str

        mov     al, [cs:romalen]
        add     cl, al                  ; cl = 使用済み桁数(prefix+romabuf)
        mov     bl, cl                  ; bl に退避 (bx は本関数内で他用途に使わない)

        mov     cl, al
        xor     ch, ch
        mov     si, romabuf
.rloop:
        jcxz    .rdone
        mov     al, [cs:si]
        call    put_char
        inc     si
        loop    .rloop
.rdone:

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
        je      .direct_char

        ; --- かな入力モード ---
        ; 制御文字(0x0D/0x08/0x0B/0x00)は上ですでに処理済み。
        ; それ以外の印字可能文字はすべてローマ字処理に回す
        ; (数字・記号なども romabuf を経由させ、未確定分の先を
        ; 追い越して出力されるのを防ぐ)。大文字だけ小文字化する。
        cmp     al, 'A'
        jb      .romaji_char
        cmp     al, 'Z'
        ja      .romaji_char
        add     al, 0x20                ; 小文字化
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

.do_ctrlk:
        cmp     byte [cs:romalen], 0
        je      .ck_toggle
        call    flush_romabuf_literal
.ck_toggle:
        xor     byte [cs:mode], 1
        jmp     .after_key

.do_bs:
        cmp     byte [cs:romalen], 0
        je      .bs_line
        dec     byte [cs:romalen]
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

msg_ok:         db      'FEP v0 resident. Ctrl-K toggles kana mode.', 0x0D, 0x0A, '$'
msg_already:    db      'FEP already resident.', 0x0D, 0x0A, '$'

;--------------------------------------------------------------------
; ローマ字->かな 変換テーブル (tools/gen_romaji.py の生成物)
;--------------------------------------------------------------------
%include "ROMAJI.INC"
