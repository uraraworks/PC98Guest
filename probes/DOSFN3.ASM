; DOSFN3.COM - 拡張エラーコード(AH=59h)と日付の設定(AH=2Bh)
;
; 書籍(4-7)で呼び出し形式を確認し、値は実測する。
;   AH=59h BX=00 -> AX=拡張エラーコード BH=エラークラス BL=可能な対処 CH=ローカス
;                   **CL,DX,SI,DI,BP,DS,ES は破壊される**
;   AH=2Bh CX=年 DH=月 DL=日 -> AL=00(成功)/FF(無効)
;
; 結果: 0xA1000 から 8バイト x N (AX,BX,CX,DX)
;   T0  3Dh 存在しないファイルを開く
;   T1  59h 直後の拡張エラー
;   T2  5Bh 新規作成(1回目)
;   T3  3Eh クローズ
;   T4  5Bh 新規作成(2回目・すでにある)
;   T5  59h 直後の拡張エラー
;   T6  41h 削除
;   T7  2Bh 日付を 2001-02-03 に設定
;   T8  2Ah 日付の取得
;   T9  2Bh 月に13を渡す(無効なはず)
;   T10 2Ah 日付の取得(変わっていないはず)
;   T11 0xDEAD 終端マーカー
;
; ビルド: nasm -f bin DOSFN3.ASM -o DOSFN3.COM

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

        ; T0 存在しないファイルを開く
        mov     ah, 0x3D
        xor     al, al
        mov     dx, fnone
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T1 拡張エラー(DS が壊れるので先に退避・後で復元)
        push    ds
        mov     ah, 0x59
        xor     bx, bx
        int     0x21
        push    ax
        push    bx
        push    cx
        pop     cx
        pop     bx
        pop     ax
        pop     ds
        xor     dx, dx
        call    rec

        ; T2 新規作成(1回目)
        mov     ah, 0x5B
        xor     cx, cx
        mov     dx, fname
        int     0x21
        mov     [handle], ax
        pushf
        pop     dx
        call    rec

        ; T3 クローズ
        mov     bx, [handle]
        mov     ah, 0x3E
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T4 新規作成(2回目・すでにある)
        mov     ah, 0x5B
        xor     cx, cx
        mov     dx, fname
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T5 拡張エラー
        push    ds
        mov     ah, 0x59
        xor     bx, bx
        int     0x21
        push    ax
        push    bx
        push    cx
        pop     cx
        pop     bx
        pop     ax
        pop     ds
        xor     dx, dx
        call    rec

        ; T6 削除
        mov     ah, 0x41
        mov     dx, fname
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T7 日付を 2001-02-03 に設定
        mov     ah, 0x2B
        mov     cx, 2001
        mov     dh, 2
        mov     dl, 3
        int     0x21
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T8 日付の取得
        mov     ah, 0x2A
        int     0x21
        call    rec

        ; T9 月に13を渡す
        mov     ah, 0x2B
        mov     cx, 2001
        mov     dh, 13
        mov     dl, 3
        int     0x21
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        ; T10 日付の取得
        mov     ah, 0x2A
        int     0x21
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

fnone:  db 'NOSUCH.XYZ',0
fname:  db 'DOSFN3.TMP',0
outp:   dw 0
handle: dw 0
