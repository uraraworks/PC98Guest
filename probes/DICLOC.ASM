; DICLOC.COM - FEP.DIC がディスクのどこにあるか(開始LBA・セクタ数)を解決して表示する
;
; 目的:
;   辞書をディスクから直接読む方式(常駐後にセクタ単位で読む)への第一歩。
;   常駐はせず、「起動時にDOSを使って辞書の場所を突き止める」処理だけを
;   単体で作り、正しさを確かめる。FATを辿る処理は間違えやすいので、まず
;   ここで単体テストする。
;
; 対象ディスク (build-disk.mjs が作る out/esctest.xdf):
;   1024バイト/セクタ、8セクタ/トラック、2ヘッド、77シリンダ(計1232セクタ)
;   予約1セクタ、FAT×2(各2セクタ)、ルートエントリ192、FAT12。
;   ただし値はハードコードせず、必ずBPBから読む。
;
; 手順:
;   1) AH=19h でカレントドライブ番号を得る(0=A:)
;   2) INT 25h(絶対ディスク読み込み)でブートセクタ(論理セクタ0)を読む。
;      INT 25h は戻り際にスタックへフラグを1つ残す仕様なので、直後に
;      popf すること(RAWRD18.ASM系と違いこちらはDOSファンクションではなく
;      BIOS/DOS低レベルの絶対読み込みそのものであることに注意)。
;   3) BPBから bps/spc/rsvd/nfat/rootent/totsec/spf/spt/heads を読む。
;   4) fat_start・root_start・root_sects・data_start を計算する。
;   5) FAT全体・ルートディレクトリ全体をINT 25hで読み、
;      "FEP     DIC"(8.3の生の形、スペース埋め)を探す。
;   6) 見つかった開始クラスタからFAT12チェーンを辿り、クラスタ数と
;      「連続しているか(次クラスタ=現クラスタ+1か)」を求める。
;   7) 開始LBA = data_start + (開始クラスタ-2) * spc を求めて表示する。
;
; 出力(数値はすべて10進。tools/expect_dicloc.py が同じ書式で期待値を出す):
;   [bpb] bps=NNNN spc=N rsvd=N nfat=N rootent=NNN spf=N spt=N heads=N
;   [calc] fat=N root=N rootsec=N data=N
;   [file] clus=N size=NNNNNNN
;   [chain] count=N contiguous=yes|no
;   [loc] lba=N sectors=N
;
; ビルド: nasm -f bin DICLOC.ASM -o DICLOC.COM

        cpu     8086
        org     0x100

FAT_BUF_SIZE    equ     4096    ; FATがこのバイト数に収まらなければエラー終了
ROOT_BUF_SIZE   equ     8192    ; ルートディレクトリがこのバイト数に収まらなければエラー終了
CHAIN_SAFETY    equ     8192    ; FATチェーンの暴走(循環等)検出用の上限反復回数

start:
        ; ---- 1) カレントドライブ番号 ----
        mov     ah, 0x19
        int     0x21
        mov     [drive], al

        ; ---- 2) ブートセクタ(論理セクタ0)をINT 25hで読む ----
        mov     cx, 1
        mov     dx, 0
        mov     bx, bootsec_buf
        mov     al, [drive]
        xor     ah, ah                  ; AHはINT 25hの入力仕様に無いが、直前の
                                         ; INT 21h呼び出しの残骸(AH=19h等)が
                                         ; 混入しないよう明示的にクリアする
        int     0x25
        popf
        jc      err_boot_read

        ; ---- 3) BPB解析 ----
        mov     ax, [bootsec_buf + 0x0B]
        mov     [bps], ax
        mov     al, [bootsec_buf + 0x0D]
        mov     [spc], al
        mov     ax, [bootsec_buf + 0x0E]
        mov     [rsvd], ax
        mov     al, [bootsec_buf + 0x10]
        mov     [nfat], al
        mov     ax, [bootsec_buf + 0x11]
        mov     [rootent], ax
        mov     ax, [bootsec_buf + 0x13]
        mov     [totsec], ax
        mov     ax, [bootsec_buf + 0x16]
        mov     [spf], ax
        mov     ax, [bootsec_buf + 0x18]
        mov     [spt], ax
        mov     ax, [bootsec_buf + 0x1A]
        mov     [heads], ax

        call    print_bpb_line

        ; ---- 4) 位置計算 ----
        ; fat_start = rsvd
        mov     ax, [rsvd]
        mov     [fat_start], ax

        ; root_start = fat_start + nfat * spf
        xor     ah, ah
        mov     al, [nfat]
        mov     bx, ax                  ; bx = nfat
        mov     ax, [spf]
        mul     bx                      ; dx:ax = spf * nfat
        cmp     dx, 0
        jne     err_overflow
        mov     bx, ax                  ; bx = nfat*spf(セクタ数)
        mov     ax, [fat_start]
        add     ax, bx
        jc      err_overflow
        mov     [root_start], ax

        ; root_sects = ceil(rootent * 32 / bps)
        mov     ax, [rootent]
        mov     cx, 32
        mul     cx                      ; dx:ax = rootent*32
        add     ax, [bps]
        adc     dx, 0
        sub     ax, 1
        sbb     dx, 0                   ; dx:ax = rootent*32 + bps - 1
        mov     cx, [bps]
        cmp     cx, 0
        je      err_overflow
        div     cx                      ; ax = root_sects (端数は捨てる)
        mov     [root_sects], ax

        ; data_start = root_start + root_sects
        mov     ax, [root_start]
        add     ax, [root_sects]
        jc      err_overflow
        mov     [data_start], ax

        call    print_calc_line

        ; ---- 5a) FAT全体を読む ----
        mov     ax, [spf]
        mov     cx, [bps]
        mul     cx                      ; dx:ax = spf*bps (FATの総バイト数)
        cmp     dx, 0
        jne     err_fat_too_big
        cmp     ax, FAT_BUF_SIZE
        ja      err_fat_too_big

        mov     cx, [spf]
        mov     dx, [fat_start]
        mov     bx, fat_buf
        mov     al, [drive]
        xor     ah, ah                  ; 直前のAH=09h(DOS文字列出力)の残骸を
                                         ; 混入させない(FAT読み込み失敗の原因)
        int     0x25
        popf
        jc      err_fat_read

        ; ---- 5b) ルートディレクトリ全体を読む ----
        mov     ax, [root_sects]
        mov     cx, [bps]
        mul     cx                      ; dx:ax = root_sects*bps
        cmp     dx, 0
        jne     err_root_too_big
        cmp     ax, ROOT_BUF_SIZE
        ja      err_root_too_big

        mov     cx, [root_sects]
        mov     dx, [root_start]
        mov     bx, root_buf
        mov     al, [drive]
        xor     ah, ah                  ; 同上(ルートディレクトリ読み込みも
                                         ; 同じ原因で失敗しうる)
        int     0x25
        popf
        jc      err_root_read

        ; ---- 5c) "FEP     DIC" を探す ----
        mov     ax, [rootent]
        mov     [entries_left], ax
        mov     si, root_buf
        mov     byte [file_found], 0

.search_loop:
        mov     ax, [entries_left]
        cmp     ax, 0
        je      .search_done
        mov     al, [si]
        cmp     al, 0
        je      .search_done            ; 0x00: これ以降エントリなし
        cmp     al, 0xE5
        je      .next_entry             ; 0xE5: 削除済み

        push    si
        mov     di, target_name
        mov     cx, 11
        repe    cmpsb
        pop     si
        je      .found
        jmp     .next_entry

.found:
        mov     ax, [si + 0x1A]
        mov     [file_clus], ax
        mov     ax, [si + 0x1C]
        mov     [file_size], ax
        mov     ax, [si + 0x1E]
        mov     [file_size + 2], ax
        mov     byte [file_found], 1
        jmp     .search_done

.next_entry:
        add     si, 32
        mov     ax, [entries_left]
        dec     ax
        mov     [entries_left], ax
        jmp     .search_loop

.search_done:
        cmp     byte [file_found], 0
        je      err_not_found

        call    print_file_line

        ; ---- 6) FAT12チェーンを辿る ----
        mov     ax, [file_clus]
        mov     [cur_clus], ax
        mov     word [chain_count], 1
        mov     byte [chain_contig], 1
        mov     word [safety], CHAIN_SAFETY

.chain_loop:
        mov     ax, [cur_clus]
        call    get_fat12_entry         ; -> ax = FATエントリ値(12bit)
        mov     [next_clus], ax
        cmp     ax, 0x0FF8
        jae     .chain_done             ; 終端

        mov     ax, [cur_clus]
        inc     ax
        cmp     ax, [next_clus]
        je      .contig_ok
        mov     byte [chain_contig], 0
.contig_ok:
        mov     ax, [chain_count]
        inc     ax
        mov     [chain_count], ax

        mov     ax, [next_clus]
        mov     [cur_clus], ax

        mov     ax, [safety]
        dec     ax
        mov     [safety], ax
        cmp     ax, 0
        je      err_chain_runaway
        jmp     .chain_loop

.chain_done:
        call    print_chain_line

        ; ---- 7) 開始LBA・セクタ数を計算 ----
        mov     ax, [file_clus]
        sub     ax, 2
        mov     bl, [spc]
        xor     bh, bh
        mul     bx                      ; dx:ax = (file_clus-2)*spc
        cmp     dx, 0
        jne     err_overflow
        add     ax, [data_start]
        jc      err_overflow
        mov     [result_lba], ax

        mov     ax, [chain_count]
        mov     bl, [spc]
        xor     bh, bh
        mul     bx                      ; dx:ax = chain_count*spc
        cmp     dx, 0
        jne     err_overflow
        mov     [result_sectors], ax

        call    print_loc_line

        mov     ax, 0x4C00
        int     0x21

;--------------------------------------------------------------------
; エラー出口
;--------------------------------------------------------------------
err_boot_read:
        mov     dx, msg_err_boot_read
        jmp     fail_print_ax

err_fat_too_big:
        mov     dx, msg_err_fat_too_big
        jmp     fail_print

err_fat_read:
        mov     dx, msg_err_fat_read
        jmp     fail_print_ax

err_root_too_big:
        mov     dx, msg_err_root_too_big
        jmp     fail_print

err_root_read:
        mov     dx, msg_err_root_read
        jmp     fail_print_ax

err_not_found:
        mov     dx, msg_err_not_found
        jmp     fail_print

err_chain_runaway:
        mov     dx, msg_err_chain_runaway
        jmp     fail_print

err_overflow:
        mov     dx, msg_err_overflow
        jmp     fail_print

; DS:DX = エラーメッセージ('$'終端)を表示して終了(AL/AHの値は使わない版)
fail_print:
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

; DS:DX = エラーメッセージを表示し、続けてAH(INT 25hのステータス)を16進で
; 表示してから終了する版
fail_print_ax:
        push    ax
        mov     ah, 0x09
        int     0x21
        pop     ax                      ; AL = INT 25h/26hのエラーコード(CF=1時の
                                         ; 仕様どおり)。AHは追加情報でALではない
                                         ; ので、ALをそのまま表示する(以前はAHを
                                         ; 上書きして表示しており誤った値だった)
        call    print_hex8
        mov     dx, msg_close_crlf
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

;--------------------------------------------------------------------
; FAT12エントリ取得
;   in:  ax = クラスタ番号
;   out: ax = 12bit FATエントリ値
;--------------------------------------------------------------------
get_fat12_entry:
        push    cx
        push    dx
        push    bx
        mov     cx, 3
        mul     cx                      ; dx:ax = cluster*3
        mov     cx, 2
        div     cx                      ; ax = offset, dx = cluster mod 2(偶数=0/奇数=1)
        mov     bx, ax
        mov     ax, [fat_buf + bx]
        cmp     dx, 0
        je      .even
        mov     cx, 4
        shr     ax, cl
        jmp     .done
.even:
        and     ax, 0x0FFF
.done:
        pop     bx
        pop     dx
        pop     cx
        ret

;--------------------------------------------------------------------
; 32bit除算: dx:ax(被除数) / cx(除数) -> dx:ax = 商, bx = 余り
;--------------------------------------------------------------------
divide_dword32:
        mov     [dd_hi], dx
        mov     [dd_lo], ax
        mov     ax, [dd_hi]
        xor     dx, dx
        div     cx                      ; ax = 商の上位16bit、dx = 次段への繰越
        mov     [dd_qhi], ax
        mov     ax, [dd_lo]
        div     cx                      ; ax = 商の下位16bit、dx = 最終余り
        mov     bx, dx
        mov     dx, [dd_qhi]
        ret

;--------------------------------------------------------------------
; 表示ルーチン
;--------------------------------------------------------------------

; DS:DX = '$'終端文字列を表示
print_str:
        mov     ah, 0x09
        int     0x21
        ret

; dx:ax(32bit符号なし) を10進でAH=02出力(先頭ゼロなし)
print_dec32:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        mov     si, dec32_buf + 10
        mov     byte [si], '$'          ; AH=09h は'$'終端が必須(0では止まらず
                                         ; 後続の定数を巻き込んで暴走する)
        mov     cx, ax
        or      cx, dx
        jnz     .conv
        dec     si
        mov     byte [si], '0'
        jmp     .print
.conv:
.cloop:
        mov     cx, ax
        or      cx, dx
        jz      .print
        mov     cx, 10
        call    divide_dword32          ; dx:ax=商, bx=余り(0-9)
        add     bl, '0'
        dec     si
        mov     [si], bl
        jmp     .cloop
.print:
        mov     dx, si
        call    print_str
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; AL(8bit) を16進2桁でAH=02出力
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

; word値(ax)を0拡張してprint_dec32へ渡す
print_dec_word:
        push    dx
        mov     dx, 0
        call    print_dec32
        pop     dx
        ret

print_bpb_line:
        push    ax
        push    dx
        mov     dx, msg_bpb_bps
        call    print_str
        mov     ax, [bps]
        call    print_dec_word
        mov     dx, msg_bpb_spc
        call    print_str
        xor     ah, ah
        mov     al, [spc]
        call    print_dec_word
        mov     dx, msg_bpb_rsvd
        call    print_str
        mov     ax, [rsvd]
        call    print_dec_word
        mov     dx, msg_bpb_nfat
        call    print_str
        xor     ah, ah
        mov     al, [nfat]
        call    print_dec_word
        mov     dx, msg_bpb_rootent
        call    print_str
        mov     ax, [rootent]
        call    print_dec_word
        mov     dx, msg_bpb_spf
        call    print_str
        mov     ax, [spf]
        call    print_dec_word
        mov     dx, msg_bpb_spt
        call    print_str
        mov     ax, [spt]
        call    print_dec_word
        mov     dx, msg_bpb_heads
        call    print_str
        mov     ax, [heads]
        call    print_dec_word
        mov     dx, msg_crlf
        call    print_str
        pop     dx
        pop     ax
        ret

print_calc_line:
        push    ax
        push    dx
        mov     dx, msg_calc_fat
        call    print_str
        mov     ax, [fat_start]
        call    print_dec_word
        mov     dx, msg_calc_root
        call    print_str
        mov     ax, [root_start]
        call    print_dec_word
        mov     dx, msg_calc_rootsec
        call    print_str
        mov     ax, [root_sects]
        call    print_dec_word
        mov     dx, msg_calc_data
        call    print_str
        mov     ax, [data_start]
        call    print_dec_word
        mov     dx, msg_crlf
        call    print_str
        pop     dx
        pop     ax
        ret

print_file_line:
        push    ax
        push    dx
        mov     dx, msg_file_clus
        call    print_str
        mov     ax, [file_clus]
        call    print_dec_word
        mov     dx, msg_file_size
        call    print_str
        mov     ax, [file_size]
        mov     dx, [file_size + 2]
        call    print_dec32
        mov     dx, msg_crlf
        call    print_str
        pop     dx
        pop     ax
        ret

print_chain_line:
        push    ax
        push    dx
        mov     dx, msg_chain_count
        call    print_str
        mov     ax, [chain_count]
        call    print_dec_word
        mov     dx, msg_chain_contig
        call    print_str
        cmp     byte [chain_contig], 0
        jne     .yes
        mov     dx, msg_no
        jmp     .print_yn
.yes:
        mov     dx, msg_yes
.print_yn:
        call    print_str
        mov     dx, msg_crlf
        call    print_str
        pop     dx
        pop     ax
        ret

print_loc_line:
        push    ax
        push    dx
        mov     dx, msg_loc_lba
        call    print_str
        mov     ax, [result_lba]
        call    print_dec_word
        mov     dx, msg_loc_sectors
        call    print_str
        mov     ax, [result_sectors]
        call    print_dec_word
        mov     dx, msg_crlf
        call    print_str
        pop     dx
        pop     ax
        ret

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------
drive:          db      0

bps:            dw      0
spc:            db      0
rsvd:           dw      0
nfat:           db      0
rootent:        dw      0
totsec:         dw      0
spf:            dw      0
spt:            dw      0
heads:          dw      0

fat_start:      dw      0
root_start:     dw      0
root_sects:     dw      0
data_start:     dw      0

entries_left:   dw      0
file_found:     db      0
file_clus:      dw      0
file_size:      dd      0

cur_clus:       dw      0
next_clus:      dw      0
chain_count:    dw      0
chain_contig:   db      0
safety:         dw      0

result_lba:     dw      0
result_sectors: dw      0

dd_hi:          dw      0
dd_lo:          dw      0
dd_qhi:         dw      0

dec32_buf:      times 11 db 0

target_name:    db      'FEP     DIC'   ; 8.3の生の形(11バイト、スペース埋め)

msg_bpb_bps:            db      '[bpb] bps=$'
msg_bpb_spc:            db      ' spc=$'
msg_bpb_rsvd:           db      ' rsvd=$'
msg_bpb_nfat:           db      ' nfat=$'
msg_bpb_rootent:        db      ' rootent=$'
msg_bpb_spf:            db      ' spf=$'
msg_bpb_spt:            db      ' spt=$'
msg_bpb_heads:          db      ' heads=$'

msg_calc_fat:           db      '[calc] fat=$'
msg_calc_root:          db      ' root=$'
msg_calc_rootsec:       db      ' rootsec=$'
msg_calc_data:          db      ' data=$'

msg_file_clus:          db      '[file] clus=$'
msg_file_size:          db      ' size=$'

msg_chain_count:        db      '[chain] count=$'
msg_chain_contig:       db      ' contiguous=$'
msg_yes:                db      'yes$'
msg_no:                 db      'no$'

msg_loc_lba:            db      '[loc] lba=$'
msg_loc_sectors:        db      ' sectors=$'

msg_crlf:               db      0x0D, 0x0A, '$'
msg_close_crlf:         db      ')', 0x0D, 0x0A, '$'

msg_err_boot_read:      db      'error: boot sector read failed (int25h status=$'
msg_err_fat_too_big:    db      'error: FAT does not fit in buffer', 0x0D, 0x0A, '$'
msg_err_fat_read:       db      'error: FAT read failed (int25h status=$'
msg_err_root_too_big:   db      'error: root directory does not fit in buffer', 0x0D, 0x0A, '$'
msg_err_root_read:      db      'error: root directory read failed (int25h status=$'
msg_err_not_found:      db      'error: FEP.DIC not found in root directory', 0x0D, 0x0A, '$'
msg_err_chain_runaway:  db      'error: FAT chain too long (possible loop)', 0x0D, 0x0A, '$'
msg_err_overflow:       db      'error: value overflow in calculation', 0x0D, 0x0A, '$'

;--------------------------------------------------------------------
; バッファ
;--------------------------------------------------------------------
bootsec_buf:    times 2048 db 0
fat_buf:        times FAT_BUF_SIZE db 0
root_buf:       times ROOT_BUF_SIZE db 0
