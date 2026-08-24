; HDDRD.COM - PC-98 INT 1Bh (ハードディスクBIOS) 生セクタ読み出し測定プログラム
;
; 目的:
;   FDでは INT 1Bh の生セクタ読み出しで辞書を引く方式が実測済み(RAWRD.COM)。
;   HDDでも同じことをするための土台。HDD BIOSの呼び出し形式はFDと違う
;   (MT/MF/SEEKビットが無く、相対/絶対の2種類のアドレス指定がある)ので、
;   まず単体で読めるかどうかを実測する(常駐しない)。
;
; 使い方:
;   相対アドレス指定(既定):
;     HDDRD <セクタ番号10進> [AH16進2桁] [DA/UA16進2桁] [r]
;       省略時 AH=06 UA=00
;       <セクタ番号> は線形セクタ番号(相対アドレス)。DL(最上位)=0固定、
;       CH:CL に セクタ番号の上位:下位バイトを入れる(値は65535までなので
;       24bitの最上位バイトは常に0)。
;
;   絶対アドレス指定:
;     HDDRD a [AH16進2桁] [DA/UA16進2桁] [cyl10進] [head10進] [sec10進]
;       第1引数が文字 'a'(大小どちらでも可)のときだけこの形式になる。
;       この場合セクタ番号引数は無く、代わりに cyl/head/sec を直接渡す。
;       CX=cyl(16bit) DH=head DL=sec としてINT 1Bhに渡す。
;
;   どちらの形式でも引数が解析できなかった場合はエラーにせず既定値を使う
;   (このトークンを読み飛ばして次の引数へ進む。RAWRD.COMと同じ流儀)。
;
; INT 1Bh 仕様 (『PC-9801プログラマーズBible』2-9-4。推測しないこと):
;   AH = コマンド識別コード。下位4bit=コマンド(06H=読み出し)。D5=r
;        (0:リトライあり/1:リトライなし)。FDと違いMT/MF/SEEKビットは無い。
;   AL = DA/UA (デバイスアドレス/ユニット番号)
;        00H-03H: SASI HDD(相対) / 80H-83H: SASI HDD(絶対)
;        20H-27H: SCSI HDD(相対) / A0H-A7H: SCSI HDD(絶対)
;   BX = 転送バイト数(256の倍数)
;   ES:BP = バッファ先頭
;   絶対アドレス指定: CX=シリンダ番号 / DH=ヘッド番号 / DL=セクタ番号
;   相対アドレス指定: DL,CH,CL の24bitに相対アドレス
;                     (DLが最上位バイト、CLが最下位バイト)
;                     相対アドレス = (ヘッド数×CX+DH)×セクタ/トラック+DL
;                     = ディスク先頭からの線形セクタ番号
; 出力: CF(0=正常/1=異常)、AH=ステータス
;
; 転送は1物理セクタ=256バイト固定(BX=256)。
; バッファは静的に0xCCで初期化してある(実行時にmemsetしない)。
; 「BIOSが1バイトも書かなかった」のか「ゼロを書いた」のかを区別するため
; (FDのRAWRD.COMでこの方法でSEEKビット漏れを見つけた実績がある)。
; 成功時も必ずstatus(AH)を表示する。
;
; ビルド: nasm -f bin HDDRD.ASM -o HDDRD.COM

        cpu     8086
        org     0x100

DEFAULT_AH      equ     0x06    ; コマンド6(読み出し)、r=0(リトライあり)
DEFAULT_UA      equ     0x00    ; SASI HDD ユニット0、相対アドレス指定
XFER_BYTES      equ     256     ; 1物理セクタ

start:
        mov     si, 0x0081              ; PSPコマンドライン先頭

        ; ---- 既定値 ----
        mov     byte [mode_val], 'r'
        mov     byte [ah_val], DEFAULT_AH
        mov     byte [ua_val], DEFAULT_UA
        mov     word [sector_val], 0
        mov     word [cyl_val], 0
        mov     byte [head_val], 0
        mov     byte [sec_val], 0

        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done

        ; ---- 第1引数が 'a'/'A' 単独トークンなら絶対アドレス指定モード ----
        mov     al, [si]
        cmp     al, 'a'
        je      .maybe_abs
        cmp     al, 'A'
        jne     .relative_parse
.maybe_abs:
        mov     bx, si
        inc     bx
        mov     al, [bx]
        cmp     al, ' '
        je      .abs_confirmed
        cmp     al, 0x0D
        je      .abs_confirmed
        jmp     .relative_parse
.abs_confirmed:
        mov     byte [mode_val], 'a'
        inc     si                      ; 'a' トークンを消費

        ; --- AH ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_hex2
        jc      .abs_ah_bad
        mov     [ah_val], al
        jmp     .abs_after_ah
.abs_ah_bad:
        call    skip_token
.abs_after_ah:

        ; --- DA/UA ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_hex2
        jc      .abs_ua_bad
        mov     [ua_val], al
        jmp     .abs_after_ua
.abs_ua_bad:
        call    skip_token
.abs_after_ua:

        ; --- cyl(10進) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_dec16
        mov     [cyl_val], bx

        ; --- head(10進) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_dec16
        mov     [head_val], bl

        ; --- sec(10進) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_dec16
        mov     [sec_val], bl

        jmp     args_done

.relative_parse:
        mov     byte [mode_val], 'r'

        ; --- セクタ番号(10進) ---
        call    parse_dec16
        mov     [sector_val], bx

        ; --- AH ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_hex2
        jc      .rel_ah_bad
        mov     [ah_val], al
        jmp     .rel_after_ah
.rel_ah_bad:
        call    skip_token
.rel_after_ah:

        ; --- DA/UA ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    parse_hex2
        jc      .rel_ua_bad
        mov     [ua_val], al
        jmp     .rel_after_ua
.rel_ua_bad:
        call    skip_token
.rel_after_ua:

        ; --- 第4引数(あれば 'r' 確認トークン。読み飛ばすだけ) ---
        call    skip_spaces
        cmp     byte [si], 0x0D
        je      args_done
        call    skip_token

args_done:
        ; ---- モードごとに CX/DH/DL を決める ----
        cmp     byte [mode_val], 'a'
        je      .build_abs

        ; 相対アドレス指定: CX = セクタ番号の(高:低)バイト = sector_val そのもの
        ;                   DH = 0(未使用) / DL = 0(24bitの最上位バイト)
        mov     ax, [sector_val]
        mov     [reg_cx], ax
        mov     byte [reg_dh], 0
        mov     byte [reg_dl], 0
        jmp     .regs_done
.build_abs:
        mov     ax, [cyl_val]
        mov     [reg_cx], ax
        mov     al, [head_val]
        mov     [reg_dh], al
        mov     al, [sec_val]
        mov     [reg_dl], al
.regs_done:

        ; ---- INT 1Bh でバッファへ1物理セクタ(256バイト)読み出す ----
        mov     ax, cs
        mov     es, ax
        mov     bp, buffer
        mov     [es_val], es
        mov     [bp_val], bp

        mov     ah, [ah_val]
        mov     al, [ua_val]
        mov     bx, XFER_BYTES
        mov     cx, [reg_cx]
        mov     dh, [reg_dh]
        mov     dl, [reg_dl]
        int     0x1B
        mov     byte [status_ah], ah
        pushf
        pop     ax
        and     ax, 1
        mov     [cf_val], al

        ; ---- 出力 ----
        ; [ah=XX ua=XX mode=X] sector=NNNNN            (相対モード)
        ; [ah=XX ua=XX mode=X] cyl=NNNNN head=XX sec=XX (絶対モード)
        ; (dl=XX ch=XX cl=XX)  cf=C status=XX es=XXXX bp=XXXX
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
        mov     dx, msg_mode
        mov     ah, 0x09
        int     0x21
        mov     dl, [mode_val]
        mov     ah, 0x02
        int     0x21
        mov     dl, ']'
        mov     ah, 0x02
        int     0x21

        cmp     byte [mode_val], 'a'
        je      .print_abs_fields

        mov     dx, msg_sector
        mov     ah, 0x09
        int     0x21
        mov     bx, [sector_val]
        call    print_dec16
        jmp     .after_pos_fields
.print_abs_fields:
        mov     dx, msg_cyl
        mov     ah, 0x09
        int     0x21
        mov     bx, [cyl_val]
        call    print_dec16
        mov     dx, msg_head
        mov     ah, 0x09
        int     0x21
        mov     al, [head_val]
        call    print_hex8
        mov     dx, msg_sec
        mov     ah, 0x09
        int     0x21
        mov     al, [sec_val]
        call    print_hex8
.after_pos_fields:

        mov     dx, msg_regs
        mov     ah, 0x09
        int     0x21
        mov     al, [reg_dl]
        call    print_hex8
        mov     dx, msg_ch
        mov     ah, 0x09
        int     0x21
        mov     ax, [reg_cx]
        mov     al, ah                  ; 上位バイト = CH
        call    print_hex8
        mov     dx, msg_cl
        mov     ah, 0x09
        int     0x21
        mov     ax, [reg_cx]
        call    print_hex8              ; 下位バイト = CL

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
; サブルーチン(RAWRD.COMと同じ実装。個別測定プログラムなので複製している)
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
        mul     dx                      ; ax = bx*10 (小さい値前提で上位dxは捨てる)
        mov     bx, ax
        pop     ax
        add     bx, ax
        inc     cx
        inc     si
        jmp     .loop
.done:
        or      cx, cx
        jnz     .have_digits
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
mode_val:       db      'r'
ah_val:         db      0
ua_val:         db      0
sector_val:     dw      0
cyl_val:        dw      0
head_val:       db      0
sec_val:        db      0
reg_cx:         dw      0
reg_dh:         db      0
reg_dl:         db      0
status_ah:      db      0
cf_val:         db      0
es_val:         dw      0
bp_val:         dw      0
dec_buf:        times 6 db 0

msg_ahua:       db      '[ah=$'
msg_ua:         db      ' ua=$'
msg_mode:       db      ' mode=$'
msg_sector:     db      ' sector=$'
msg_cyl:        db      ' cyl=$'
msg_head:       db      ' head=$'
msg_sec:        db      ' sec=$'
msg_regs:       db      '  (dl=$'
msg_ch:         db      ' ch=$'
msg_cl:         db      ' cl=$'
msg_cf:         db      ')  cf=$'
msg_status:     db      ' status=$'
msg_esbp:       db      ' es=$'
msg_bp:         db      ' bp=$'
msg_crlf:       db      0x0D, 0x0A, '$'

buffer:         times 256 db 0xCC
