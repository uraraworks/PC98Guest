; UNTSR.COM - TSR60R を解除する
;
; INT 60h のベクタからセグメントを得て、目印を確かめてから
;   1) 元のベクタへ戻す
;   2) そのセグメントのメモリを解放する(AH=49h)
;
; 結果: 0xA1000 に記録
;   +0 見つけた段階 (0=目印なし 1=目印あり)
;   +2 AH=25h の後のフラグ  +4 AH=49h の AX  +6 AH=49h のフラグ
;
; ビルド: nasm -f bin UNTSR.ASM -o UNTSR.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100

start:
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 8
        rep     stosw

        ; INT 60h のベクタを取る
        mov     ax, 0x3560
        int     0x21
        mov     [tseg], es              ; 常駐プログラムのセグメント

        ; 目印を確かめる
        mov     ds, ax                  ; ダミー(下で入れ直す)
        push    cs
        pop     ds
        mov     ax, [tseg]
        mov     es, ax
        mov     si, 0x103
        mov     di, sig
        mov     cx, 8
.cmp:
        mov     al, [es:si]
        cmp     al, [di]
        jne     .nosig
        inc     si
        inc     di
        loop    .cmp

        ; 目印あり
        mov     ax, OUT_SEG
        mov     es, ax
        mov     word [es:0], 1

        ; 元のベクタへ戻す
        mov     ax, [tseg]
        mov     es, ax
        mov     dx, [es:0x10B]          ; old_off
        mov     bx, [es:0x10D]          ; old_seg
        push    ds
        mov     ds, bx
        mov     ax, 0x2560
        int     0x21
        pop     ds
        pushf
        pop     ax
        push    ax
        mov     ax, OUT_SEG
        mov     es, ax
        pop     ax
        mov     [es:2], ax

        ; メモリを解放する
        mov     ax, [tseg]
        mov     es, ax
        mov     ah, 0x49
        int     0x21
        pushf
        pop     bx
        push    ax
        push    bx
        mov     ax, OUT_SEG
        mov     es, ax
        pop     bx
        pop     ax
        mov     [es:4], ax
        mov     [es:6], bx

        mov     dx, msgok
        jmp     .out
.nosig:
        mov     dx, msgng
.out:
        push    cs
        pop     ds
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C00
        int     0x21

sig:    db 'TSR60R!!'
tseg:   dw 0
msgok:  db 'UNTSR done.',0x0D,0x0A,'$'
msgng:  db 'UNTSR: not found.',0x0D,0x0A,'$'
