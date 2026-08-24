; INTEST.COM - PC-98 / MS-DOS 行入力(INT 21h AH=0Ah) 測定プログラム
;
; 目的:
;   FreeDOS(98) の COMMAND.COM (FreeCOM) が入力に INT 21h を使っていない
;   ことが実測でわかったため、「FEP側が壊れているのか」「FreeCOM がそもそも
;   DOS の行入力ファンクションを使わないだけなのか」を切り分ける。
;   本プログラムは AH=0Ah(バッファ入力)を直接呼ぶだけの最小構成。
;
; 動作:
;   [1] "[AH=0Ah] input: " を表示 (AH=09h)
;   [2] AH=0Ah で1行入力
;   [3] 改行後、"[got N] " に続けて受け取った内容をそのまま出力 (AH=02h、1バイトずつ)
;   [4] 改行後、"[hex] " に続けて受け取ったバイト列を16進2桁空白区切りで表示
;   [5] 改行して終了 (AX=4C00h)
;
; 10進・16進の表示は DOS に変換関数が無いため自前で実装する。
;
; ビルド: nasm -f bin INTEST.ASM -o INTEST.COM

        cpu     8086
        org     0x100

start:
        ; ---- [1] プロンプト表示 ----
        mov     dx, msg_prompt
        mov     ah, 0x09
        int     0x21

        ; ---- [2] AH=0Ah で1行入力 ----
        mov     dx, inbuf
        mov     ah, 0x0A
        int     0x21

        ; ---- 改行 ----
        call    crlf

        ; ---- [3] "[got N] " + N(10進) ----
        mov     dx, msg_got
        mov     ah, 0x09
        int     0x21

        xor     ah, ah
        mov     al, [inbuf + 1]         ; DOS が返した実際の文字数
        mov     [gotlen], al
        call    print_dec

        mov     dx, msg_space
        mov     ah, 0x09
        int     0x21

        ; ---- 受け取った内容をそのまま出力 (AH=02h、1バイトずつ) ----
        mov     si, inbuf + 2
        xor     ch, ch
        mov     cl, [gotlen]
        or      cl, cl
        jz      .skip_body
.body_loop:
        lodsb
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        loop    .body_loop
.skip_body:

        call    crlf

        ; ---- [4] "[hex] " + バイト列16進 ----
        mov     dx, msg_hex
        mov     ah, 0x09
        int     0x21

        mov     si, inbuf + 2
        xor     ch, ch
        mov     cl, [gotlen]
        or      cl, cl
        jz      .skip_hex
.hex_loop:
        lodsb
        call    print_hex_byte
        push    cx
        mov     dl, ' '
        mov     ah, 0x02
        int     0x21
        pop     cx
        loop    .hex_loop
.skip_hex:

        call    crlf

        ; ---- 終了 ----
        mov     ax, 0x4C00
        int     0x21

;--------------------------------------------------------------------
; crlf : 改行を出力
;--------------------------------------------------------------------
crlf:
        push    ax
        push    dx
        mov     dx, msg_crlf
        mov     ah, 0x09
        int     0x21
        pop     dx
        pop     ax
        ret

;--------------------------------------------------------------------
; print_dec : AL(0-255) を10進でAH=02h出力(先頭ゼロ抑制、0は"0"を出す)
;--------------------------------------------------------------------
print_dec:
        push    ax
        push    bx
        push    cx
        push    dx

        xor     ah, ah                  ; AX = 0-255
        mov     bx, 10
        xor     cx, cx                  ; 桁数カウンタ(スタックに積んだ桁数)

        or      ax, ax
        jnz     .divloop
        ; 0 の特殊扱い
        mov     dl, '0'
        mov     ah, 0x02
        int     0x21
        jmp     .done

.divloop:
        or      ax, ax
        jz      .printloop
        xor     dx, dx
        div     bx                      ; AX / 10 -> AX=商, DX=余り
        push    dx                      ; 余り(下位桁)を積む
        inc     cx
        jmp     .divloop

.printloop:
        jcxz    .done
        pop     dx
        add     dl, '0'
        push    cx
        mov     ah, 0x02
        int     0x21
        pop     cx
        loop    .printloop

.done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;--------------------------------------------------------------------
; print_hex_byte : AL の値を16進2桁でAH=02h出力
;--------------------------------------------------------------------
print_hex_byte:
        push    ax
        push    cx
        push    dx

        mov     ah, al
        mov     cl, 4
        shr     al, cl                  ; 上位4bit
        call    .nibble
        mov     al, ah
        and     al, 0x0F                ; 下位4bit
        call    .nibble

        pop     dx
        pop     cx
        pop     ax
        ret

.nibble:
        cmp     al, 10
        jae     .alpha
        add     al, '0'
        jmp     .out
.alpha:
        add     al, 'A' - 10
.out:
        mov     dl, al
        push    ax
        mov     ah, 0x02
        int     0x21
        pop     ax
        ret

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------
msg_prompt:     db '[AH=0Ah] input: $'
msg_crlf:       db 0x0D, 0x0A, '$'
msg_got:        db '[got $'
msg_space:      db '] $'
msg_hex:        db '[hex] $'

gotlen:         db 0

; AH=0Ah 用バッファ: 先頭が最大文字数(CR含まず)、次が実際の文字数(DOSが書く)、
; 続けて最大80バイトの入力領域(+予備)。合計 2+82 = 84 バイト。
inbuf:          db 80, 0
                times 82 db 0
