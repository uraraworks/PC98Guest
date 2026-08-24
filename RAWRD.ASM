; RAWRD.COM - PC-98 INT 1Bh (フロッピーディスクBIOS) 生セクタ読み出し測定プログラム
;
; 目的:
;   辞書をメモリに全部載せる方式をやめ、当時のFEPのように「ディスクに置いて
;   必要な所だけ読む」方式にできるかを確かめる土台。まずINT 1Bhで生セクタが
;   読めるかどうかを単体で測る(常駐しない)。
;
;   RAWRD 0 実行時、CF=0(成功)なのにバッファが全部ゼロという食い違いが
;   実測された。原因をパラメータ総当たりで特定するため、AH/DA-UA/Nを
;   コマンドラインから振れるようにしてある。
;
; 使い方: RAWRD <LBA10進> [AH16進2桁] [DA/UA16進2桁] [N10進1桁]
;   省略時: AH=66  DA/UA=91  N=3(1024バイト)
;   引数が解析できなかった場合はエラーにせず既定値を使う。
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
; バッファは静的に 0xCC で初期化してある(実行時にmemsetしない)。
; 「BIOSが1バイトも書かなかった」のか「ゼロを書いた」のかを区別するため。
;
; ビルド: nasm -f bin RAWRD.ASM -o RAWRD.COM

        cpu     8086
        org     0x100

SECTORS_PER_TRACK equ 8
HEADS             equ 2
DEFAULT_N         equ 3          ; 1024バイト
DEFAULT_DA_UA     equ 0x91       ; 1MBインターフェース・ユニット1 (FDD2/B:)
DEFAULT_AH        equ 0x66       ; MT0/MF1/f0/SEEK1/コマンド6(読み出し)

start:
        ; ---- PSPのコマンドラインから引数を取り出す ----
        mov     si, 0x0081              ; PSPコマンドライン先頭

        ; 既定値をセット(引数が無い/解析失敗ならこのまま使う)
        mov     word [lba], 0
        mov     byte [ah_val], DEFAULT_AH
        mov     byte [ua_val], DEFAULT_DA_UA
        mov     byte [n_val], DEFAULT_N

        ; --- 第1引数: LBA(10進) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      .args_done
        call    parse_dec16             ; -> bx=値, si更新。数字が1つも無ければ変化なし
        mov     [lba], bx

        ; --- 第2引数: AH(16進2桁) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      .args_done
        call    parse_hex2              ; -> cf=0ならal=値、si更新(成功時のみ)
        jc      .skip_ah_bad
        mov     [ah_val], al
        jmp     .after_ah
.skip_ah_bad:
        call    skip_token              ; 解析失敗時はこのトークンを読み飛ばす
.after_ah:

        ; --- 第3引数: DA/UA(16進2桁) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      .args_done
        call    parse_hex2
        jc      .skip_ua_bad
        mov     [ua_val], al
        jmp     .after_ua
.skip_ua_bad:
        call    skip_token
.after_ua:

        ; --- 第4引数: N(10進1桁) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      .args_done
        mov     al, [si]
        cmp     al, '0'
        jb      .args_done
        cmp     al, '9'
        ja      .args_done
        sub     al, '0'
        mov     [n_val], al

.args_done:

        ; ---- LBA -> CHS 変換 ----
        ; cyl = lba / 16, head = (lba/8) mod 2, sec = (lba mod 8) + 1
        mov     ax, [lba]
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

        ; ---- 転送バイト数 = 128 << N ----
        mov     ax, 128
        mov     cl, [n_val]
        shl     ax, cl
        mov     [size], ax

        ; ---- INT 1Bh でバッファへ1セクタ読み出す ----
        mov     ax, cs
        mov     es, ax
        mov     bp, buffer
        mov     [es_val], es
        mov     [bp_val], bp

        mov     ah, [ah_val]
        mov     al, [ua_val]
        mov     bx, [size]
        mov     ch, [n_val]
        mov     cl, [cyl]
        mov     dh, [head]
        mov     dl, [sec]
        int     0x1B
        mov     byte [status_ah], ah
        pushf
        pop     ax
        and     ax, 1
        mov     [cf_val], al

        ; ---- 出力: [ah=XX ua=XX n=X] lba=NNNN cyl=CC head=HH sec=SS ----
        mov     dx, msg_ahua
        mov     ah, 0x09
        int     0x21
        mov     al, [ah_val]
        call    print_hex8
        mov     dx, msg_ua
        mov     ah, 0x09
        int     0x21
        mov     al, [ua_val]
        call    print_hex8
        mov     dx, msg_n
        mov     ah, 0x09
        int     0x21
        mov     al, [n_val]
        add     al, '0'
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        mov     dx, msg_lba
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

        mov     dx, msg_cf
        mov     ah, 0x09
        int     0x21
        mov     al, [cf_val]
        add     al, '0'
        mov     dl, al
        mov     ah, 0x02
        int     0x21

        mov     dx, msg_status
        mov     ah, 0x09
        int     0x21
        mov     al, [status_ah]
        call    print_hex8

        mov     dx, msg_esbp
        mov     ah, 0x09
        int     0x21
        mov     ax, [es_val]
        call    print_hex16
        mov     dx, msg_bp
        mov     ah, 0x09
        int     0x21
        mov     ax, [bp_val]
        call    print_hex16

        mov     dx, msg_crlf
        mov     ah, 0x09
        int     0x21

        call    dump_head32

        mov     ax, 0x4C00
        int     0x21

;--------------------------------------------------------------------
; サブルーチン
;--------------------------------------------------------------------

; DS:SI をスペース(0x20)である限り読み飛ばす
skip_spaces:
        push    ax
.loop:
        mov     al, [si]
        cmp     al, ' '
        jne     .done
        inc     si
        jmp     .loop
.done:
        pop     ax
        ret

; DS:SI を空白(0x20)またはCR(0x0D)に当たるまで読み飛ばす(解析失敗トークンの破棄用)
skip_token:
        push    ax
.loop:
        mov     al, [si]
        cmp     al, 0x0D
        je      .done
        cmp     al, ' '
        je      .done
        inc     si
        jmp     .loop
.done:
        pop     ax
        ret

; DS:SI から10進数字列を読み、BX=値。SIは末尾(非数字)まで進む。
; 数字が1つも無ければBXは変化させない(呼び出し側が既定値を保持する)。
parse_dec16:
        push    ax
        push    cx
        push    dx
        xor     cx, cx                  ; cx = 読んだ桁数
        xor     bx, bx
.loop:
        mov     al, [si]
        cmp     al, '0'
        jb      .done
        cmp     al, '9'
        ja      .done
        sub     al, '0'
        xor     ah, ah
        push    ax
        mov     ax, bx
        mov     dx, 10
        mul     dx                      ; ax = bx*10 (LBAは小さいので上位dxは捨てる)
        mov     bx, ax
        pop     ax
        add     bx, ax
        inc     cx
        inc     si
        jmp     .loop
.done:
        or      cx, cx
        jnz     .have_digits
        ; 数字が無かった: 呼び出し側のBXを壊さない -> このルーチンでは何もしない
        pop     dx
        pop     cx
        pop     ax
        ret
.have_digits:
        pop     dx
        pop     cx
        pop     ax
        ret

; DS:SI から16進2桁を読み、成功ならCF=0/AL=値/SIを2進める。
; 2桁とも0-9A-Fa-fでなければCF=1(SIは動かさない)。
parse_hex2:
        push    bx
        push    cx
        mov     bx, si
        mov     al, [bx]
        call    .hexval
        jc      .bad
        mov     ah, al
        mov     al, [bx+1]
        call    .hexval
        jc      .bad
        mov     cl, 4
        shl     ah, cl
        or      al, ah
        add     si, 2
        clc
        pop     cx
        pop     bx
        ret
.bad:
        stc
        pop     cx
        pop     bx
        ret
; AL(1文字) -> AL(0-15)、非16進ならCF=1
.hexval:
        cmp     al, '0'
        jb      .hv_bad
        cmp     al, '9'
        jbe     .hv_digit
        cmp     al, 'A'
        jb      .hv_bad
        cmp     al, 'F'
        jbe     .hv_upper
        cmp     al, 'a'
        jb      .hv_bad
        cmp     al, 'f'
        ja      .hv_bad
        sub     al, 'a' - 10
        clc
        ret
.hv_upper:
        sub     al, 'A' - 10
        clc
        ret
.hv_digit:
        sub     al, '0'
        clc
        ret
.hv_bad:
        stc
        ret

; BX = 16bit符号なし整数を10進でAH=02出力(先頭ゼロを抑制する)
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

; AX = 16bit値を16進4桁でAH=02出力(上位バイトから)
print_hex16:
        push    ax
        mov     al, ah
        call    print_hex8
        pop     ax
        call    print_hex8
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
ah_val:         db      0
ua_val:         db      0
n_val:          db      0
size:           dw      0
cyl:            db      0
head:           db      0
sec:            db      0
status_ah:      db      0
cf_val:         db      0
es_val:         dw      0
bp_val:         dw      0
dec_buf:        times 6 db 0

msg_ahua:       db      '[ah=$'
msg_ua:         db      ' ua=$'
msg_n:          db      ' n=$'
msg_lba:        db      '] lba=$'
msg_cyl:        db      ' cyl=$'
msg_head:       db      ' head=$'
msg_sec:        db      ' sec=$'
msg_cf:         db      '  cf=$'
msg_status:     db      ' status=$'
msg_esbp:       db      ' es=$'
msg_bp:         db      ' bp=$'
msg_crlf:       db      0x0D, 0x0A, '$'

buffer:         times 1024 db 0xCC
