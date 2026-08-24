; TSR60.COM - 常駐(TSR)の作り方を測るプログラム
;
; INT 60h に自前のハンドラを置いて常駐する。ハンドラは
;   AX = 0x1234（自分が生きている印）
;   BX = 呼ばれた回数（常駐データが保たれているかの確認）
; を返す。
;
; 常駐サイズは「常駐させたい最後のラベル」から求める。ラベルの順序が
; そのまま常駐範囲になるので、常駐させたいデータは resident_end より前に置く。
;
; ビルド: nasm -f bin TSR60.ASM -o TSR60.COM

        cpu     8086
        org     0x100

start:
        ; ---- INT 60h に自分のハンドラを設定する ----
        mov     ax, 0x2560              ; AH=25h ベクタ設定 / AL=60h
        mov     dx, handler             ; DS:DX = ハンドラ (COM なので DS=CS)
        int     0x21

        mov     dx, msg
        mov     ah, 0x09
        int     0x21

        ; ---- 常駐して終了する ----
        mov     dx, 16                  ; わざと小さすぎる値(故障注入)
        nop
        nop
        nop
        mov     ax, 0x3100              ; AH=31h 常駐終了
        int     0x21

; ---------------------------------------------------------------
; INT 60h ハンドラ。ここから resident_end までが常駐する範囲。
; ---------------------------------------------------------------
handler:
        push    ds
        push    cs
        pop     ds
        inc     word [counter]
        mov     bx, [counter]
        mov     ax, 0x1234
        pop     ds
        iret

counter:
        dw      0

resident_end:

; ---- ここから下は常駐しない(常駐後は上書きされてよい) ----
msg:    db 'TSR60S resident(bad size).',0x0D,0x0A,'$'
