; WIKIEX2.COM - Wiki「キーボード」の使用例をそのまま実行して確かめる検証用
        cpu     8086
        org     0x100
start:
        ; ---- ここから Wiki 掲載のコードそのまま ----
.loop:
        mov     ah, 0x00
        int     0x18            ; AH=スキャンコード AL=文字コード
        cmp     al, 0x1B        ; ESC で終了
        je      .done
        or      al, al
        jz      .loop           ; AL=0 は文字を持たないキー。ここでは読み飛ばす
        mov     dl, al
        mov     ah, 0x02
        int     0x21            ; DOS の1文字出力
        jmp     .loop
.done:
        ; ---- ここまで ----
        mov     ax, 0x4C00
        int     0x21
