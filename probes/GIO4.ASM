; GIO4.COM - ホストから I/O・メモリ・BIOS呼び出し・画面出力を行うための橋(第4版)
;
; 第3版に「文字列を DOS の1文字出力(AH=02h)で流す」機能を足したもの。
; エスケープシーケンスをホストから自由に送って、画面の変化を測るために使う。
; AH=09h は DS:DX を使うためホストから文字列を置きにくいが、AH=02h なら DL だけで済む。
;
; 第2版(GIO2)に「任意の INT を呼ぶ」機能を足したもの。カーソル制御や画面モードの
; 切り替えは BIOS 経由なので、ポートを叩くだけでは測れない。
;
; 命令領域: 0xA1000
;   +0x00 word seq     ホストが増やすと実行される
;   +0x02 word ack     実行後にゲストが seq を書き戻す
;   +0x04 word n_io    I/O オペの個数(最大16)
;   +0x06 word n_fill  fill オペの個数(最大8)
;   +0x08 word n_int   INT オペの個数(最大8)
;   +0x0A word n_print 画面へ流すバイト数(最大256)
;   +0x100.. 流すバイト列
;   +0x10.. I/Oオペ 4バイト: port(w) value(b) op(b)   op 0=OUT8 1=IN8(結果をvalueへ)
;   +0x50.. fillオペ 8バイト: seg(w) off(w) len(w) val(b) pad(b)
;   +0x90.. INTオペ 16バイト: intno(b) flags_out(b) ax(w) bx(w) cx(w) dx(w) es(w) pad(w)
;            呼び出し後、ax/bx/cx/dx には返り値が、flags_out には FLAGS の下位が入る
;
;   実行順は I/O -> fill -> INT。
;
; ビルド: nasm -f bin GIO4.ASM -o GIO4.COM

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

        ; ---- I/O ----
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

        ; ---- fill ----
.fills:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     cx, [es:6]
        or      cx, cx
        jz      .prints
        mov     si, 0x50
.nextfill:
        push    cx
        push    si
        mov     ax, [es:si]
        mov     di, [es:si+2]
        mov     cx, [es:si+4]
        mov     bl, [es:si+6]
        mov     es, ax
        mov     al, bl
        rep     stosb
        mov     ax, CMD_SEG
        mov     es, ax
        pop     si
        pop     cx
        add     si, 8
        loop    .nextfill

        ; ---- 画面出力(AH=02h で1バイトずつ) ----
.prints:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     cx, [es:0x0A]
        or      cx, cx
        jz      .ints
        mov     si, 0x100
.nextch:
        push    cx
        push    si
        mov     ax, CMD_SEG
        mov     es, ax
        mov     dl, [es:si]
        mov     ah, 0x02
        int     0x21
        pop     si
        pop     cx
        inc     si
        loop    .nextch

        ; ---- INT ----
.ints:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     cx, [es:8]
        or      cx, cx
        jz      .ack
        mov     si, 0x90
.nextint:
        push    cx
        push    si

        mov     al, [es:si]             ; intno
        mov     [cs:.patch+1], al       ; 呼ぶ番号を書き換える
        mov     ax, [es:si+2]
        mov     bx, [es:si+4]
        mov     cx, [es:si+6]
        mov     dx, [es:si+8]
        push    ax
        mov     ax, [es:si+10]          ; 呼び出し時の ES
        mov     es, ax
        pop     ax
.patch:
        int     0x00                    ; ここが書き換わる

        push    ax
        mov     ax, CMD_SEG
        mov     es, ax
        pop     ax
        pop     si
        push    si
        mov     [es:si+2], ax
        mov     [es:si+4], bx
        mov     [es:si+6], cx
        mov     [es:si+8], dx
        pushf
        pop     ax
        mov     [es:si+1], al

        pop     si
        pop     cx
        add     si, 16
        loop    .nextint

.ack:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     ax, [es:0]
        mov     [es:2], ax
        jmp     .poll
