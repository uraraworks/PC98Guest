; TVRAM2.COM - PC-98 テキスト VRAM 測定(2) 属性の下位ビットと全角の格納方法
;
; TVRAM1 で確定したこと(この測定の前提):
;   ・文字コード面 0xA0000 / 属性面 0xA2000 / 1セル2バイト / 1行160バイト
;   ・画面は 80x25 = 2000セル ちょうど。0xA0FA0 以降は表示されない
;   ・属性の色は上位3ビット: b7=緑 b6=赤 b5=青。0xE1 で白・表示あり
;   ・全角は「上位バイト bit7 が右半分の印」らしい(TVRAM1 で 0xFF43 が
;     右半分扱いされ、対になる左半分が無いため何も描かれなかった)
;
; ここで測ること:
;   [1] 属性の下位5ビット(b4..b0)の意味。色を白(0xE0)に固定して1ビットずつ振る
;   [2] 全角のバイト順。TVRAM1 の結果から
;         下位バイト = JIS 第1バイト - 0x20 / 上位バイト = JIS 第2バイト
;       と読めたので、その式で「亜」「漢」を組み立てて実際に読めるか確かめる
;   [3] 右半分の印の付け方。4通り並べ、正しく1文字に見えるものを選ぶ
;   [4] ANK と全角の切り分け。下位を 'A' に固定して上位バイトを振る
;
; ビルド: nasm -f bin TVRAM2.ASM -o TVRAM2.COM

        cpu     8086
        org     0x100

CHR_SEG equ     0xA000
ATR_SEG equ     0xA200
ROW     equ     160

; 「亜」JIS 0x3021 -> 下位 0x30-0x20=0x10 / 上位 0x21  => 0x2110
AZA     equ     0x2110
AZA_R   equ     0xA110          ; 上位に 0x80 を足したもの
; 「漢」JIS 0x3441 -> 下位 0x34-0x20=0x14 / 上位 0x41  => 0x4114
KAN     equ     0x4114
KAN_R   equ     0xC114

start:
        ; ---- 属性面を 0xE1(白・表示あり) で埋める ----
        mov     ax, ATR_SEG
        mov     es, ax
        xor     di, di
        mov     ax, 0x00E1
        mov     cx, 2000
        rep     stosw

        ; ---- 文字コード面を空白で埋める ----
        mov     ax, CHR_SEG
        mov     es, ax
        xor     di, di
        mov     ax, 0x0020
        mov     cx, 2000
        rep     stosw

        ; ---- [1] 2行目: 'X' を32個 ----
        mov     di, 2*ROW
        mov     cx, 32
        mov     ax, 0x0058
        rep     stosw

        ; ---- [2][3] 4行目: 全角の並べ方4通り + 「漢」----
        mov     si, ktab
.kl:
        lodsw
        cmp     ax, 0xFFFF
        je      .kdone
        mov     di, ax
        lodsw
        mov     [es:di], ax
        jmp     .kl
.kdone:

        ; ---- [4] 6行目: 下位を 'A' に固定して上位バイトを振る ----
        mov     si, atab
.al:
        lodsw
        cmp     ax, 0xFFFF
        je      .adone
        mov     di, ax
        lodsw
        mov     [es:di], ax
        jmp     .al
.adone:

        ; ---- [1] 2行目の属性: 色を白に固定し下位5ビットを 0x00..0x1F ----
        mov     ax, ATR_SEG
        mov     es, ax
        mov     di, 2*ROW
        mov     ax, 0x00E0
        mov     cx, 32
.a1:
        mov     [es:di], ax
        add     di, 2
        inc     ax
        loop    .a1

        ; ---- キー待ち ----
        mov     ah, 0x07
        int     0x21
        mov     ax, 0x4C00
        int     0x21

; ---------------------------------------------------------------
; 4行目(オフセット 4*160 = 0x280)。セル番号は行頭からの通し。
;   cell 0,1   : 左=亜 右=亜|0x8000      … 上位 bit7 を右半分の印とする
;   cell 4,5   : 左=亜 右=亜(印なし)      … 印が要るのかを見る
;   cell 8,9   : 左=亜 右=空白            … 対を書かない場合
;   cell 12,13 : 左=亜|0x8000 右=亜      … 印を左に付けた場合(逆順)
;   cell 16,17 : 左=漢 右=漢|0x8000      … 別の字でも同じ式が通るか
; ---------------------------------------------------------------
ktab:
        dw      4*ROW+0*2,  AZA
        dw      4*ROW+1*2,  AZA_R
        dw      4*ROW+4*2,  AZA
        dw      4*ROW+5*2,  AZA
        dw      4*ROW+8*2,  AZA
        dw      4*ROW+12*2, AZA_R
        dw      4*ROW+13*2, AZA
        dw      4*ROW+16*2, KAN
        dw      4*ROW+17*2, KAN_R
        dw      0xFFFF

; ---------------------------------------------------------------
; 6行目。下位バイトは 'A'(0x41) 固定、上位バイトだけを変える。
; 上位が何以上で全角として扱われるかの境界を見る。
; ---------------------------------------------------------------
atab:
        dw      6*ROW+0*2,  0x0041
        dw      6*ROW+2*2,  0x0141
        dw      6*ROW+4*2,  0x1041
        dw      6*ROW+6*2,  0x2041
        dw      6*ROW+8*2,  0x2141
        dw      6*ROW+10*2, 0x4041
        dw      6*ROW+12*2, 0x7F41
        dw      0xFFFF
