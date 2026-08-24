; DOSFN2.COM - ファイルポインタの移動(AH=42h)と、読み出しが要求より少なくなる条件
;
; 書籍(4-7 MS-DOSファンクションコール一覧)で AH=42h の呼び出し形式を確認し、
; 実際の値は本プログラムで測る。
;   AH=42h  CX:DX=オフセット  AL=00(先頭から)/01(現在位置から)/02(終端から)
;           BX=ハンドル  ->  CF=0 のとき DX:AX=移動後の位置
;
; 結果: 0xA1000 から 8バイト x N (AX,BX,CX,DX)
;   T0  3Ch 作成            AX=ハンドル
;   T1  40h 16バイト書く    AX=書けた数
;   T2  42h AL=02 offset 0  DX:AX=ファイルの大きさ
;   T3  3Eh クローズ
;   T4  3Dh 開く            AX=ハンドル
;   T5  42h AL=00 offset 8  DX:AX=8 のはず
;   T6  3Fh 16バイト要求    AX=実際に読めた数(残り8のはず)
;   T7  読めた中身の先頭4ワード
;   T8  42h AL=01 offset -4 DX:AX=現在位置-4
;   T9  3Fh 16バイト要求    AX=実際に読めた数
;   T10 42h AL=00 offset 100 (終端より後ろ)  DX:AX=100
;   T11 3Fh 16バイト要求    AX=実際に読めた数(0のはず)
;   T12 3Eh クローズ
;   T13 41h 削除
;   T14 0xDEAD 終端マーカー
;
; 読み込みバッファは 0xCC で埋めてある。
;
; ビルド: nasm -f bin DOSFN2.ASM -o DOSFN2.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100

start:
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 128
        rep     stosw
        mov     word [outp], 0

        ; T0 作成
        mov     ah, 0x3C
        xor     cx, cx
        mov     dx, fname
        int     0x21
        mov     [handle], ax
        pushf
        pop     dx
        call    rec

        ; T1 16バイト書く
        mov     bx, [handle]
        mov     ah, 0x40
        mov     cx, 16
        mov     dx, wdata
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T2 終端へシーク(大きさが分かる)
        mov     bx, [handle]
        mov     ax, 0x4202
        xor     cx, cx
        xor     dx, dx
        int     0x21
        call    rec                     ; DX:AX が位置。DX は FLAGS ではない

        ; T3 クローズ
        mov     bx, [handle]
        mov     ah, 0x3E
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T4 開く
        mov     ah, 0x3D
        xor     al, al
        mov     dx, fname
        int     0x21
        mov     [handle], ax
        pushf
        pop     dx
        call    rec

        ; T5 先頭から8へ
        mov     bx, [handle]
        mov     ax, 0x4200
        xor     cx, cx
        mov     dx, 8
        int     0x21
        call    rec

        ; T6 16バイト要求(残りは8のはず)
        mov     bx, [handle]
        mov     ah, 0x3F
        mov     cx, 16
        mov     dx, rbuf
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T7 読めた中身
        mov     ax, [rbuf]
        mov     bx, [rbuf+2]
        mov     cx, [rbuf+4]
        mov     dx, [rbuf+6]
        call    rec

        ; T8 現在位置から -4
        mov     bx, [handle]
        mov     ax, 0x4201
        mov     cx, 0xFFFF
        mov     dx, 0xFFFC              ; -4
        int     0x21
        call    rec

        ; T9 16バイト要求
        mov     bx, [handle]
        mov     ah, 0x3F
        mov     cx, 16
        mov     dx, rbuf2
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T10 終端より後ろへ
        mov     bx, [handle]
        mov     ax, 0x4200
        xor     cx, cx
        mov     dx, 100
        int     0x21
        call    rec

        ; T11 16バイト要求(0のはず)
        mov     bx, [handle]
        mov     ah, 0x3F
        mov     cx, 16
        mov     dx, rbuf3
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T12 クローズ
        mov     bx, [handle]
        mov     ah, 0x3E
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T13 削除
        mov     ah, 0x41
        mov     dx, fname
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T14 終端
        mov     ax, 0xDEAD
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        mov     ax, 0x4C00
        int     0x21

rec:
        push    ax
        push    di
        mov     ax, OUT_SEG
        mov     es, ax
        pop     di
        pop     ax
        push    di
        mov     di, [outp]
        mov     [es:di], ax
        mov     [es:di+2], bx
        mov     [es:di+4], cx
        mov     [es:di+6], dx
        add     word [outp], 8
        pop     di
        ret

fname:  db 'DOSFN2.TMP',0
wdata:  db '0123456789ABCDE'
        db 0x0A
outp:   dw 0
handle: dw 0
rbuf:   times 20 db 0xCC
rbuf2:  times 20 db 0xCC
rbuf3:  times 20 db 0xCC
