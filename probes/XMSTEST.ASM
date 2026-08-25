; XMSTEST.COM - 拡張メモリ(XMS)・EMS・UMB が使えるかを測る
;
; XMS: INT 2Fh AX=4300h で有無、AX=4310h で入口(ES:BX)を得て、そこを呼ぶ
;      入口 AH=00h バージョン / AH=08h 空き容量 / AH=09h 確保 / AH=0Ah 解放
; EMS: デバイス "EMMXXXX0" を開けるかで判定
; UMB: INT 21h AX=5802h(リンク状態の取得) / AX=5803h(設定)
;
; 結果: 0xA1000 から 8バイト x N (AX,BX,CX,DX)
;   T0  2Fh/4300h        AL=80h なら XMS あり
;   T1  2Fh/4310h        BX=入口のオフセット(ES はここには入らない)
;   T2  XMS AH=00h       AX=バージョン BX=リビジョン
;   T3  XMS AH=08h       AX=最大の空きブロック(KB) DX=空き合計(KB)
;   T4  XMS AH=09h 64KB確保  AX=1 なら成功 DX=ハンドル
;   T5  XMS AH=08h 確保後の空き
;   T6  XMS AH=0Ah 解放   AX=1 なら成功
;   T7  EMS デバイスを開く AX=ハンドル or エラー
;   T8  UMB 5802h        AL=リンク状態
;   T9  5803h BX=1 でリンク
;   T10 AH=48h BX=FFFF   確保できる最大(パラグラフ)
;   T11 終端マーカー
;
; ビルド: nasm -f bin XMSTEST.ASM -o XMSTEST.COM

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

        ; T0 XMS の有無
        mov     ax, 0x4300
        int     0x2F
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T1 入口を得る
        mov     ax, 0x4310
        int     0x2F
        mov     [xms_off], bx
        mov     [xms_seg], es
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T2 バージョン
        mov     ah, 0x00
        call    far [xms_off]
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T3 空き容量
        mov     ah, 0x08
        xor     bl, bl
        call    far [xms_off]
        xor     cx, cx
        call    rec

        ; T4 64KB 確保
        mov     ah, 0x09
        mov     dx, 64
        call    far [xms_off]
        mov     [handle], dx
        xor     cx, cx
        call    rec

        ; T5 確保後の空き
        mov     ah, 0x08
        xor     bl, bl
        call    far [xms_off]
        xor     cx, cx
        call    rec

        ; T6 解放
        mov     ah, 0x0A
        mov     dx, [handle]
        call    far [xms_off]
        xor     cx, cx
        call    rec

        ; T7 EMS のデバイスを開く
        mov     ah, 0x3D
        xor     al, al
        mov     dx, emsname
        int     0x21
        pushf
        pop     dx
        xor     bx, bx
        xor     cx, cx
        call    rec

        ; T8 UMB のリンク状態
        mov     ax, 0x5802
        int     0x21
        xor     bx, bx
        xor     cx, cx
        pushf
        pop     dx
        call    rec

        ; T9 UMB をリンクする
        mov     ax, 0x5803
        mov     bx, 1
        int     0x21
        xor     cx, cx
        pushf
        pop     dx
        call    rec

        ; T10 確保できる最大
        mov     ah, 0x48
        mov     bx, 0xFFFF
        int     0x21
        xor     cx, cx
        pushf
        pop     dx
        call    rec

        ; T11 終端
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

xms_off: dw 0
xms_seg: dw 0
handle:  dw 0
outp:    dw 0
emsname: db 'EMMXXXX0',0
