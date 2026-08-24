; RAWRD.COM - PC-98 INT 1Bh (フロッピーディスクBIOS) 生セクタ読み出し測定プログラム
;
; 目的:
;   辞書をメモリに全部載せる方式をやめ、当時のFEPのように「ディスクに置いて
;   必要な所だけ読む」方式にできるかを確かめる土台。まずINT 1Bhで生セクタが
;   読めるかどうかを単体で測る(常駐しない)。
;
; 使い方: RAWRD <LBA10進>  (引数省略時はLBA=0)
;
; 対象ディスクの形状 (build-disk.mjsが作るout/esctest.xdf):
;   1024バイト/セクタ、8セクタ/トラック、2ヘッド、77シリンダ (計1232セクタ)
;   FreeDOS起動時はFDD2(B:)に入る。DA/UAは1MBインターフェースのユニット1(0x91)
;
;   LBA -> CHS:
;     cyl  = lba / (8 * 2)
;     head = (lba / 8) % 2
;     sec  = (lba % 8) + 1        ; セクタは1起点
;
; INT 1Bh 仕様 (『PC-9801プログラマーズBible』2-9-3。推測しないこと):
;   AH = BIOSコマンド識別コード
;        下位4bit(D0-D3) = コマンド。06H = データの読み出し
;        上位4bit(D4-D7) = D7:MT(マルチトラック) D6:MF(1=倍密度MFM)
;                          D5:f(1=リトライなし) D4:SEEK(1=シークを行う)
;   AL = デバイスアドレス/ユニット番号(DA/UA)。0x91 = 1MBインターフェース・ユニット1
;   BX = 転送データ長(バイト単位)
;   CH = セクタ長(N値。3 = 1024バイト)
;   CL = 開始シリンダ番号
;   DH = ヘッド番号(0または1)
;   DL = 開始セクタ番号(1起点)
;   ES:BP = データバッファ領域の先頭アドレス
; 出力:
;   CF = 0:正常終了 / 1:異常終了
;   AH = ステータス情報
;
; このプログラムでは AH = 0x66 (0110 0110 = MT0/MF1/f0/SEEK1/コマンド6) を使う。
; うまくいかなければ他の組み合わせに変えて試すこと。
;
; ビルド: nasm -f bin RAWRD.ASM -o RAWRD.COM

        cpu     8086
        org     0x100

SECTORS_PER_TRACK equ 8
HEADS             equ 2
SECTOR_LEN_N      equ 3          ; 1024バイト
SECTOR_BYTES      equ 1024
DA_UA             equ 0x91       ; 1MBインターフェース・ユニット1 (FDD2/B:)
READ_AH           equ 0x66       ; MT0/MF1/f0/SEEK1/コマンド6(読み出し)

start:
        ; ---- PSPのコマンドラインからLBAを10進で取り出す ----
        xor     bx, bx                  ; bx = 累積LBA(初期値0)
        mov     si, 0x0081              ; PSPコマンドライン先頭
.skip_sp:
        mov     al, [si]
        cmp     al, ' '
        jne     .skip_sp_done
        inc     si
        jmp     .skip_sp
.skip_sp_done:
        cmp     al, 0x0D
        je      .parse_done
.parse_loop:
        mov     al, [si]
        cmp     al, 0x0D
        je      .parse_done
        cmp     al, ' '
        je      .parse_done
        cmp     al, '0'
        jb      .parse_done
        cmp     al, '9'
        ja      .parse_done
        sub     al, '0'
        xor     ah, ah
        push    ax
        ; bx = bx*10
        mov     ax, bx
        mov     cx, 10
        mul     cx                      ; dx:ax = bx*10 (LBAは小さい値なので上位は捨てる)
        mov     bx, ax
        pop     ax
        add     bx, ax
        inc     si
        jmp     .parse_loop
.parse_done:
        mov     [lba], bx

        ; ---- LBA -> CHS 変換 ----
        ; cyl = lba / 16, head = (lba/8) mod 2, sec = (lba mod 8) + 1
        mov     ax, bx
        xor     dx, dx
        mov     cx, SECTORS_PER_TRACK * HEADS   ; 16
        div     cx                      ; ax=cyl, dx=lba mod 16
        mov     [cyl], al
        mov     bx, dx                  ; bx = lba mod 16
        mov     ax, bx
        xor     dx, dx
        mov     cx, SECTORS_PER_TRACK   ; 8
        div     cx                      ; ax=head(0/1), dx=lba mod 8
        mov     [head], al
        mov     al, dl
        inc     al
        mov     [sec], al

        ; ---- INT 1Bh でバッファへ1セクタ読み出す ----
        mov     ax, cs
        mov     es, ax
        mov     bp, buffer

        mov     ah, READ_AH
        mov     al, DA_UA
        mov     bx, SECTOR_BYTES
        mov     ch, SECTOR_LEN_N
        mov     cl, [cyl]
        mov     dh, [head]
        mov     dl, [sec]
        int     0x1B
        jc      .err
        mov     byte [status_ah], ah

        ; ---- [ok] 出力 ----
        mov     dx, msg_ok
        mov     ah, 0x09
        int     0x21

        mov     bx, [lba]
        call    print_dec16
        mov     dx, msg_cyl
        mov     ah, 0x09
        int     0x21
        mov     al, [cyl]
        call    print_hex8
        mov     dx, msg_head
        mov     ah, 0x09
        int     0x21
        mov     al, [head]
        call    print_hex8
        mov     dx, msg_sec
        mov     ah, 0x09
        int     0x21
        mov     al, [sec]
        call    print_hex8

        mov     dx, msg_crlf
        mov     ah, 0x09
        int     0x21

        call    dump_head32
        jmp     finish

.err:
        mov     byte [status_ah], ah
        mov     dx, msg_err
        mov     ah, 0x09
        int     0x21
        mov     al, [status_ah]
        call    print_hex8
        mov     dx, msg_crlf
        mov     ah, 0x09
        int     0x21

finish:
        mov     ax, 0x4C00
        int     0x21

;--------------------------------------------------------------------
; サブルーチン
;--------------------------------------------------------------------

; BX = 16bit符号なし整数を10進でAH=02出力(先頭ゼロ抑制なし・シンプルに5桁固定でも良いが
; 見やすさのため先頭ゼロを抑制する)
print_dec16:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        mov     si, dec_buf + 5
        mov     byte [si], 0
        mov     ax, bx
        mov     cx, 10
        or      ax, ax
        jnz     .conv
        dec     si
        mov     byte [si], '0'
        jmp     .print
.conv:
.cloop:
        or      ax, ax
        jz      .cdone
        xor     dx, dx
        div     cx
        add     dl, '0'
        dec     si
        mov     [si], dl
        jmp     .cloop
.cdone:
.print:
        mov     dx, si
        push    cs
        pop     ds
        mov     ah, 0x09
        ; dec_bufは'$'終端していないので個別に出す
        call    print_asciiz_ds_dx
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; DS:DX = 0終端(NUL終端)文字列を1文字ずつAH=02で出す
print_asciiz_ds_dx:
        push    ax
        push    si
        mov     si, dx
.loop:
        mov     al, [si]
        or      al, al
        jz      .done
        push    dx
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        pop     dx
        inc     si
        jmp     .loop
.done:
        pop     si
        pop     ax
        ret

; AL = 8bit値を16進2桁でAH=02出力
print_hex8:
        push    ax
        push    cx
        mov     ah, al
        mov     cl, 4
        shr     al, cl
        call    .nib
        mov     al, ah
        and     al, 0x0F
        call    .nib
        pop     cx
        pop     ax
        ret
.nib:
        and     al, 0x0F
        cmp     al, 10
        jb      .digit
        add     al, 'A' - 10 - '0'
.digit:
        add     al, '0'
        push    dx
        mov     dl, al
        push    ax
        mov     ah, 0x02
        int     0x21
        pop     ax
        pop     dx
        ret

; buffer先頭32バイトを「16進2桁 空白区切り」で出力する
dump_head32:
        push    ax
        push    bx
        push    cx
        mov     bx, buffer
        mov     cx, 32
.dloop:
        mov     al, [bx]
        call    print_hex8
        push    dx
        mov     dl, ' '
        mov     ah, 0x02
        int     0x21
        pop     dx
        inc     bx
        loop    .dloop
        mov     dx, msg_crlf
        mov     ah, 0x09
        int     0x21
        pop     cx
        pop     bx
        pop     ax
        ret

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------
lba:            dw      0
cyl:            db      0
head:           db      0
sec:            db      0
status_ah:      db      0
dec_buf:        times 6 db 0

msg_ok:         db      '[ok] lba=$'
msg_cyl:        db      ' cyl=$'
msg_head:       db      ' head=$'
msg_sec:        db      ' sec=$'
msg_err:        db      '[err] AH=$'
msg_crlf:       db      0x0D, 0x0A, '$'

buffer:         times SECTOR_BYTES db 0
