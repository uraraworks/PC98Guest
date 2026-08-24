; ESCT2.COM - PC-98 / MS-DOS INT 21h ハンドラ内発ANSIエスケープ・全角出力 測定TSR
;
; 目的:
;   ESCT1.COM は「プログラム本体から」ESCシーケンスと全角(Shift_JIS)を
;   出せるかを測った。本TSRは一歩進めて「INT 21h のハンドラの中から」
;   同じことができるかを測る。AH=0Ah(行入力)がフックされた瞬間に、
;   元のINT 21h処理へ渡す前に画面最下行へステータス行を描く。
;
;   ハンドラ内から画面出力する際は、int 21h を使うと自分自身のハンドラに
;   再入してしまうため使えない。paste.asm の echo_al と同じ作法で、
;   pushf + call far [cs:oldint21] により旧ハンドラを直接呼び出す。
;
; 常駐確認: AX=E7E8h で呼ぶと AX=5432h('T2') を返す。
;
; ビルド: nasm -f bin ESCT2.ASM -o ESCT2.COM

        cpu     8086
        org     0x100

ESC             equ     0x1B
IDENT_FUNC      equ     0xE7E8
IDENT_REPLY     equ     0x5432

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint21:       dd      0

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

; 最下行にステータス行を描く。呼び出し前後でレジスタ・DSを保存/復元する。
draw_status:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds

        mov     ax, cs
        mov     ds, ax

        mov     dx, msg_save_cursor
        call    put_str
        mov     dx, msg_release_line
        call    put_str
        mov     dx, msg_goto_line25
        call    put_str
        mov     dx, msg_reverse
        call    put_str
        mov     dx, msg_prefix
        call    put_str

        mov     si, kanji_bytes
        mov     cx, kanji_len
.kloop:
        lodsb
        call    put_char
        loop    .kloop

        mov     dx, msg_suffix
        call    put_str
        mov     dx, msg_normal
        call    put_str
        mov     dx, msg_restore_cursor
        call    put_str

        pop     ds
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

; AH=0Ah(行入力)が呼ばれた瞬間に、元処理へ渡す前にステータス行を描く。
.buffered:
        pushf
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        call    draw_status
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        popf
        jmp     far [cs:oldint21]

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------

; 全角7文字。ESCT1.ASM と同じ選定(帯域内6文字+帯域外の「熙」)。
; ソースに日本語を書かず、バイト値のみで表す。
kanji_bytes:
        db      0x82, 0xA0            ; あ
        db      0x83, 0x41            ; ア
        db      0x88, 0x9F            ; 亜
        db      0x8A, 0xBF            ; 漢
        db      0x8E, 0x9A            ; 字
        db      0x9F, 0x54            ; 鬱
        db      0xEA, 0xA4            ; 熙 (帯域外・対照用)
kanji_len equ $ - kanji_bytes

msg_save_cursor:        db      ESC, '[s', '$'
msg_release_line:       db      ESC, '[>1h', '$'
msg_goto_line25:        db      ESC, '[25;1H', '$'
msg_reverse:             db      ESC, '[7m', '$'
msg_prefix:              db      '[T2] $'
msg_suffix:              db      ' <T2END>$'
msg_normal:              db      ESC, '[0m', '$'
msg_restore_cursor:      db      ESC, '[u', '$'

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

msg_ok:         db      'ESCT2 resident.', 0x0D, 0x0A, '$'
msg_already:    db      'ESCT2 already resident.', 0x0D, 0x0A, '$'
