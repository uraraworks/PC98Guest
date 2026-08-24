; VSPY.COM - どのポートのどのビットが変化するかを調べる
;
; 垂直帰線の待ち方を知るには、まず「どこを見れば帰線が分かるか」を決める必要がある。
; 候補ポートを大量に読み、変化したビットを求める。
;   OR  … 一度でも1になったビット
;   AND … 常に1だったビット
;   OR ^ AND = 変化したビット
;
; 結果: 0xA1000 から 1ポートあたり4バイト
;   +0 port(w)  +2 OR(b)  +3 AND(b)
;
; ビルド: nasm -f bin VSPY.ASM -o VSPY.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100
SAMPLES equ     20000

start:
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 64
        rep     stosw

        mov     si, ports
        xor     di, di
.next:
        lodsw
        cmp     ax, 0xFFFF
        je      .done
        mov     dx, ax                  ; port

        xor     bl, bl                  ; OR
        mov     bh, 0xFF                ; AND
        mov     cx, SAMPLES
.loop:
        in      al, dx
        or      bl, al
        and     bh, al
        loop    .loop

        push    ax
        mov     ax, OUT_SEG
        mov     es, ax
        pop     ax
        mov     [es:di], dx
        mov     [es:di+2], bl
        mov     [es:di+3], bh
        add     di, 4
        jmp     .next
.done:
        mov     ax, 0x4C00
        int     0x21

ports:
        dw      0x0060, 0x0062, 0x00A0, 0x00A2, 0x0000, 0x0002, 0x0032
        dw      0xFFFF
