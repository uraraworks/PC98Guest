; FCBTEST.COM - FCB を使う古い系統のファイル操作を測る
;
; 書籍(4-7)で呼び出し形式を確認し、実際の値は本プログラムで測る。
;   AH=1Ah DTA(転送アドレス)の設定   DS:DX
;   AH=16h ファイルの作成            DS:DX=オープンされていないFCB -> AL=00/FF
;   AH=15h シーケンシャルライト      DS:DX=オープンされているFCB   -> AL=00/01/02
;   AH=10h クローズ
;   AH=0Fh オープン
;   AH=14h シーケンシャルリード      -> AL=00 正常 / 01 EOF / 02 バッファ小 / 03 EOF一部
;   AH=13h ファイルの削除
;   AH=11h 最初のエントリを検索      -> AL=00 存在 / FF 不在
;
; FCB の並び(37バイト):
;   +0 ドライブ(0=既定) +1 名前8 +9 拡張子3 +12 カレントブロック(w)
;   +14 レコード長(w) +16 ファイルサイズ(dw) +20 日付(w) +22 時刻(w)
;   +24 予備8 +32 カレントレコード(b) +33 ランダムレコード(dw)
;
; 結果: 0xA1000 から 8バイト x N (AX,BX,CX,DX)
;   T0 1Ah DTA設定  T1 16h 作成  T2 15h 書き込み  T3 10h クローズ
;   T4 0Fh オープン T5 レコード長の確認  T6 14h 読み込み  T7 読めた中身
;   T8 14h もう一度読む(EOFのはず)  T9 11h 検索(ある)  T10 11h 検索(ない)
;   T11 13h 削除  T12 11h 検索(消えたはず)  T13 終端マーカー
;
; ビルド: nasm -f bin FCBTEST.ASM -o FCBTEST.COM

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

        ; T0 DTA を dta へ
        mov     ah, 0x1A
        mov     dx, dta
        int     0x21
        xor     bx, bx
        xor     cx, cx
        pushf
        pop     dx
        call    rec

        ; T1 作成
        mov     ah, 0x16
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; レコード長を16に、位置を先頭に
        mov     word [fcb+12], 0        ; カレントブロック
        mov     word [fcb+14], 16       ; レコード長
        mov     byte [fcb+32], 0        ; カレントレコード

        ; T2 書き込み(DTA の中身が書かれる)
        mov     ah, 0x15
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T3 クローズ
        mov     ah, 0x10
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T4 オープン
        mov     ah, 0x0F
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T5 オープン後のレコード長とファイルサイズ
        mov     ax, [fcb+14]
        mov     bx, [fcb+16]
        mov     cx, [fcb+18]
        xor     dx, dx
        call    rec

        ; 読み出し位置を先頭に、レコード長を16に
        mov     word [fcb+12], 0
        mov     word [fcb+14], 16
        mov     byte [fcb+32], 0

        ; DTA を読み込み用に潰す
        mov     di, dta
        mov     cx, 16
        mov     al, 0xCC
        push    es
        push    ds
        pop     es
        rep     stosb
        pop     es

        ; T6 読み込み
        mov     ah, 0x14
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T7 読めた中身
        mov     ax, [dta]
        mov     bx, [dta+2]
        mov     cx, [dta+4]
        mov     dx, [dta+6]
        call    rec

        ; T8 もう一度読む(EOF のはず)
        mov     ah, 0x14
        mov     dx, fcb
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T9 検索(ある)
        mov     ah, 0x11
        mov     dx, fcb2
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T10 検索(ない)
        mov     ah, 0x11
        mov     dx, fcbnone
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T11 削除
        mov     ah, 0x13
        mov     dx, fcb2
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T12 検索(消えたはず)
        mov     ah, 0x11
        mov     dx, fcb2
        int     0x21
        pushf
        pop     dx
        call    rec

        ; T13 終端
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

; 'FCBTEST .TMP' の FCB
fcb:    db 0
        db 'FCBTEST '
        db 'TMP'
        times 37-12 db 0
; 検索・削除用(同じ名前の別インスタンス)
fcb2:   db 0
        db 'FCBTEST '
        db 'TMP'
        times 37-12 db 0
; 存在しない名前
fcbnone: db 0
        db 'NOSUCHXX'
        db 'ZZZ'
        times 37-12 db 0

dta:    db 'FCB-RECORD-16BY'
        db 0x0A
        times 64 db 0
