; RAWRD18.COM - PC-98 INT 18h(キーボードBIOS)ハンドラ内からINT 1Bhを呼べるか測定するTSR
;
; 目的:
;   RAWRD.COM は「プログラム本体から」INT 1Bhで生セクタが読めるかを測った。
;   本TSRは一歩進めて「INT 18h ハンドラの中から」同じことができるかを測る。
;   AH=00h(キー1つ取得)が呼ばれた瞬間に、元のINT 18h処理でキーを取得した
;   直後にINT 1BhでLBA0のセクタを読み、結果を常駐データへ記録する。
;
;   INT 18h はDOSのCONドライバ内部から呼ばれうる。つまりハンドラは「DOSの中」
;   で動作している可能性があるため、ハンドラ内から INT 21h は一切呼ばない
;   (画面出力もしない。記録はメモリだけ。K18SPY.ASMの作法を踏襲)。
;
; 常駐確認: AX=E7EDh で呼ぶと AX=5231h('R1') を返す。
;
; ホスト側は物理メモリを 'RAWRD18!' の8バイトシグネチャで走査して本TSRを
; 見つけ、その直後を下記レイアウトで読む(隙間なく並べること):
;   db 'RAWRD18!'   ; シグネチャ8バイト
;   dw 呼び出し回数
;   db 最後のCF (0/1)
;   db 最後のステータスAH
;   db 8バイト       ; 読めたデータの先頭8バイト
;
; INT 1Bh の呼び出しパラメータはRAWRD.ASMと同じ(AH=0x66, AL(DA/UA)=0x91,
; LBA0 -> CHS(0,0,1))。
;
; ビルド: nasm -f bin RAWRD18.ASM -o RAWRD18.COM

        cpu     8086
        org     0x100

IDENT_FUNC        equ     0xE7ED
IDENT_REPLY       equ     0x5231

SECTOR_LEN_N      equ     3          ; 1024バイト
SECTOR_BYTES      equ     1024
DA_UA             equ     0x91       ; 1MBインターフェース・ユニット1 (FDD2/B:)
READ_AH           equ     0x76       ; MT0/MF1/f1(リトライなし)/SEEK1/コマンド6(読み出し)
                                     ; SEEKビット(D4)は必須。0x66(SEEK=0)だと実測で
                                     ; バッファに1バイトも書かれない(0xCC埋めのまま)。

start:  jmp     install

;--------------------------------------------------------------------
; 常駐データ
;--------------------------------------------------------------------
        align   2
oldint18:       dd      0

; ホスト側が探すシグネチャ。この直後のレイアウトを崩さないこと。
sig:            db      'RAWRD18!'
call_count:     dw      0
last_cf:        db      0
last_status_ah: db      0
last_data8:     times 8 db 0

; INT 1Bh用の読み出しバッファ(常駐データ内に確保。1セクタ分)
read_buffer:    times SECTOR_BYTES db 0

;--------------------------------------------------------------------
; INT 18h ハンドラ
;--------------------------------------------------------------------
newint18:
        cmp     ax, IDENT_FUNC
        je      .ident

        cmp     ah, 0x00
        jne     .passthrough_only

        ; ---- AH=00h: 元のINT 18hでキーを1つ取得してからINT 1Bhを試す ----
        pushf
        call    far [cs:oldint18]       ; ここでキーを1つ取得(結果はAX)
        push    ax                      ; 取得したキーを退避(呼び出し元へ素通しするため)

        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        mov     ax, cs
        mov     ds, ax
        mov     es, ax

        ; call_countをインクリメント(0xFFFFで飽和)
        cmp     word [cs:call_count], 0xFFFF
        je      .no_inc
        inc     word [cs:call_count]
.no_inc:

        ; INT 1Bh: LBA0 -> cyl=0, head=0, sec=1 を1セクタ読む
        mov     bp, read_buffer
        mov     ah, READ_AH
        mov     al, DA_UA
        mov     bx, SECTOR_BYTES
        mov     ch, SECTOR_LEN_N
        mov     cl, 0
        mov     dh, 0
        mov     dl, 1
        int     0x1B

        jc      .read_err
        mov     byte [cs:last_cf], 0
        mov     byte [cs:last_status_ah], ah
        jmp     .copy_head

.read_err:
        mov     byte [cs:last_cf], 1
        mov     byte [cs:last_status_ah], ah

.copy_head:
        mov     si, read_buffer
        mov     di, last_data8
        mov     cx, 8
        cld
        rep     movsb

        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx

        pop     ax                      ; 取得したキーを復元
        iret                            ; 呼び出し元(元のint 18h発行元)へキー(AX)を返す

.passthrough_only:
        jmp     far [cs:oldint18]

.ident:
        mov     ax, IDENT_REPLY
        iret

;--------------------------------------------------------------------
; 常駐範囲はここまで。この行より後ろに、常駐後に参照するデータを
; 一切置かないこと(install以降はTSR化と同時に破棄される)。
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
        mov     ax, 0x3518              ; 元のINT 18hベクタを取得
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

msg_ok:         db      'RAWRD18 resident.', 0x0D, 0x0A, '$'
msg_already:    db      'RAWRD18 already resident.', 0x0D, 0x0A, '$'
