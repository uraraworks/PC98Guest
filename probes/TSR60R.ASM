; TSR60R.COM - 解除できる常駐プログラム
;
; TSR60 と同じく INT 60h にハンドラを置いて常駐するが、解除に必要なものを
; 決まった位置に置く。
;   CS:0x100 から 'TSR60R!!' の目印
;   CS:0x108 に元の INT 60h ベクタ(オフセット,セグメント)
; 解除側(UNTSR.COM)はこれを見つけて元へ戻し、メモリを解放する。
;
; ビルド: nasm -f bin TSR60R.ASM -o TSR60R.COM

        cpu     8086
        org     0x100

        jmp     near start      ; **near を明示する。**書かないと2バイトに縮められ、
                                ; 目印の位置がずれて解除側から見つけられなくなる

sig:    db 'TSR60R!!'          ; 0x103 から(jmp near が3バイト)
old_off: dw 0
old_seg: dw 0

start:
        ; 元のベクタを取っておく
        mov     ax, 0x3560
        int     0x21
        mov     [old_off], bx
        mov     [old_seg], es

        mov     ax, 0x2560
        mov     dx, handler
        int     0x21

        mov     dx, msg
        mov     ah, 0x09
        int     0x21

        mov     dx, resident_end
        add     dx, 15
        mov     cl, 4
        shr     dx, cl
        mov     ax, 0x3100
        int     0x21

handler:
        push    ds
        push    cs
        pop     ds
        inc     word [counter]
        mov     bx, [counter]
        mov     ax, 0x1234
        pop     ds
        iret

counter: dw 0
resident_end:
msg:    db 'TSR60R resident.',0x0D,0x0A,'$'
