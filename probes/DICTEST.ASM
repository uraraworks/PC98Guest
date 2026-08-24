; DICTEST.COM - TSUKUSHI.DIC 辞書ファイルの単体検索テスト (常駐しない普通の .COM)
;
; 使い方: DICTEST <ローマ字>
;   例)  DICTEST nihon
;
; 処理の流れ:
;   1. PSP 0x80(長さ)/0x81(本文開始)からコマンドライン引数を取り出す
;   2. ROMAJI.INC のテーブルでローマ字->ひらがな(Shift_JIS)に変換する
;      (変換アルゴリズムは tools/romaji_ref.py が正典。ここでは
;       FEP.ASM の resolve_romaji 一式の構造をそのまま流用している。
;       FEP.ASM 自体は変更しない)
;   3. 変換したよみを "[yomi] " に続けて表示する
;   4. カレントディレクトリの TSUKUSHI.DIC を開き、二分探索でよみを引く
;   5. 見つかれば "1:候補 2:候補" のように番号付きで表示、
;      見つからなければ "not found" と表示する
;
; ビルド: nasm -f bin DICTEST.ASM -o DICTEST.COM

        cpu     8086
        org     0x100

CR              equ     0x0D
LF              equ     0x0A

ARGBUFSIZE      equ     128
ROMABUFSIZE     equ     8
YOMIBUFSIZE     equ     16      ; 索引の「よみ」フィールドと同じ最大長
CANDTEXTBUFSIZE equ     80

;--------------------------------------------------------------------
start:
        ; ---- 1. コマンドライン引数の取り出し ----
        mov     cl, [0x80]
        xor     ch, ch
        mov     si, 0x81
.skip_sp:
        jcxz    .noarg
        cmp     byte [si], ' '
        jne     .args_start
        inc     si
        dec     cx
        jmp     .skip_sp
.noarg:
        mov     dx, msg_usage
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C01
        int     0x21

.args_start:
        mov     di, argbuf
        xor     bh, bh                  ; bh = 取り込んだバイト数
.copyloop:
        jcxz    .copydone
        mov     al, [si]
        cmp     al, CR
        je      .copydone
        mov     [di], al
        inc     si
        inc     di
        inc     bh
        dec     cx
        cmp     bh, ARGBUFSIZE - 1
        jae     .copydone
        jmp     .copyloop
.copydone:
        mov     [arglen], bh

        ; ---- 2. ローマ字->ひらがな変換 ----
        mov     byte [romalen], 0
        mov     byte [yomilen], 0
        mov     si, argbuf
        mov     cl, [arglen]
        xor     ch, ch
.convloop:
        jcxz    .convdone
        mov     al, [si]
        cmp     al, 'A'
        jb      .noupper
        cmp     al, 'Z'
        ja      .noupper
        add     al, 0x20
.noupper:
        push    cx
        push    si
        mov     bl, [romalen]
        xor     bh, bh
        mov     di, romabuf
        add     di, bx
        mov     [di], al
        inc     byte [romalen]
        call    resolve_romaji
        pop     si
        pop     cx
        inc     si
        loop    .convloop
.convdone:
        call    finalize_romabuf

        ; ---- 3. よみを表示 ----
        mov     si, msg_yomi_prefix
        mov     cl, msg_yomi_prefix_len
        xor     ch, ch
        call    print_bytes
        mov     si, yomibuf
        mov     cl, [yomilen]
        xor     ch, ch
        call    print_bytes
        call    print_crlf

        ; ---- 4. TSUKUSHI.DIC を開く ----
        mov     dx, dic_filename
        mov     ax, 0x3D00              ; open, read-only
        int     0x21
        jc      .open_err
        mov     [dic_handle], ax

        ; ヘッダ(16バイト)を読む
        mov     bx, [dic_handle]
        mov     cx, 16
        mov     dx, header_buf
        mov     ah, 0x3F
        int     0x21
        jc      .header_read_err
        cmp     ax, 16
        jne     .header_read_err

        ; マジック確認
        mov     si, header_buf
        mov     di, dic_magic
        mov     cx, 8
        repe    cmpsb
        jne     .magic_err

        mov     ax, [header_buf + 8]
        mov     [dic_count], ax
        mov     ax, [header_buf + 10]
        mov     [dic_index_off], ax

        ; ---- 二分探索 ----
        mov     word [search_lo], 0
        mov     ax, [dic_count]
        dec     ax
        mov     [search_hi], ax

.search_loop:
        mov     ax, [search_lo]
        cmp     ax, [search_hi]
        jg      .notfound               ; lo > hi (signed) -> 見つからない

        mov     ax, [search_lo]
        add     ax, [search_hi]
        shr     ax, 1
        mov     [search_mid], ax

        mov     ax, [search_mid]
        mov     cx, 20
        mul     cx                      ; dx:ax = mid * 20
        add     ax, [dic_index_off]
        mov     dx, ax
        xor     cx, cx
        mov     bx, [dic_handle]
        mov     ax, 0x4200              ; seek, SEEK_SET
        int     0x21
        jc      .io_err

        mov     bx, [dic_handle]
        mov     cx, 20
        mov     dx, entry_buf
        mov     ah, 0x3F
        int     0x21
        jc      .io_err
        cmp     ax, 20
        jne     .io_err

        mov     si, entry_buf
        mov     di, yomibuf
        mov     cx, YOMIBUFSIZE
        repe    cmpsb
        je      .found

        dec     si
        dec     di
        mov     al, [si]                ; 索引側(entry_buf)のバイト
        cmp     al, [di]                ; 目的のよみ(yomibuf)のバイト
        jb      .go_upper               ; 索引側が小さい -> 後半を探す

        mov     ax, [search_mid]
        dec     ax
        mov     [search_hi], ax
        jmp     .search_loop
.go_upper:
        mov     ax, [search_mid]
        inc     ax
        mov     [search_lo], ax
        jmp     .search_loop

.found:
        mov     ax, [entry_buf + YOMIBUFSIZE]  ; 候補ブロックの絶対オフセット(下位ワード)
        mov     dx, ax
        xor     cx, cx
        mov     bx, [dic_handle]
        mov     ax, 0x4200
        int     0x21
        jc      .io_err

        mov     bx, [dic_handle]
        mov     cx, 1
        mov     dx, small_buf
        mov     ah, 0x3F
        int     0x21
        jc      .io_err
        cmp     ax, 1
        jne     .io_err
        mov     al, [small_buf]
        mov     [cand_count], al
        mov     byte [cand_num], 1

.cand_loop:
        mov     al, [cand_num]
        cmp     al, [cand_count]
        ja      .cand_done

        mov     al, [cand_num]
        call    print_decimal
        mov     dl, ':'
        mov     ah, 0x02
        int     0x21

        mov     bx, [dic_handle]
        mov     cx, 1
        mov     dx, small_buf
        mov     ah, 0x3F
        int     0x21
        jc      .io_err
        cmp     ax, 1
        jne     .io_err
        mov     al, [small_buf]
        mov     [cand_len], al

        mov     cl, [cand_len]
        xor     ch, ch
        mov     bx, [dic_handle]
        mov     dx, cand_text_buf
        mov     ah, 0x3F
        int     0x21
        jc      .io_err
        mov     cl, [cand_len]          ; 要求バイト数を読み直す(cx がDOS呼び出しで
        xor     ch, ch                  ; 変化していても影響を受けないようにする)
        cmp     ax, cx                  ; ax = 実際に読めたバイト数(int21の戻り値のまま)
        jne     .io_err

        mov     si, cand_text_buf
        mov     cl, [cand_len]
        xor     ch, ch
        call    print_bytes

        mov     al, [cand_num]
        cmp     al, [cand_count]
        jae     .no_space
        mov     dl, ' '
        mov     ah, 0x02
        int     0x21
.no_space:
        inc     byte [cand_num]
        jmp     .cand_loop

.cand_done:
        call    print_crlf
        jmp     .exit_ok

.notfound:
        mov     dx, msg_notfound
        mov     ah, 0x09
        int     0x21
        jmp     .exit_ok

.exit_ok:
        mov     bx, [dic_handle]
        mov     ah, 0x3E
        int     0x21
        mov     ax, 0x4C00
        int     0x21

.open_err:
        mov     dx, msg_open_err
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4C02
        int     0x21

.header_read_err:
        mov     dx, msg_header_err
        mov     ah, 0x09
        int     0x21
        jmp     .fatal_close

.magic_err:
        mov     dx, msg_magic_err
        mov     ah, 0x09
        int     0x21
        jmp     .fatal_close

.io_err:
        mov     dx, msg_io_err
        mov     ah, 0x09
        int     0x21
.fatal_close:
        mov     bx, [dic_handle]
        mov     ah, 0x3E
        int     0x21
        mov     ax, 0x4C03
        int     0x21

;--------------------------------------------------------------------
; 出力ヘルパー
;--------------------------------------------------------------------

; SI = バイト列, CX = バイト数
print_bytes:
        push    ax
        push    cx
        push    dx
        push    si
.loop:
        jcxz    .done
        mov     al, [si]
        mov     dl, al
        mov     ah, 0x02
        int     0x21
        inc     si
        loop    .loop
.done:
        pop     si
        pop     dx
        pop     cx
        pop     ax
        ret

print_crlf:
        push    ax
        push    dx
        mov     dl, CR
        mov     ah, 0x02
        int     0x21
        mov     dl, LF
        mov     ah, 0x02
        int     0x21
        pop     dx
        pop     ax
        ret

; AL = 0-255 の値を10進数で表示(先頭ゼロなし)
print_decimal:
        push    ax
        push    bx
        push    cx
        push    dx
        xor     ah, ah
        mov     bx, 10
        xor     cx, cx
.pndiv:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        or      ax, ax
        jnz     .pndiv
.pnprint:
        pop     dx
        add     dl, '0'
        mov     ah, 0x02
        int     0x21
        loop    .pnprint
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;--------------------------------------------------------------------
; よみバッファへの確定出力(溢れたら黙って捨てる。FEP.ASM の try_append 相当)
;--------------------------------------------------------------------

; SI = 追加するバイト列, CL = バイト数
yomi_append:
        push    ax
        push    bx
        push    cx
        push    si
        push    di

        mov     al, [yomilen]
        xor     ah, ah
        mov     bl, cl
        xor     bh, bh
        add     ax, bx
        cmp     ax, YOMIBUFSIZE
        ja      .full

        mov     [ya_count], cl

        mov     al, [yomilen]
        xor     ah, ah
        mov     di, yomibuf
        add     di, ax
.copyloop:
        jcxz    .copydone
        mov     al, [si]
        mov     [di], al
        inc     si
        inc     di
        loop    .copyloop
.copydone:
        mov     al, [yomilen]
        add     al, [ya_count]
        mov     [yomilen], al

.full:
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret

; AL = 1バイトを追加する
yomi_append_ascii:
        push    si
        push    cx
        mov     [ya_scratch], al
        mov     si, ya_scratch
        mov     cl, 1
        call    yomi_append
        pop     cx
        pop     si
        ret

;--------------------------------------------------------------------
; ROMAJI.INC テーブル検索(FEP.ASM の find_exact / find_prefix と同一)
;--------------------------------------------------------------------

; CL = 照合するバイト数(romabuf の先頭 CL バイトを見る)。
; 完全一致: CF=1, SI = かなバイト列へのポインタ, AL = かなのバイト数。
; 不一致  : CF=0
find_exact:
        push    bx
        push    cx
        push    dx
        push    di
        push    bp
        mov     bl, cl
        mov     si, romaji_table
.rec:
        mov     al, [si]
        or      al, al
        jz      .nf
        mov     dl, [si+1]
        xor     dh, dh
        add     si, 2
        cmp     al, bl
        jne     .skip
        mov     di, si
        mov     bp, romabuf
        mov     cl, al
        xor     ch, ch
.cmp:
        mov     ah, [bp]
        cmp     ah, [di]
        jne     .skip
        inc     bp
        inc     di
        loop    .cmp
        mov     si, di
        mov     al, dl
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        stc
        ret
.skip:
        xor     ah, ah
        add     si, ax
        add     si, dx
        jmp     .rec
.nf:
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        clc
        ret

; CL = romabuf の先頭 CL バイト。これより長いエントリの先頭一致があれば CF=1。
find_prefix:
        push    ax
        push    bx
        push    cx
        push    dx
        push    di
        push    bp
        mov     bl, cl
        mov     si, romaji_table
.rec:
        mov     al, [si]
        or      al, al
        jz      .nf
        mov     dl, [si+1]
        xor     dh, dh
        add     si, 2
        cmp     al, bl
        jbe     .skip
        mov     di, si
        mov     bp, romabuf
        mov     cl, bl
        xor     ch, ch
.cmp:
        mov     ah, [bp]
        cmp     ah, [di]
        jne     .skip
        inc     bp
        inc     di
        loop    .cmp
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        stc
        ret
.skip:
        xor     ah, ah
        add     si, ax
        add     si, dx
        jmp     .rec
.nf:
        pop     bp
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        clc
        ret

;--------------------------------------------------------------------
; romabuf の先頭 BL バイトを捨てて詰める (romalen -= BL)
;--------------------------------------------------------------------
shift_romabuf_left:
        push    ax
        push    bx
        push    cx
        push    si
        push    di
        xor     bh, bh
        mov     al, [romalen]
        sub     al, bl
        mov     cl, al
        xor     ch, ch
        mov     si, romabuf
        add     si, bx
        mov     di, romabuf
.loop:
        jcxz    .done
        mov     al, [si]
        mov     [di], al
        inc     si
        inc     di
        loop    .loop
.done:
        mov     al, [romalen]
        sub     al, bl
        mov     [romalen], al
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret

; AH = 文字。母音(a/i/u/e/o)なら CF=1。
is_vowel:
        cmp     ah, 'a'
        je      .yes
        cmp     ah, 'i'
        je      .yes
        cmp     ah, 'u'
        je      .yes
        cmp     ah, 'e'
        je      .yes
        cmp     ah, 'o'
        je      .yes
        clc
        ret
.yes:
        stc
        ret

; AL = 文字。子音(a-z、母音・n以外)なら CF=1。
is_consonant_al:
        cmp     al, 'a'
        jb      .no
        cmp     al, 'z'
        ja      .no
        cmp     al, 'a'
        je      .no
        cmp     al, 'i'
        je      .no
        cmp     al, 'u'
        je      .no
        cmp     al, 'e'
        je      .no
        cmp     al, 'o'
        je      .no
        cmp     al, 'n'
        je      .no
        stc
        ret
.no:
        clc
        ret

emit_n_sound:
        push    si
        push    cx
        mov     si, sound_n
        mov     cl, 2
        call    yomi_append
        pop     cx
        pop     si
        ret

emit_sokuon_sound:
        push    si
        push    cx
        mov     si, sound_sokuon
        mov     cl, 2
        call    yomi_append
        pop     cx
        pop     si
        ret

flush_romabuf_literal:
        push    ax
        push    cx
        push    si
        mov     cl, [romalen]
        xor     ch, ch
        mov     si, romabuf
.loop:
        jcxz    .done
        mov     al, [si]
        push    cx
        push    si
        call    yomi_append_ascii
        pop     si
        pop     cx
        inc     si
        loop    .loop
.done:
        mov     byte [romalen], 0
        pop     si
        pop     cx
        pop     ax
        ret

; CR確定時: romabuf の残りを確定する。
;   残り == "n" (1バイト)  -> ん
;   残り == "nn"(2バイト)  -> ん
;   それ以外               -> 素通し
finalize_romabuf:
        mov     al, [romalen]
        cmp     al, 1
        jne     .chk2
        cmp     byte [romabuf], 'n'
        jne     .passall
        call    emit_n_sound
        mov     byte [romalen], 0
        ret
.chk2:
        cmp     al, 2
        jne     .passall
        cmp     byte [romabuf], 'n'
        jne     .passall
        cmp     byte [romabuf+1], 'n'
        jne     .passall
        call    emit_n_sound
        mov     byte [romalen], 0
        ret
.passall:
        call    flush_romabuf_literal
        ret

;--------------------------------------------------------------------
; ローマ字->かな 変換本体 (romabuf に1文字足した直後に呼ぶ)
;--------------------------------------------------------------------
resolve_romaji:
.loop:
        mov     al, [romalen]
        or      al, al
        jz      .ret

        cmp     al, 2
        jb      .no_nn
        mov     ah, [romabuf]
        cmp     ah, 'n'
        jne     .no_nn
        mov     ah, [romabuf+1]
        cmp     ah, 'n'
        jne     .no_nn
        cmp     al, 2
        je      .ret
        mov     ah, [romabuf+2]
        call    is_vowel
        jc      .nn_vowel
        call    emit_n_sound
        mov     bl, 2
        call    shift_romabuf_left
        jmp     .loop
.nn_vowel:
        call    emit_n_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.no_nn:
        mov     cl, [romalen]
        call    find_exact
        jc      .exact_found
        mov     cl, [romalen]
        call    find_prefix
        jc      .ret

        mov     al, [romalen]
        cmp     al, 2
        jb      .passthrough
        cmp     byte [romabuf], 'n'
        jne     .chk_double
        call    emit_n_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop
.chk_double:
        mov     al, [romabuf]
        mov     ah, [romabuf+1]
        cmp     al, ah
        jne     .passthrough
        call    is_consonant_al
        jnc     .passthrough
        call    emit_sokuon_sound
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.passthrough:
        mov     al, [romabuf]
        call    yomi_append_ascii
        mov     bl, 1
        call    shift_romabuf_left
        jmp     .loop

.exact_found:
        mov     cl, al
        call    yomi_append
        mov     byte [romalen], 0
        jmp     .loop
.ret:
        ret

;--------------------------------------------------------------------
; データ
;--------------------------------------------------------------------
        align   2

argbuf:         times   ARGBUFSIZE db 0
arglen:         db      0

romabuf:        times   ROMABUFSIZE db 0
romalen:        db      0

yomibuf:        times   YOMIBUFSIZE db 0
yomilen:        db      0

ya_count:       db      0
ya_scratch:     db      0

sound_n:        db      0x82, 0xF1      ; "ん"
sound_sokuon:   db      0x82, 0xC1      ; "っ"

dic_filename:   db      'TSUKUSHI.DIC', 0
dic_handle:     dw      0
dic_magic:      db      'FEPDIC01'
dic_count:      dw      0
dic_index_off:  dw      0

header_buf:     times   16 db 0
entry_buf:      times   20 db 0
small_buf:      db      0

search_lo:      dw      0
search_hi:      dw      0
search_mid:     dw      0

cand_count:     db      0
cand_num:       db      0
cand_len:       db      0
cand_text_buf:  times   CANDTEXTBUFSIZE db 0

msg_usage:      db      'usage: DICTEST <romaji>', CR, LF, '$'
msg_open_err:   db      'TSUKUSHI.DIC: open error', CR, LF, '$'
msg_header_err: db      'TSUKUSHI.DIC: header read error', CR, LF, '$'
msg_magic_err:  db      'TSUKUSHI.DIC: bad magic (format error)', CR, LF, '$'
msg_io_err:     db      'TSUKUSHI.DIC: read/seek error', CR, LF, '$'
msg_notfound:   db      'not found', CR, LF, '$'

msg_yomi_prefix:
        db      '[yomi] '
msg_yomi_prefix_len equ $ - msg_yomi_prefix

%include "ROMAJI.INC"
