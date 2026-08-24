; GIO2.COM - ホストから I/O ポートとメモリを叩くための橋(第2版)
;
; 第1版(GIO.COM)は I/O ポートだけを扱い、VRAM の書き込みはホストの
; writeMemoryBase64 で行うつもりだった。しかし **ホストから直接書いても画面が
; 再描画されない**。メモリの内容自体は変わる(読み戻すと書いた値が返る)が、
; エミュレータ側の「変化した」判定を通らないため描画が古いまま残る。
; そのため、メモリの書き込みもゲスト側で行う必要がある。
;
; 命令領域: 0xA1000（テキストVRAMの後ろ。画面に出ないことを実測済み）
;   +0x00 word seq     ホストが増やすと実行される
;   +0x02 word ack     実行後にゲストが seq を書き戻す
;   +0x04 word n_io    I/O オペの個数(最大16)
;   +0x06 word n_fill  fill オペの個数(最大16)
;   +0x10.. I/Oオペ 4バイト: port(w) value(b) op(b)   op 0=OUT8 1=IN8(結果をvalueへ)
;   +0x50.. fillオペ 8バイト: seg(w) off(w) len(w) val(b) pad(b)
;
;   実行順は I/O -> fill。逆順が要るときは2回に分けて発行する。
;
; 終了できない(page reload でリセットする)。
;
; ビルド: nasm -f bin GIO2.ASM -o GIO2.COM

        cpu     8086
        org     0x100

CMD_SEG equ     0xA100

start:
        mov     ax, CMD_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 256
        rep     stosw

.poll:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     ax, [es:0]
        cmp     ax, [es:2]
        je      .poll

        ; ---- I/O オペ ----
        mov     cx, [es:4]
        or      cx, cx
        jz      .fills
        mov     si, 0x10
.nextio:
        mov     dx, [es:si]
        mov     al, [es:si+2]
        mov     ah, [es:si+3]
        cmp     ah, 1
        je      .doin
        out     dx, al
        jmp     .stepio
.doin:
        in      al, dx
        mov     [es:si+2], al
.stepio:
        add     si, 4
        loop    .nextio

.fills:
        mov     ax, CMD_SEG             ; ES は fill で壊すので入れ直す
        mov     es, ax
        mov     cx, [es:6]
        or      cx, cx
        jz      .ack
        mov     si, 0x50
.nextfill:
        push    cx
        push    si
        mov     ax, [es:si]             ; seg
        mov     di, [es:si+2]           ; off
        mov     cx, [es:si+4]           ; len
        mov     bl, [es:si+6]           ; val
        mov     es, ax                  ; ここで ES が書き込み先になる
        mov     al, bl
        rep     stosb
        mov     ax, CMD_SEG
        mov     es, ax
        pop     si
        pop     cx
        add     si, 8
        loop    .nextfill

.ack:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     ax, [es:0]
        mov     [es:2], ax
        jmp     .poll
