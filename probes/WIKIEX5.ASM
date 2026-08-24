; WIKIEX5.COM - Wiki「グラフィック画面」の例をそのまま実行して確かめる検証用
; グラフィックを表示し、青プレーンに対角線を引く。キーを押すと消して終了。
        cpu     8086
        org     0x100

BLUE    equ     0xA800          ; 青プレーン
RED     equ     0xB000
GREEN   equ     0xB800
INTEN   equ     0xE000          ; 輝度(16色モードのとき有効)

start:
        ; ---- ここから Wiki 掲載のコード ----
        mov     al, 0x01
        out     0x6A, al                ; 16色モードにする
        mov     al, 0x0D
        out     0xA2, al                ; グラフィック表示を開始する

        ; 4プレーンを消す(1プレーン32000バイト)
        mov     si, planes
        mov     cx, 4
.clr:
        push    cx
        lodsw
        mov     es, ax
        xor     di, di
        xor     al, al
        mov     cx, 32000
        rep     stosb
        pop     cx
        loop    .clr

        ; 対角線を引く
        xor     bx, bx                  ; y
.line:
        mov     ax, bx                  ; x = y
        call    plot
        inc     bx
        cmp     bx, 200
        jb      .line
        ; ---- ここまで ----

        mov     ah, 0x00                ; キー待ち
        int     0x18

        mov     al, 0x0C
        out     0xA2, al                ; グラフィック表示を止める
        mov     ax, 0x4C00
        int     0x21

; ---------------------------------------------------------------
; plot: AX=x, BX=y の位置に青プレーンで点を打つ
;   オフセット = y * 80 + x / 8
;   ビット      = 0x80 >> (x & 7)     ※ bit7 が左端
; ---------------------------------------------------------------
plot:
        push    ax
        push    bx
        push    cx
        push    dx
        push    di

        mov     cx, ax
        and     cx, 7
        mov     dl, 0x80
        shr     dl, cl                  ; ビットマスク
        push    dx                      ; MUL は DX を壊すので退避しておく

        mov     cl, 3
        shr     ax, cl                  ; x / 8
        mov     di, ax

        mov     ax, bx
        mov     cx, 80
        mul     cx                      ; DX:AX = y * 80
        add     di, ax

        pop     dx                      ; ビットマスクを戻す
        mov     ax, BLUE
        mov     es, ax
        or      [es:di], dl

        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

planes: dw BLUE, RED, GREEN, INTEN
