; VSWAIT.COM - Wiki「時間と割り込み」の垂直帰線待ちをそのまま実行して確かめる検証用
;
; 200回待って、かかった実時間をホスト側で測る。56.4Hz なら約3.55秒になるはず。
; 単に「戻ってくる」だけでは待てている証明にならないので、回数と時間で確かめる。
;
; 0xA1000: +0 開始マーカー(0xB00B) +2 完了マーカー(0xDA7A) +4 待った回数
;
; ビルド: nasm -f bin VSWAIT.ASM -o VSWAIT.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100
COUNT   equ     200

start:
        mov     ax, OUT_SEG
        mov     es, ax
        mov     word [es:0], 0xB00B     ; 開始
        mov     word [es:2], 0
        mov     word [es:4], 0

        mov     cx, COUNT
.loop:
        push    cx
        call    waitvsync
        pop     cx
        push    cx
        mov     ax, OUT_SEG
        mov     es, ax
        inc     word [es:4]
        pop     cx
        loop    .loop

        mov     ax, OUT_SEG
        mov     es, ax
        mov     word [es:2], 0xDA7A     ; 完了

        mov     ax, 0x4C00
        int     0x21

; ---------------------------------------------------------------
; waitvsync: 垂直帰線が始まるまで待つ
;   ポート 0x60 の bit5 が垂直帰線。いま帰線中なら一度抜けるのを待ってから、
;   次に始まるところで返る（そうしないと同じ帰線で何度も返ってしまう）。
; ---------------------------------------------------------------
waitvsync:
.busy:
        in      al, 0x60
        and     al, 0x20
        jnz     .busy                   ; 帰線が終わるのを待つ
.idle:
        in      al, 0x60
        and     al, 0x20
        jz      .idle                   ; 次の帰線が始まるのを待つ
        ret
