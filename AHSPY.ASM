; AHSPY.COM - PC-98 / MS-DOS INT 21h ファンクション呼び出し回数カウンタ TSR
;
; 目的:
;   FreeDOS(98) の COMMAND.COM (FreeCOM) がコマンドライン入力にどの
;   DOS ファンクション(AH)を使っているのかを特定するための計測ツール。
;   NEC MS-DOS の COMMAND.COM は AH=0Ah(バッファ入力)を使うが、FreeCOM は
;   別のファンクションを使っているらしいことが実測でわかっている。
;
;   INT 21h をフックし、AH=00h〜FFh それぞれの呼び出し回数を256要素の
;   カウンタ配列に記録してから元のハンドラへ素通しする。
;   ホスト(JS)側は物理メモリを 'AHSPY001' の8バイトシグネチャで走査して
;   本TSRを見つけ、その直後の512バイトをu16リトルエンディアンの
;   カウンタ配列として読む。
;
; 常駐確認: AX=E7EAh で呼ぶと AX=4153h('AS') を返す。
;
; 常駐の作法・INT 21h フック・多重常駐チェックは ESCT2.ASM をそのまま踏襲する。
;
; ビルド: nasm -f bin AHSPY.ASM -o AHSPY.COM

        cpu     8086
        org     0x100

IDENT_FUNC      equ     0xE7EA
IDENT_REPLY     equ     0x4153

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint21:       dd      0

; ホスト側が探すシグネチャ。この直後に counters が隙間なく続くこと。
sig:            db      'AHSPY001'
counters:       times 256 dw 0

;--------------------------------------------------------------------
; INT 21h ハンドラ
;--------------------------------------------------------------------
newint21:
        cmp     ax, IDENT_FUNC
        je      .ident

        ; counters[AH] をインクリメント(0xFFFFで飽和)。レジスタ・フラグは
        ; 一切変更せず、素通しする元処理へそのまま渡す。
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
        jmp     far [cs:oldint21]

.ident:
        mov     ax, IDENT_REPLY
        iret

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
        mov     ax, 0x3521              ; 元の INT 21h ベクタを取得
        int     0x21
        mov     [oldint21], bx
        mov     [oldint21 + 2], es

        mov     dx, newint21            ; 自前ハンドラを設定
        mov     ax, 0x2521
        int     0x21

        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     dx, (install - start + 0x100 + 15) / 16 ; PSPを含む常駐パラグラフ数
        mov     ax, 0x3100              ; 常駐終了
        int     0x21

msg_ok:         db      'AHSPY resident.', 0x0D, 0x0A, '$'
msg_already:    db      'AHSPY already resident.', 0x0D, 0x0A, '$'
