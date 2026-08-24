; CALL60.COM - INT 60h を呼んで結果を固定番地へ置く
;
; 常駐した TSR60 が生きているかを確認するための呼び出し側。
; 結果は 0xA1000 へ置き、ホストから readMemoryBase64 で読む。
;   +0 word AX（0x1234 なら生きている）
;   +2 word BX（呼ばれた回数）
;   +4 word 0xC0DE（ここまで到達した印）
;
; ビルド: nasm -f bin CALL60.ASM -o CALL60.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100

start:
        ; 前回の結果を消しておく(残骸と区別するため)
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        mov     cx, 8
        mov     ax, 0xCCCC
        rep     stosw

        xor     ax, ax
        xor     bx, bx
        int     0x60

        push    ax
        mov     ax, OUT_SEG
        mov     es, ax
        pop     ax
        mov     [es:0], ax
        mov     [es:2], bx
        mov     word [es:4], 0xC0DE

        mov     dx, msg
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C00
        int     0x21

msg:    db 'CALL60 done.',0x0D,0x0A,'$'
