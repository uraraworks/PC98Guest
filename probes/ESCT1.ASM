; ESCT1.COM - PC-98 / MS-DOS 文字出力・ANSIエスケープ 測定プログラム
;
; 目的:
;   INT 21h AH=02h(1文字出力) と AH=09h($終端文字列出力) の2経路で、
;   全角(Shift_JIS)文字と半角カナが正しく画面まで届くかを比較測定する。
;   あわせて ANSI.SYS 相当のエスケープシーケンス(属性反転・カーソル位置の
;   保存/復元・ファンクションキー行の制御)がどこまで効くかを測る。
;
; 全角バイト列の選定意図:
;   あ 82 A0 / ア 83 41 / 亜 88 9F / 漢 8A BF / 字 8E 9A / 鬱 9F 54 は
;   先頭バイトが 0x80-0x9F の帯域内。熙 EA A4 だけ先頭バイトが 0xEA で
;   帯域外。もし出力側(CONドライバ等)が 0x80-0x9F を捨てているなら、
;   帯域内の6文字はすべて壊れ、帯域外の「熙」だけが正しく出るという
;   診断的なパターンになる。半角カナ ｱｲｳ (B1 B2 B3) は単純な1バイト
;   コードなので対照として添える。
;
; ビルド: nasm -f bin ESCT1.ASM -o ESCT1.COM

        cpu     8086
        org     0x100

ESC     equ     0x1B

start:
        ; ---- [1] AH=02h : 1バイトずつ出力 ----
        mov     dx, msg1_head
        mov     ah, 0x09
        int     0x21

        mov     si, kanji_bytes
        mov     cx, kanji_len
.l1:
        lodsb
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        loop    .l1

        mov     dx, msg_end1
        mov     ah, 0x09
        int     0x21

        ; ---- [2] AH=09h : 文字列出力で一括 ----
        mov     dx, msg2_head
        mov     ah, 0x09
        int     0x21

        mov     dx, kanji_dollar
        mov     ah, 0x09
        int     0x21

        mov     dx, msg_end2
        mov     ah, 0x09
        int     0x21

        ; ---- [3] attr : 反転表示 ----
        mov     dx, msg3
        mov     ah, 0x09
        int     0x21

        ; ---- [4] save/restore : カーソル位置の保存/復元 ----
        mov     dx, msg4
        mov     ah, 0x09
        int     0x21

        ; ---- [5] status line : ファンクションキー行を解放して最下行に出力 ----
        mov     dx, msg5
        mov     ah, 0x09
        int     0x21

        ; カーソルを本文側(6行目)へ明示的に戻す
        mov     dx, msg_back
        mov     ah, 0x09
        int     0x21

        ; ---- キー入力待ち ----
        mov     dx, msg_press
        mov     ah, 0x09
        int     0x21

        mov     ah, 0x08
        int     0x21

        ; ファンクションキー行を元に戻す
        mov     dx, msg_restore_fn
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4C00
        int     0x21

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------

; 全角7文字+半角カナ3文字。 [1] では1バイトずつ、[2] では下の $終端版を使う。
kanji_bytes:
        db 0x82, 0xA0            ; あ
        db 0x83, 0x41            ; ア
        db 0x88, 0x9F            ; 亜
        db 0x8A, 0xBF            ; 漢
        db 0x8E, 0x9A            ; 字
        db 0x9F, 0x54            ; 鬱
        db 0xEA, 0xA4            ; 熙 (帯域外・対照用)
        db 0xB1, 0xB2, 0xB3      ; 半角カナ ｱｲｳ
kanji_len equ $ - kanji_bytes

kanji_dollar:
        db 0x82, 0xA0            ; あ
        db 0x83, 0x41            ; ア
        db 0x88, 0x9F            ; 亜
        db 0x8A, 0xBF            ; 漢
        db 0x8E, 0x9A            ; 字
        db 0x9F, 0x54            ; 鬱
        db 0xEA, 0xA4            ; 熙 (帯域外・対照用)
        db 0xB1, 0xB2, 0xB3      ; 半角カナ ｱｲｳ
        db '$'

msg1_head:      db '[1] AH=02h : $'
msg_end1:       db ' <END1>', 0x0D, 0x0A, '$'

msg2_head:      db '[2] AH=09h : $'
msg_end2:       db ' <END2>', 0x0D, 0x0A, '$'

msg3:           db '[3] attr : ', ESC, '[7m', 'REVERSED', ESC, '[0m', ' normal <END3>', 0x0D, 0x0A, '$'

msg4:           db '[4] save/restore : ', ESC, '[s', ESC, '[1;1H', '#', ESC, '[u', '<END4>', 0x0D, 0x0A, '$'

msg5:           db ESC, '[>1h'
                ; 24行目(元から使える行)と25行目(ESC[>1h で解放される最下行)の
                ; 両方に書き、25行目が本当に使えるようになったかを区別する。
                db ESC, '[24;1H', '[5a] LINE24 <END5A>'
                db ESC, '[25;1H', '[5b] LINE25 <END5B>'
                db '$'

msg_back:       db ESC, '[6;1H', '$'

msg_press:      db '[press any key]$'

msg_restore_fn: db ESC, '[>1l', '$'
