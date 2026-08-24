; SENSE.COM - INT 18h AH=01h(キーのセンス)が何を返すか測る
;
; 目的:
;   AH=01h の仕様は未確認。返るレジスタとフラグを、キーが無い状態と
;   ある状態の両方で観測する。キーは消費しない(AH=00h を呼ばない)ので、
;   一度積まれたキーは積まれたままになり、状態が安定して観測できる。
;
; 結果の置き場: 0xA1000〜
;   テキストVRAMの後ろで、実測で「画面に出ない」ことを確認済みの領域。
;   番地が固定なので、ホストから readMemoryBase64(0xA1000, ...) で読める。
;   +0 AX / +2 BX / +4 CX / +6 DX / +8 FLAGS / +10 ループ回数
;
; ビルド: nasm -f bin SENSE.ASM -o SENSE.COM
        cpu     8086
        org     0x100

OUT_SEG equ     0xA100          ; 0xA1000

start:
.loop:
        mov     ah, 0x01
        int     0x18

        push    ax
        mov     ax, OUT_SEG     ; INT が es を壊しても大丈夫なよう毎回入れ直す
        mov     es, ax
        pop     ax

        mov     [es:0], ax
        mov     [es:2], bx
        mov     [es:4], cx
        mov     [es:6], dx
        pushf
        pop     ax
        mov     [es:8], ax
        inc     word [es:10]
        jmp     .loop
