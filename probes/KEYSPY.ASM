; KEYSPY.COM - PC-98 キーボード 実キー押下 測定プログラム
;
; 目的:
;   実際のキー押下に対して キーボードBIOS(INT 18h) が返す値を画面に出し続ける。
;   自動操作(_webnp2_push_key_buffer)はバッファへの直接投入なので、
;   キーコードの変換経路より手前を通らない。**実キーでしか測れない範囲**を
;   人が押して読み取るための測定器。
;
; 測ること:
;   ・AH=00h(入力待ち) が返す AH / AL の中身
;   ・同じ文字でも通常キーとテンキーで値が違うか
;   ・特殊キー(ROLL UP/DOWN, HOME CLR, INS, DEL, ひらがな, NFER/XFER 等)の値
;   ・AH=02h が修飾キーの状態を返すか、返すならビット割り当て
;
; 画面はテキストVRAMへ直接書く(0xA0000/0xA2000)。DOSの出力を使うと
; スクロールで履歴が流れるため。
;
; 終了: ESC を2回続けて押す
;
; ビルド: nasm -f bin KEYSPY.ASM -o KEYSPY.COM

        cpu     8086
        org     0x100

CHR_SEG equ     0xA000
ATR_SEG equ     0xA200
ROW     equ     160
HIST    equ     16              ; 履歴の行数
HIST_TOP equ    6               ; 履歴の開始行

start:
        ; ---- 属性面を白・表示ありで埋める ----
        mov     ax, ATR_SEG
        mov     es, ax
        xor     di, di
        mov     ax, 0x00E1
        mov     cx, 2000
        rep     stosw

        ; ---- 見出し行(2行目)だけ反転にする ----
        mov     di, 2*ROW
        mov     ax, 0x00E5
        mov     cx, 44
        rep     stosw

        ; ---- 文字面を空白で埋める ----
        mov     ax, CHR_SEG
        mov     es, ax
        xor     di, di
        mov     ax, 0x0020
        mov     cx, 2000
        rep     stosw

        ; ---- タイトル ----
        mov     si, msg_title
        xor     di, di
        call    puts
        mov     si, msg_help
        mov     di, 1*ROW
        call    puts
        mov     si, msg_hdr
        mov     di, 4*ROW
        call    puts
        mov     si, msg_n
        mov     di, 3*ROW+28*2
        call    puts

        ; ---- 起動時に一度だけ AH=02h を試す ----
        ; ここで固まるなら AH=02h は使えない、という結果になる
        mov     si, msg_s0
        mov     di, 3*ROW
        call    puts
        mov     ah, 0x02
        int     0x18
        mov     [shift0], al
        mov     di, 3*ROW+14*2
        call    hexb

mainloop:
        ; ---- キー入力待ち ----
        mov     ah, 0x00
        int     0x18
        mov     [lastax], ax

        ; ---- 修飾キーの状態 ----
        mov     ah, 0x02
        int     0x18
        mov     [shift], al

        ; ---- 2行目: 最後の1打 ----
        mov     si, msg_last
        mov     di, 2*ROW
        call    puts
        mov     di, 2*ROW+9*2
        mov     al, [lastax+1]          ; AH
        call    hexb
        mov     di, 2*ROW+16*2
        mov     al, [lastax]            ; AL
        call    hexb
        mov     di, 2*ROW+26*2
        mov     al, [shift]
        call    hexb
        mov     di, 2*ROW+35*2
        mov     al, [lastax]
        call    putchr

        ; ---- 履歴に1行追加 ----
        mov     al, [count]
        and     al, HIST-1
        mov     ah, ROW
        mul     ah                      ; ax = (idx * 160)
        add     ax, HIST_TOP*ROW
        mov     di, ax
        push    di

        mov     al, [count]
        call    hexb                    ; 通し番号
        add     di, 2*2
        mov     al, [lastax+1]
        call    hexb                    ; AH
        add     di, 2*2
        mov     al, [lastax]
        call    hexb                    ; AL
        add     di, 2*2
        mov     al, [shift]
        call    hexb                    ; 修飾キー
        add     di, 2*2
        mov     al, [lastax]
        call    putchr                  ; 文字として表示

        pop     di
        inc     byte [count]

        ; ---- 押下回数(自動測定で「新しい入力が来たか」を判定するため) ----
        mov     di, 3*ROW+30*2
        mov     al, [count]
        call    hexb

        ; ---- ESC 2回で終了 ----
        mov     al, [lastax]
        cmp     al, 0x1B
        jne     .noesc
        cmp     byte [prev], 0x1B
        je      .quit
.noesc:
        mov     [prev], al
        jmp     mainloop

.quit:
        mov     ax, 0x4C00
        int     0x21

; ---------------------------------------------------------------
; puts: ds:si = 0終端文字列 / es:di = 書き込み先セル
; ---------------------------------------------------------------
puts:
        lodsb
        or      al, al
        jz      .done
        xor     ah, ah
        mov     [es:di], ax
        add     di, 2
        jmp     puts
.done:
        ret

; ---------------------------------------------------------------
; hexb: al = 値を2桁の16進で / es:di に書く(di は4進む) / bl を壊す
; ---------------------------------------------------------------
hexb:
        mov     bl, al
        shr     al, 1
        shr     al, 1
        shr     al, 1
        shr     al, 1
        call    hexnib
        mov     al, bl
        and     al, 0x0F
        call    hexnib
        ret
hexnib:
        cmp     al, 10
        jb      .dig
        add     al, 'A'-10
        jmp     .put
.dig:
        add     al, '0'
.put:
        xor     ah, ah
        mov     [es:di], ax
        add     di, 2
        ret

; ---------------------------------------------------------------
; putchr: al = 文字。表示できない値は '.' にする
; ---------------------------------------------------------------
putchr:
        cmp     al, 0x20
        jb      .dot
        cmp     al, 0x7F
        jae     .dot
        jmp     .put
.dot:
        mov     al, '.'
.put:
        xor     ah, ah
        mov     [es:di], ax
        add     di, 2
        ret

msg_title: db 'KEYSPY - PC-98 keyboard probe',0
msg_help:  db 'press keys / ESC twice to quit',0
msg_s0:    db 'AH=02h@start:',0
msg_last:  db 'LAST  AH=..  AL=..  SHIFT=..  CHAR=',0
msg_hdr:   db '##  AH  AL  SFT CH',0
msg_n:     db 'N=',0

count:  db 0
prev:   db 0
shift:  db 0
shift0: db 0
lastax: dw 0
