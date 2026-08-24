; EXITCD.COM - 指定した終了コードで終わる
;
; 終了コードは AH=4Ch の AL で渡す。呼び出し元(COMMAND.COM)からは
; ERRORLEVEL として見えるはず、というのを確かめるための道具。
; 終了コードはホストが 0xA1010 に置く(下位バイトを使う)。
;
; ビルド: nasm -f bin EXITCD.ASM -o EXITCD.COM

        cpu     8086
        org     0x100

start:
        mov     ax, 0xA100
        mov     es, ax
        mov     al, [es:0x10]
        mov     ah, 0x4C
        int     0x21
