; RAWWR.COM - ディスクへの生セクタ書き込みを測る
;
; **書き込み先は FDD2 (DA/UA 0x91) 固定。** 空のFDを挿しておくこと。
; HDD やシステムディスクを壊さないよう、ドライブは引数にせず固定してある。
;
; INT 1Bh の書き込みは 下位4bit = 05H。読み出し(06H)と同じく SEEK(D4) を立てる。
;   AH = 0x75  (MF=1, SEEK=1, コマンド=5=書き込み)
;   AL = 0x91  FDD2
;   BX = 転送バイト数 1024
;   CH = 3(セクタ長1024)  CL = シリンダ0  DH = ヘッド0  DL = セクタ2
;   ES:BP = バッファ
;
; 結果: 0xA1000 から 8バイト x N
;   T0 書き込み  AX=ステータス(AHに入る) DX=FLAGS
;   T1 読み出し
;   T2 読めた中身の先頭4ワード
;   T3 期待値と一致したか (AX=1 一致 / 0 不一致)
;   T4 終端マーカー
;
; ビルド: nasm -f bin RAWWR.ASM -o RAWWR.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100
DAUA    equ     0x91            ; FDD2
SECT    equ     2               ; セクタ番号(1起点)

start:
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 128
        rep     stosw
        mov     word [outp], 0

        ; 書き込むバッファを作る
        mov     di, wbuf
        mov     cx, 1024
        push    es
        push    ds
        pop     es
        mov     al, 0
.fill:
        stosb
        inc     al
        loop    .fill
        pop     es
        mov     word [wbuf], 0x5A5A
        mov     word [wbuf+2], 0xA5A5

        ; T0 書き込み
        push    ds
        pop     es
        mov     ah, 0x75
        mov     al, DAUA
        mov     bx, 1024
        mov     ch, 3
        mov     cl, 0
        mov     dh, 0
        mov     dl, SECT
        mov     bp, wbuf
        int     0x1B
        pushf
        pop     dx
        xor     bx, bx
        xor     cx, cx
        call    rec

        ; 読み出しバッファを 0xCC で埋める
        mov     di, rbuf
        mov     cx, 1024
        push    es
        push    ds
        pop     es
        mov     al, 0xCC
        rep     stosb
        pop     es

        ; T1 読み出し
        push    ds
        pop     es
        mov     ah, 0x76
        mov     al, DAUA
        mov     bx, 1024
        mov     ch, 3
        mov     cl, 0
        mov     dh, 0
        mov     dl, SECT
        mov     bp, rbuf
        int     0x1B
        pushf
        pop     dx
        xor     bx, bx
        xor     cx, cx
        call    rec

        ; T2 読めた中身
        mov     ax, [rbuf]
        mov     bx, [rbuf+2]
        mov     cx, [rbuf+4]
        mov     dx, [rbuf+6]
        call    rec

        ; T3 全バイト比較
        mov     si, wbuf
        mov     di, rbuf
        mov     cx, 1024
        mov     ax, 1
.cmp:
        mov     bl, [si]
        cmp     bl, [di]
        je      .next
        xor     ax, ax
        jmp     .done
.next:
        inc     si
        inc     di
        loop    .cmp
.done:
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T4 終端
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

outp:   dw 0
wbuf:   times 1024 db 0
rbuf:   times 1024 db 0
