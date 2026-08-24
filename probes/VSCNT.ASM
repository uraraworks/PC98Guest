; VSCNT.COM - 指定したポート・ビットの立ち上がりを数え続ける
;
; ホストが 0xA1010 に「見るポート(word)」と「ビットマスク(byte)」を書いてから
; 起動する。0xA1000 のカウンタ(32bit)を増やし続けるので、ホスト側で
; 実時間の経過と突き合わせれば周波数が出る。
;
; **時間はゲスト側で測らない。** ゲストで測るとポーリングが対象を支配したり
; 帰線を取り逃したりして、誤差の向きが条件で反転する。
;
; 終了できない(page reload でリセットする)。
;
; ビルド: nasm -f bin VSCNT.ASM -o VSCNT.COM

        cpu     8086
        org     0x100

CFG_SEG equ     0xA100

start:
        mov     ax, CFG_SEG
        mov     es, ax
        mov     dx, [es:0x10]           ; 見るポート
        mov     bl, [es:0x11+0]         ; (未使用: 上位バイト)
        mov     bl, [es:0x12]           ; ビットマスク
        xor     word [es:0], 0          ; カウンタは触らずそのまま使う
        mov     word [es:0], 0
        mov     word [es:2], 0

        xor     bh, bh                  ; 直前の状態
.loop:
        in      al, dx
        and     al, bl
        cmp     al, bh
        je      .loop
        mov     bh, al
        or      al, al
        jz      .loop                   ; 0→1 の立ち上がりだけ数える

        mov     ax, CFG_SEG
        mov     es, ax
        add     word [es:0], 1
        adc     word [es:2], 0
        jmp     .loop
