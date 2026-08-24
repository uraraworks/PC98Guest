; TIMCNT3.COM - タイマの周期を変えて割り込みを数える
;
; 8253 のチャネル0(タイマ割り込み)に分周値を設定してから数える。
; 分周値はホストが 0xA1010 に置く(ワード)。0 のときは設定を変えない。
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
; ビルド: nasm -f bin TIMCNT3.ASM -o TIMCNT3.COM

        cpu     8086
        org     0x100

CFG_SEG equ     0xA100
VECT    equ     0x08

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

        ; ホストが置いた分周値で 8253 チャネル0 を設定する
        mov     ax, CFG_SEG
        mov     es, ax
        mov     bx, [es:0x10]
        or      bx, bx
        jz      .nodiv
        mov     al, 0x36                ; チャネル0・16bit・モード3
        out     0x77, al
        mov     al, bl
        out     0x71, al                ; 下位
        mov     al, bh
        out     0x71, al                ; 上位
.nodiv:

        ; IRQ0 のマスクを外す(bit0 を落とす)
        in      al, 0x02
        mov     [savedmask], al
        and     al, 0xFE
        out     0x02, al
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
        pop     ax
        pop     ds
        jmp     far [cs:old_off]        ; 元のハンドラへ渡す

old_off: dw 0
old_seg: dw 0
savedmask: db 0
