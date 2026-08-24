; WIKIEX3.COM - Wiki「キーがあるか調べる(待たずに)」の例をそのまま実行して確かめる検証用
;
; 掲載スニペットは AH=01h -> BH判定 -> AH=00h の3手。ここではその後ろに
; 「取り出した文字を出して .nokey へ落ちる」処理を足してループにしている。
; int 21h がフラグを壊すので、BH の判定結果をあとから ZF で見直さないこと。
        cpu     8086
        org     0x100
start:
.poll:
        ; ---- ここから Wiki 掲載のコードそのまま ----
        mov     ah, 0x01
        int     0x18
        or      bh, bh
        jz      .nokey          ; キーは来ていない
        mov     ah, 0x00        ; 来ているので取り出す
        int     0x18
        ; ---- ここまで(AL に文字コード, AH にスキャンコード) ----
        cmp     al, 0x1B
        je      .done
        or      al, al
        jz      .nokey          ; 文字を持たないキーは読み飛ばす
        mov     dl, al
        mov     ah, 0x02
        int     0x21
.nokey:
        jmp     .poll
.done:
        mov     ax, 0x4C00
        int     0x21
