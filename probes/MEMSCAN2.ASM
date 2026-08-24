; MEMSCAN2.COM - メモリ空間を4KBごとに実際に書いて分類する(16色モードにしてから)
;
; MEMSCAN.COM は既定(8色)のまま走らせたため、輝度プレーン(0xE0000)が
; 「未実装」に見えた。**メモリマップは画面モードで変わる**ことを確かめる版。
;
; 「そこに何があるか」は読むだけでは分からない。書いて読み返し、戻す。
;   書いた値が読める       -> RAM
;   元の値のまま           -> ROM か未実装
; 元の値も一緒に記録するので、未実装(0xFF が読めることが多い)と ROM を見分けられる。
;
; 結果: 0xA1000 から 1ブロック2バイト x 256
;   +0 分類  1=RAM / 2=書けない / 0=未走査
;   +1 元の値
;
; 結果領域そのもの(0xA1000 台)は走査しない。走査すると自分の結果を壊す。
;
; 割り込みは1ブロックずつ止める。書いてから戻すまでの間に割り込みが入ると、
; 割り込み側が壊れた値を読むおそれがあるため。
;
; ビルド: nasm -f bin MEMSCAN2.ASM -o MEMSCAN2.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100
SKIP    equ     0xA1                    ; 結果領域のあるブロック番号

start:
        mov     al, 0x01
        out     0x6A, al                ; 16色モードにする

        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 256
        rep     stosw                   ; 結果領域を0で潰す

        xor     bx, bx                  ; ブロック番号
.next:
        cmp     bx, SKIP
        je      .skip

        mov     ax, bx
        mov     cl, 8
        shl     ax, cl                  ; セグメント = ブロック番号 * 0x100
        mov     ds, ax
        xor     si, si

        cli
        mov     al, [si]                ; 元の値
        mov     ah, al
        not     al
        mov     [si], al                ; 反転して書く
        mov     dl, [si]                ; 読み返す
        mov     [si], ah                ; 元に戻す
        sti

        ; 分類: 書いた値(al)が読めたか
        mov     dh, 2                   ; 書けない
        cmp     dl, al
        jne     .store
        mov     dh, 1                   ; RAM
.store:
        push    ax
        mov     ax, OUT_SEG
        mov     ds, ax
        pop     ax
        mov     di, bx
        shl     di, 1
        mov     [di], dh                ; 分類
        mov     [di+1], ah              ; 元の値

.skip:
        inc     bx
        cmp     bx, 256
        jb      .next

        mov     ax, 0x4C00
        int     0x21
