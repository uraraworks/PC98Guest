; CONHOOK.COM - コンソール出力の流れを常駐メモリのリングバッファへ記録する TSR
;
; 目的:
;   DOS のコンソール出力(INT 29h 高速コンソール出力)に渡された1バイトずつを
;   リングバッファへ積む。ホスト側(WebNP2)がエミュレータのメモリを直接読み、
;   画面の差分ではなく「出力の流れそのもの」を得る。
;   FreeDOS(98) では type/dir/mem/FreeCOM のエコーとプロンプトまですべて
;   INT 29h を通ることを実測済み(README.md 参照)。
;
; ヘッダ(セグメント内オフセット 0103h = 先頭の JMP の直後。ホストはここを読む):
;   +0  'CONH'           シグネチャ
;   +4  dw 1             版
;   +6  dw BUFSIZE       バッファサイズ(2の冪)
;   +8  dd total         書き込み総数(32bit 単調増加。位置 = total & (BUFSIZE-1))
;   +12 dw BUF           バッファのセグメント内オフセット
;   +14 dw 0
; ホストは割り込みベクタ 29h の指す先のセグメントを取り、そこの 0103h を調べる
; (シグネチャのメモリ全検索はしない)。
;
; 注意:
;   - バッファは install 部より後ろ(resb)に置き、resident_end はその末尾。
;   - 常駐サイズは resident_end: から計算する。install: より後ろに常駐後に参照する物を置かない。
;   - ハンドラ内で DOS を呼ばない(INT 29h は DOS の中から呼ばれる)。レジスタ・フラグは保存し、
;     元のハンドラへ jmp far でチェーンする。
;   - 二重常駐は INT 29h の指す先のシグネチャで検出し、何もせず終了する。
;
; nasm -f bin CONHOOK.ASM -o CONHOOK.COM

BUFSIZE equ 8000h

        org 100h
        jmp near install        ; 100h (3バイト固定。短いjmpだとヘッダが1バイト手前にずれる)
hdr:                            ; 103h
        db 'CONH'
        dw 1
        dw BUFSIZE
total:  dd 0
        dw buf
        dw 0

old29:  dd 0

int29:
        pushf
        cli
        push bx
        mov bx,[cs:total]
        and bx,BUFSIZE-1
        mov [cs:buf+bx],al
        add word [cs:total],1
        adc word [cs:total+2],0
        pop bx
        popf
        jmp far [cs:old29]

install:
        mov ax,3529h
        int 21h
        cmp word [es:hdr],'CO'
        jne .fresh
        cmp word [es:hdr+2],'NH'
        jne .fresh
        mov dx,msg_already
        mov ah,09h
        int 21h
        mov ax,4C00h
        int 21h
.fresh:
        mov [old29],bx
        mov [old29+2],es
        mov dx,int29
        mov ax,2529h
        int 21h
        mov dx,msg_ok
        mov ah,09h
        int 21h
        mov dx,resident_end+15
        mov cl,4
        shr dx,cl
        mov ax,3100h
        int 21h
msg_ok:      db 'CONHOOK resident',13,10,'$'
msg_already: db 'CONHOOK already resident',13,10,'$'

; バッファはファイルに載せない(resb)。install 部の後ろに置くので、起動中にバッファへ
; 書かれても install のコードやメッセージは壊れない。install 部も常駐範囲に含まれる(数十バイト)。
        align 16
buf:                            ; ここから BUFSIZE バイトがバッファ(ファイルには載らない)
resident_end equ buf+BUFSIZE
