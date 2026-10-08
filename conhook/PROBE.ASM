; PROBE.COM - コンソール出力が INT 29h / INT 21h のどれを通るかを数える測定器(製品ではない)
; ヘッダ(シグネチャ 'CONH')は 0x103。カウンタは +8.. にバイト数で入る。
; nasm -f bin PROBE.ASM -o PROBE.COM
        org 100h
        jmp install
hdr:                            ; = 0x103
        db 'CONH'
        dw 0
        dw 0
n29:    dd 0                    ; +8  INT 29h のバイト数
n02:    dd 0                    ; +12 AH=02h
n06:    dd 0                    ; +16 AH=06h (DL!=FFh)
n09:    dd 0                    ; +20 AH=09h
n40:    dd 0                    ; +24 AH=40h BX=1/2
c40:    dd 0                    ; +28 AH=40h の呼び出し回数
old29:  dd 0
old21:  dd 0

int29:
        pushf
        inc word [cs:n29]
        jnz .1
        inc word [cs:n29+2]
.1:     popf
        jmp far [cs:old29]

int21:
        pushf
        cmp ah,02h
        jne .a
        mov word [cs:cnt],n02
        call add1
        jmp .done
.a:     cmp ah,06h
        jne .b
        cmp dl,0FFh
        je .done
        mov word [cs:cnt],n06
        call add1
        jmp .done
.b:     cmp ah,09h
        jne .c
        push si
        push ax
        push ds
        mov si,dx
        xor ax,ax
.l9:    cmp byte [si],'$'
        je .e9
        inc si
        inc ax
        cmp ax,4000
        jb .l9
.e9:    pop ds
        mov [cs:amt],ax
        pop ax
        pop si
        mov word [cs:cnt],n09
        call addn
        jmp .done
.c:     cmp ah,40h
        jne .done
        cmp bx,1
        je .c1
        cmp bx,2
        jne .done
.c1:    mov [cs:amt],cx
        mov word [cs:cnt],n40
        call addn
        inc word [cs:c40]
.done:  popf
        jmp far [cs:old21]
cnt:    dw 0
amt:    dw 0
add1:   mov word [cs:amt],1
addn:   push bx
        mov bx,[cs:cnt]
        push ax
        mov ax,[cs:amt]
        add [cs:bx],ax
        adc word [cs:bx+2],0
        pop ax
        pop bx
        ret
resident_end:

install:
        mov ax,3529h
        int 21h
        mov [old29],bx
        mov [old29+2],es
        mov ax,3521h
        int 21h
        mov [old21],bx
        mov [old21+2],es
        mov ax,2529h
        mov dx,int29
        int 21h
        mov ax,2521h
        mov dx,int21
        int 21h
        mov dx,msg
        mov ah,09h
        int 21h
        mov dx,resident_end+15
        mov cl,4
        shr dx,cl
        mov ax,3100h
        int 21h
msg:    db 'PROBE resident',13,10,'$'
