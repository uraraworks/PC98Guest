; GIO.COM - ホストから I/O ポートを叩くための橋
;
; 目的:
;   グラフィック周りは「どのポートに何を書けば有効になるか」を試行錯誤で
;   探すことになる。毎回 .COM を書いてディスクを作り直すのは遅いので、
;   ホストが書いた命令列を実行するだけの常駐ループを置く。
;
;   ホスト側は np2.writeMemoryBase64() で命令列を書き、seq を増やすと
;   ゲストがそれを実行して ack を返す。VRAM の読み書きはホストから
;   readMemoryBase64/writeMemoryBase64 で直接できるので、ここでは扱わない。
;
; 命令領域: 0xA1000（テキストVRAMの後ろ。画面に出ないことを実測済み）
;   +0  word  seq    ホストが増やすと実行される
;   +2  word  ack    実行後にゲストが seq をそのまま書き戻す
;   +4  word  count  命令の個数
;   +6.. 命令(4バイト単位)
;         +0 word port
;         +2 byte value   op=1(IN)のときは結果がここに入る
;         +3 byte op      0 = OUT(8bit) / 1 = IN(8bit)
;
; 終了できない(page reload でリセットする)。
;
; ビルド: nasm -f bin GIO.ASM -o GIO.COM

        cpu     8086
        org     0x100

CMD_SEG equ     0xA100

start:
        ; 命令領域を 0 で潰してから待機に入る
        mov     ax, CMD_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 256
        rep     stosw

.poll:
        mov     ax, CMD_SEG
        mov     es, ax
        mov     ax, [es:0]              ; seq
        cmp     ax, [es:2]              ; ack
        je      .poll                   ; 変化なし

        mov     cx, [es:4]              ; count
        or      cx, cx
        jz      .ack
        mov     si, 6                   ; 命令の先頭
.next:
        mov     dx, [es:si]             ; port
        mov     al, [es:si+2]           ; value
        mov     ah, [es:si+3]           ; op
        cmp     ah, 1
        je      .doin
        out     dx, al
        jmp     .step
.doin:
        in      al, dx
        mov     [es:si+2], al
.step:
        add     si, 4
        loop    .next

.ack:
        mov     ax, [es:0]
        mov     [es:2], ax
        jmp     .poll
