; VSINT2.COM - 垂直帰線の割り込み(IRQ2 = INT 0Ah)を数える(ポート0x64で許可する版)
;
; VSINT.COM ではベクタを置いてマスクを外しても1回も来なかった。
; 垂直帰線割り込みはポート 0x64 への書き込みで許可・再武装するらしいので、
; 起動時とハンドラの中で 0x64 を叩く。
;
; PC-98 では IRQ2 が CRT の垂直帰線に割り当てられている。
; マスクを外して数え、ホスト側の実時間と突き合わせる。
; ポート 0x60 の bit5 をポーリングして測った 56.4Hz と一致するはず。
;
; TIMCNT.COM ではカウンタが1つも進まなかった。ベクタは置き換わっていたので、
; 割り込みそのものが来ていない。PIC のマスクレジスタ(ポート 0x02)を読むと
; bit0 が 1 = IRQ0(タイマ) がマスクされていた。ここではマスクを外して確かめる。
;
; 元のハンドラを退避してから自分のハンドラを入れ、数えたあと元へ渡す(チェイン)。
; **元へ渡さないと PIC への EOI が出ず、以降の割り込みが止まる。**
;
; カウンタ: 0xA1000 に32ビット。ホスト側で実時間と突き合わせて周波数を出す。
; 時間はゲストで測らない(ポーリングが対象を支配する / 取り逃す)。
;
; 終了できない(page reload でリセットする)。
;
; ビルド: nasm -f bin VSINT2.ASM -o VSINT2.COM

        cpu     8086
        org     0x100

CFG_SEG equ     0xA100
VECT    equ     0x0A

start:
        ; カウンタを0にする
        mov     ax, CFG_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        stosw
        stosw

        ; 元のベクタを取っておく
        mov     ax, 0x3500 | VECT       ; AH=35h ベクタ取得
        int     0x21
        mov     [old_off], bx
        mov     [old_seg], es

        ; 自分のハンドラを入れる
        push    ds
        mov     ax, 0x2500 | VECT       ; AH=25h ベクタ設定
        mov     dx, handler
        int     0x21
        pop     ds

        ; IRQ2 のマスクを外す(bit2 を落とす)
        in      al, 0x02
        mov     [savedmask], al
        and     al, 0xFB                ; bit2 = IRQ2 のマスクを落とす
        out     0x02, al
        xor     al, al
        out     0x64, al                ; 垂直帰線割り込みを許可する
        sti

.hang:
        jmp     .hang

handler:
        push    ds
        push    ax
        mov     ax, CFG_SEG
        mov     ds, ax
        add     word [0], 1
        adc     word [2], 0
        xor     al, al
        out     0x64, al                ; 次の帰線に備えて再武装する
        pop     ax
        pop     ds
        jmp     far [cs:old_off]        ; 元のハンドラへ渡す

old_off: dw 0
old_seg: dw 0
savedmask: db 0
