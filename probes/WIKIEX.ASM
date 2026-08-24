; WIKIEX.COM - Wiki「テキスト VRAM」の使用例をそのまま実行して確かめる検証用
; ビルド: nasm -f bin WIKIEX.ASM -o WIKIEX.COM
        cpu     8086
        org     0x100
start:
        ; ---- ここから Wiki 掲載のコードそのまま ----
        ; 前提: テキスト画面が表示されていること
        mov     ax, 0xA000      ; 文字コード面
        mov     es, ax
        mov     word [es:0], 0x0041     ; 'A'
        mov     word [es:2], 0x0042     ; 'B'

        mov     ax, 0xA200      ; 属性面
        mov     es, ax
        mov     word [es:0], 0x00E1     ; 白・表示あり
        mov     word [es:2], 0x00E1
        ; ---- ここまで ----

        mov     ah, 0x07                ; 画面を保持するためキー待ち
        int     0x21
        mov     ax, 0x4C00
        int     0x21
