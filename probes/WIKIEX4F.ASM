; WIKIEX4.COM - Wiki「DOSファンクション」の例をそのまま実行して確かめる検証用
; ファイルを作って16バイト書き、閉じて、開き直して読み、内容を照合して消す。
; 成功なら OK、失敗した段階を NG<番号> で表示する。
        cpu     8086
        org     0x100
start:
        ; ---- ここから Wiki 掲載のコード(書き込み側) ----
        mov     ah, 0x3C
        xor     cx, cx                  ; 属性=通常
        mov     dx, fname               ; DS:DX = ファイル名(0終端)
        int     0x21
        jc      .e1                     ; CF=1 ならエラー。AX にエラーコード
        mov     bx, ax                  ; AX = ファイルハンドル

        mov     ah, 0x40
        mov     cx, 16                  ; 書くバイト数
        mov     dx, wbuf
        int     0x21
        jc      .e2
        cmp     ax, 16                  ; 実際に書けた数を必ず確認する
        jne     .e3

        mov     ah, 0x3E                ; 閉じる
        int     0x21
        jc      .e4
        ; ---- ここまで ----

        ; 読み直して照合する
        mov     ah, 0x3D
        xor     al, al
        mov     dx, fname
        int     0x21
        jc      .e5
        mov     bx, ax
        mov     ah, 0x3F
        mov     cx, 16
        mov     dx, rbuf
        int     0x21
        jc      .e6
        cmp     ax, 16
        jne     .e7
        mov     ah, 0x3E
        int     0x21
        mov     si, wbuf
        mov     di, rbuf
        mov     cx, 16
.cmp:
        mov     al, [si]
        cmp     al, [di]
        jne     .e8
        inc     si
        inc     di
        loop    .cmp
        mov     ah, 0x41                ; 消す
        mov     dx, fname
        int     0x21
        jc      .e9
        mov     dx, msgok
        jmp     .out
.e1:    mov dx,m1
        jmp .out
.e2:    mov dx,m2
        jmp .out
.e3:    mov dx,m3
        jmp .out
.e4:    mov dx,m4
        jmp .out
.e5:    mov dx,m5
        jmp .out
.e6:    mov dx,m6
        jmp .out
.e7:    mov dx,m7
        jmp .out
.e8:    mov dx,m8
        jmp .out
.e9:    mov dx,m9
.out:
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C00
        int     0x21

fname:  db 'Z:\NOPE.TMP',0
msgok:  db 'RESULT=OK',0x0D,0x0A,'$'
m1:     db 'RESULT=NG1',0x0D,0x0A,'$'
m2:     db 'RESULT=NG2',0x0D,0x0A,'$'
m3:     db 'RESULT=NG3',0x0D,0x0A,'$'
m4:     db 'RESULT=NG4',0x0D,0x0A,'$'
m5:     db 'RESULT=NG5',0x0D,0x0A,'$'
m6:     db 'RESULT=NG6',0x0D,0x0A,'$'
m7:     db 'RESULT=NG7',0x0D,0x0A,'$'
m8:     db 'RESULT=NG8',0x0D,0x0A,'$'
m9:     db 'RESULT=NG9',0x0D,0x0A,'$'
wbuf:   db 'WIKIEX4-16BYTE!'
        db 0x0A
rbuf:   times 32 db 0xCC
