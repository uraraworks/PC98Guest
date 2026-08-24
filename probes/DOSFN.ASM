; DOSFN.COM - DOS ファンクション(INT 21h)の挙動を測る
;
; 目的:
;   よく使う INT 21h の呼び出しについて、返り値・キャリーフラグ・
;   エラー時の値を実測する。FreeDOS(98) と NEC MS-DOS の両方で走らせて
;   差分を取ることを想定している。
;
; 結果の置き場: 0xA1000 から 8バイト x N
;   テキストVRAMの後ろで、画面に出ないことを確認済みの領域(番地固定なので
;   ホストから readMemoryBase64 で読める)。1件は AX,BX,CX,DX の4ワード。
;   DX には原則「INT 直後の FLAGS」を入れる(例外は下記 T11)。
;
;   T0  AH=30h  DOSバージョン        AX=AL:major/AH:minor  BX:CX=OEM等
;   T1  AH=19h  カレントドライブ      AL=0がA:
;   T2  AH=3Ch  ファイル作成          AX=ハンドル or エラー
;   T3  AH=40h  書き込み(16バイト)    AX=書けたバイト数
;   T4  AH=3Eh  クローズ
;   T5  AH=3Dh  読み出しで開く         AX=ハンドル
;   T6  AH=3Fh  読み込み(16バイト)    AX=読めたバイト数
;   T7  読み込みバッファの中身(4ワード) ※INT呼び出しではない
;   T8  AH=3Eh  クローズ
;   T9  AH=3Dh  存在しないファイルを開く → エラーの出方を見る
;   T10 AH=41h  削除
;   T11 AH=2Ah  日付取得  ※DXは FLAGS ではなく DH=月/DL=日
;   T12 0xDEAD  終端マーカー
;
; 読み込みバッファは 0xCC で埋めてある。「書かれなかった」と
; 「0が書かれた」を区別するため。
;
; ビルド: nasm -f bin DOSFN.ASM -o DOSFN.COM

        cpu     8086
        org     0x100

OUT_SEG equ     0xA100          ; 0xA1000

start:
        ; ---- 結果置き場を 0 で潰す(前回実行の残骸と区別するため) ----
        mov     ax, OUT_SEG
        mov     es, ax
        xor     di, di
        xor     ax, ax
        mov     cx, 128
        rep     stosw
        mov     word [outp], 0

        ; ---- T0: DOSバージョン ----
        mov     ah, 0x30
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T1: カレントドライブ ----
        mov     ah, 0x19
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T2: ファイル作成 ----
        mov     ah, 0x3C
        xor     cx, cx                  ; 属性=通常
        mov     dx, fname
        int     0x21
        mov     [handle], ax
        pushf
        pop     dx
        call    rec

        ; ---- T3: 16バイト書く ----
        mov     bx, [handle]
        mov     ah, 0x40
        mov     cx, 16
        mov     dx, wdata
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T4: クローズ ----
        mov     bx, [handle]
        mov     ah, 0x3E
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T5: 読み出しで開く ----
        mov     ah, 0x3D
        xor     al, al                  ; 0=読み出し
        mov     dx, fname
        int     0x21
        mov     [handle], ax
        pushf
        pop     dx
        call    rec

        ; ---- T6: 16バイト読む ----
        mov     bx, [handle]
        mov     ah, 0x3F
        mov     cx, 16
        mov     dx, rbuf
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T7: 読めた中身をそのまま記録 ----
        mov     ax, [rbuf]
        mov     bx, [rbuf+2]
        mov     cx, [rbuf+4]
        mov     dx, [rbuf+6]
        call    rec

        ; ---- T8: クローズ ----
        mov     bx, [handle]
        mov     ah, 0x3E
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T9: 存在しないファイルを開く ----
        mov     ah, 0x3D
        xor     al, al
        mov     dx, fnone
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T10: 削除 ----
        mov     ah, 0x41
        mov     dx, fname
        int     0x21
        pushf
        pop     dx
        call    rec

        ; ---- T11: 日付(DX は FLAGS ではなく DH=月 DL=日) ----
        mov     ah, 0x2A
        int     0x21
        call    rec

        ; ---- T12: 終端マーカー ----
        mov     ax, 0xDEAD
        xor     bx, bx
        xor     cx, cx
        xor     dx, dx
        call    rec

        mov     ax, 0x4C00
        int     0x21

; ---------------------------------------------------------------
; rec: AX,BX,CX,DX を4ワードで記録する。呼び出し側のレジスタは壊さない。
; ---------------------------------------------------------------
rec:
        push    ax
        push    di
        mov     ax, OUT_SEG
        mov     es, ax
        pop     di
        pop     ax
        push    di
        mov     di, [outp]
        mov     [es:di], ax
        mov     [es:di+2], bx
        mov     [es:di+4], cx
        mov     [es:di+6], dx
        add     word [outp], 8
        pop     di
        ret

fname:  db 'DOSFN.TMP',0
fnone:  db 'NOSUCH.XYZ',0
wdata:  db 'DOSFNTEST-16BYT'
        db 0x0A
outp:   dw 0
handle: dw 0
rbuf:   times 32 db 0xCC
