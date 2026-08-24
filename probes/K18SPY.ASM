; K18SPY.COM - PC-98 / MS-DOS INT 18h (キーボード BIOS) ファンクション呼び出し回数カウンタ TSR
;
; 目的:
;   INT 18h をフックし、AH=00h〜FFh それぞれの呼び出し回数を256要素の
;   カウンタ配列に記録してから元のハンドラへ素通しする。
;   ホスト(JS)側は物理メモリを 'K18SPY01' の8バイトシグネチャで走査して
;   本TSRを見つけ、その直後の512バイトをu16リトルエンディアンの
;   カウンタ配列として読む。
;
;   INT 18h は DOS の CON ドライバ内部から呼ばれうる。つまりハンドラは
;   「DOS の中」で動作している可能性があるため、ハンドラ内から INT 21h は
;   一切呼ばない(カウンタ加算のみ行い素通しする)。
;
; 常駐確認: AX=E7EBh で呼ぶと AX=4B31h('K1') を返す。
;
; 常駐の作法・INT 21h ベクタ操作に相当する処理・多重常駐チェックは
; AHSPY.ASM (INT 21h 版) を踏襲し、フック対象のみ INT 18h に差し替える。
;
; ビルド: nasm -f bin K18SPY.ASM -o K18SPY.COM

        cpu     8086
        org     0x100

IDENT_FUNC      equ     0xE7EB
IDENT_REPLY     equ     0x4B31

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint18:       dd      0

; ホスト側が探すシグネチャ。この直後に counters が隙間なく続くこと。
sig:            db      'K18SPY01'
counters:       times 256 dw 0

;--------------------------------------------------------------------
; INT 18h ハンドラ
;--------------------------------------------------------------------
newint18:
        cmp     ax, IDENT_FUNC
        je      .ident

        ; counters[AH] をインクリメント(0xFFFFで飽和)。レジスタ・フラグは
        ; 一切変更せず、素通しする元処理へそのまま渡す。
        ; 注意: INT 18h は DOS 内部から呼ばれうるため、ここでは INT 21h を
        ; 一切呼び出さない。
        pushf
        push    ax
        push    bx
        push    ds

        mov     bx, cs
        mov     ds, bx
        xor     bh, bh
        mov     bl, ah                  ; ah は push ax で退避済みのまま不変
        shl     bx, 1
        cmp     word [counters + bx], 0xFFFF
        je      .no_inc
        inc     word [counters + bx]
.no_inc:
        pop     ds
        pop     bx
        pop     ax
        popf
        jmp     far [cs:oldint18]

.ident:
        mov     ax, IDENT_REPLY
        iret

;--------------------------------------------------------------------
; 常駐範囲はここまで。この行より後ろに、常駐後に参照するデータを
; 一切置かないこと(install 以降は TSR 化と同時に破棄される)。
;--------------------------------------------------------------------
resident_end:

;--------------------------------------------------------------------
; インストール処理 (常駐後は破棄される)
;--------------------------------------------------------------------
install:
        mov     ax, IDENT_FUNC
        int     0x21
        cmp     ax, IDENT_REPLY
        jne     .install_now
        mov     dx, msg_already
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

.install_now:
        mov     ax, 0x3518              ; 元の INT 18h ベクタを取得
        int     0x21
        mov     [oldint18], bx
        mov     [oldint18 + 2], es

        mov     dx, newint18            ; 自前ハンドラを設定
        mov     ax, 0x2518
        int     0x21

        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     dx, (resident_end - start + 0x100 + 15) / 16 ; PSPを含む常駐パラグラフ数
        mov     ax, 0x3100              ; 常駐終了
        int     0x21

msg_ok:         db      'K18SPY resident.', 0x0D, 0x0A, '$'
msg_already:    db      'K18SPY already resident.', 0x0D, 0x0A, '$'
