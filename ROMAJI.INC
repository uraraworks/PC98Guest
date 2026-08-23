; ROMAJI.INC
; このファイルは tools/gen_romaji.py の生成物。手で編集しない。
; ローマ字->ひらがな変換テーブル(nasm %include 用)
;
; レコード形式(すべて db):
;   db romlen, kanalen
;   db <ローマ字 romlen バイト>
;   db <かなの Shift_JIS(CP932) kanalen バイト>
; テーブル終端は db 0, 0

romaji_table:
    db 1, 2
    db 2Ch	; ","
    db 81h, 41h	; "、"
    db 1, 2
    db 2Dh	; "-"
    db 81h, 5Bh	; "ー"
    db 1, 2
    db 2Eh	; "."
    db 81h, 42h	; "。"
    db 1, 2
    db 61h	; "a"
    db 82h, A0h	; "あ"
    db 2, 2
    db 62h, 61h	; "ba"
    db 82h, CEh	; "ば"
    db 2, 2
    db 62h, 65h	; "be"
    db 82h, D7h	; "べ"
    db 2, 2
    db 62h, 69h	; "bi"
    db 82h, D1h	; "び"
    db 2, 2
    db 62h, 6Fh	; "bo"
    db 82h, DAh	; "ぼ"
    db 2, 2
    db 62h, 75h	; "bu"
    db 82h, D4h	; "ぶ"
    db 3, 4
    db 62h, 79h, 61h	; "bya"
    db 82h, D1h, 82h, E1h	; "びゃ"
    db 3, 4
    db 62h, 79h, 6Fh	; "byo"
    db 82h, D1h, 82h, E5h	; "びょ"
    db 3, 4
    db 62h, 79h, 75h	; "byu"
    db 82h, D1h, 82h, E3h	; "びゅ"
    db 3, 4
    db 63h, 68h, 61h	; "cha"
    db 82h, BFh, 82h, E1h	; "ちゃ"
    db 3, 2
    db 63h, 68h, 69h	; "chi"
    db 82h, BFh	; "ち"
    db 3, 4
    db 63h, 68h, 6Fh	; "cho"
    db 82h, BFh, 82h, E5h	; "ちょ"
    db 3, 4
    db 63h, 68h, 75h	; "chu"
    db 82h, BFh, 82h, E3h	; "ちゅ"
    db 2, 2
    db 64h, 61h	; "da"
    db 82h, BEh	; "だ"
    db 2, 2
    db 64h, 65h	; "de"
    db 82h, C5h	; "で"
    db 2, 2
    db 64h, 69h	; "di"
    db 82h, C0h	; "ぢ"
    db 2, 2
    db 64h, 6Fh	; "do"
    db 82h, C7h	; "ど"
    db 2, 2
    db 64h, 75h	; "du"
    db 82h, C3h	; "づ"
    db 1, 2
    db 65h	; "e"
    db 82h, A6h	; "え"
    db 2, 2
    db 66h, 75h	; "fu"
    db 82h, D3h	; "ふ"
    db 2, 2
    db 67h, 61h	; "ga"
    db 82h, AAh	; "が"
    db 2, 2
    db 67h, 65h	; "ge"
    db 82h, B0h	; "げ"
    db 2, 2
    db 67h, 69h	; "gi"
    db 82h, ACh	; "ぎ"
    db 2, 2
    db 67h, 6Fh	; "go"
    db 82h, B2h	; "ご"
    db 2, 2
    db 67h, 75h	; "gu"
    db 82h, AEh	; "ぐ"
    db 3, 4
    db 67h, 79h, 61h	; "gya"
    db 82h, ACh, 82h, E1h	; "ぎゃ"
    db 3, 4
    db 67h, 79h, 6Fh	; "gyo"
    db 82h, ACh, 82h, E5h	; "ぎょ"
    db 3, 4
    db 67h, 79h, 75h	; "gyu"
    db 82h, ACh, 82h, E3h	; "ぎゅ"
    db 2, 2
    db 68h, 61h	; "ha"
    db 82h, CDh	; "は"
    db 2, 2
    db 68h, 65h	; "he"
    db 82h, D6h	; "へ"
    db 2, 2
    db 68h, 69h	; "hi"
    db 82h, D0h	; "ひ"
    db 2, 2
    db 68h, 6Fh	; "ho"
    db 82h, D9h	; "ほ"
    db 2, 2
    db 68h, 75h	; "hu"
    db 82h, D3h	; "ふ"
    db 3, 4
    db 68h, 79h, 61h	; "hya"
    db 82h, D0h, 82h, E1h	; "ひゃ"
    db 3, 4
    db 68h, 79h, 6Fh	; "hyo"
    db 82h, D0h, 82h, E5h	; "ひょ"
    db 3, 4
    db 68h, 79h, 75h	; "hyu"
    db 82h, D0h, 82h, E3h	; "ひゅ"
    db 1, 2
    db 69h	; "i"
    db 82h, A2h	; "い"
    db 2, 4
    db 6Ah, 61h	; "ja"
    db 82h, B6h, 82h, E1h	; "じゃ"
    db 2, 2
    db 6Ah, 69h	; "ji"
    db 82h, B6h	; "じ"
    db 2, 4
    db 6Ah, 6Fh	; "jo"
    db 82h, B6h, 82h, E5h	; "じょ"
    db 2, 4
    db 6Ah, 75h	; "ju"
    db 82h, B6h, 82h, E3h	; "じゅ"
    db 2, 2
    db 6Bh, 61h	; "ka"
    db 82h, A9h	; "か"
    db 2, 2
    db 6Bh, 65h	; "ke"
    db 82h, AFh	; "け"
    db 2, 2
    db 6Bh, 69h	; "ki"
    db 82h, ABh	; "き"
    db 2, 2
    db 6Bh, 6Fh	; "ko"
    db 82h, B1h	; "こ"
    db 2, 2
    db 6Bh, 75h	; "ku"
    db 82h, ADh	; "く"
    db 3, 4
    db 6Bh, 79h, 61h	; "kya"
    db 82h, ABh, 82h, E1h	; "きゃ"
    db 3, 4
    db 6Bh, 79h, 6Fh	; "kyo"
    db 82h, ABh, 82h, E5h	; "きょ"
    db 3, 4
    db 6Bh, 79h, 75h	; "kyu"
    db 82h, ABh, 82h, E3h	; "きゅ"
    db 2, 2
    db 6Ch, 61h	; "la"
    db 82h, 9Fh	; "ぁ"
    db 2, 2
    db 6Ch, 65h	; "le"
    db 82h, A5h	; "ぇ"
    db 2, 2
    db 6Ch, 69h	; "li"
    db 82h, A1h	; "ぃ"
    db 2, 2
    db 6Ch, 6Fh	; "lo"
    db 82h, A7h	; "ぉ"
    db 2, 2
    db 6Ch, 75h	; "lu"
    db 82h, A3h	; "ぅ"
    db 2, 2
    db 6Dh, 61h	; "ma"
    db 82h, DCh	; "ま"
    db 2, 2
    db 6Dh, 65h	; "me"
    db 82h, DFh	; "め"
    db 2, 2
    db 6Dh, 69h	; "mi"
    db 82h, DDh	; "み"
    db 2, 2
    db 6Dh, 6Fh	; "mo"
    db 82h, E0h	; "も"
    db 2, 2
    db 6Dh, 75h	; "mu"
    db 82h, DEh	; "む"
    db 3, 4
    db 6Dh, 79h, 61h	; "mya"
    db 82h, DDh, 82h, E1h	; "みゃ"
    db 3, 4
    db 6Dh, 79h, 6Fh	; "myo"
    db 82h, DDh, 82h, E5h	; "みょ"
    db 3, 4
    db 6Dh, 79h, 75h	; "myu"
    db 82h, DDh, 82h, E3h	; "みゅ"
    db 2, 2
    db 6Eh, 27h	; "n'"
    db 82h, F1h	; "ん"
    db 2, 2
    db 6Eh, 61h	; "na"
    db 82h, C8h	; "な"
    db 2, 2
    db 6Eh, 65h	; "ne"
    db 82h, CBh	; "ね"
    db 2, 2
    db 6Eh, 69h	; "ni"
    db 82h, C9h	; "に"
    db 2, 2
    db 6Eh, 6Eh	; "nn"
    db 82h, F1h	; "ん"
    db 2, 2
    db 6Eh, 6Fh	; "no"
    db 82h, CCh	; "の"
    db 2, 2
    db 6Eh, 75h	; "nu"
    db 82h, CAh	; "ぬ"
    db 3, 4
    db 6Eh, 79h, 61h	; "nya"
    db 82h, C9h, 82h, E1h	; "にゃ"
    db 3, 4
    db 6Eh, 79h, 6Fh	; "nyo"
    db 82h, C9h, 82h, E5h	; "にょ"
    db 3, 4
    db 6Eh, 79h, 75h	; "nyu"
    db 82h, C9h, 82h, E3h	; "にゅ"
    db 1, 2
    db 6Fh	; "o"
    db 82h, A8h	; "お"
    db 2, 2
    db 70h, 61h	; "pa"
    db 82h, CFh	; "ぱ"
    db 2, 2
    db 70h, 65h	; "pe"
    db 82h, D8h	; "ぺ"
    db 2, 2
    db 70h, 69h	; "pi"
    db 82h, D2h	; "ぴ"
    db 2, 2
    db 70h, 6Fh	; "po"
    db 82h, DBh	; "ぽ"
    db 2, 2
    db 70h, 75h	; "pu"
    db 82h, D5h	; "ぷ"
    db 3, 4
    db 70h, 79h, 61h	; "pya"
    db 82h, D2h, 82h, E1h	; "ぴゃ"
    db 3, 4
    db 70h, 79h, 6Fh	; "pyo"
    db 82h, D2h, 82h, E5h	; "ぴょ"
    db 3, 4
    db 70h, 79h, 75h	; "pyu"
    db 82h, D2h, 82h, E3h	; "ぴゅ"
    db 2, 2
    db 72h, 61h	; "ra"
    db 82h, E7h	; "ら"
    db 2, 2
    db 72h, 65h	; "re"
    db 82h, EAh	; "れ"
    db 2, 2
    db 72h, 69h	; "ri"
    db 82h, E8h	; "り"
    db 2, 2
    db 72h, 6Fh	; "ro"
    db 82h, EBh	; "ろ"
    db 2, 2
    db 72h, 75h	; "ru"
    db 82h, E9h	; "る"
    db 3, 4
    db 72h, 79h, 61h	; "rya"
    db 82h, E8h, 82h, E1h	; "りゃ"
    db 3, 4
    db 72h, 79h, 6Fh	; "ryo"
    db 82h, E8h, 82h, E5h	; "りょ"
    db 3, 4
    db 72h, 79h, 75h	; "ryu"
    db 82h, E8h, 82h, E3h	; "りゅ"
    db 2, 2
    db 73h, 61h	; "sa"
    db 82h, B3h	; "さ"
    db 2, 2
    db 73h, 65h	; "se"
    db 82h, B9h	; "せ"
    db 3, 4
    db 73h, 68h, 61h	; "sha"
    db 82h, B5h, 82h, E1h	; "しゃ"
    db 3, 2
    db 73h, 68h, 69h	; "shi"
    db 82h, B5h	; "し"
    db 3, 4
    db 73h, 68h, 6Fh	; "sho"
    db 82h, B5h, 82h, E5h	; "しょ"
    db 3, 4
    db 73h, 68h, 75h	; "shu"
    db 82h, B5h, 82h, E3h	; "しゅ"
    db 2, 2
    db 73h, 69h	; "si"
    db 82h, B5h	; "し"
    db 2, 2
    db 73h, 6Fh	; "so"
    db 82h, BBh	; "そ"
    db 2, 2
    db 73h, 75h	; "su"
    db 82h, B7h	; "す"
    db 3, 4
    db 73h, 79h, 61h	; "sya"
    db 82h, B5h, 82h, E1h	; "しゃ"
    db 3, 4
    db 73h, 79h, 6Fh	; "syo"
    db 82h, B5h, 82h, E5h	; "しょ"
    db 3, 4
    db 73h, 79h, 75h	; "syu"
    db 82h, B5h, 82h, E3h	; "しゅ"
    db 2, 2
    db 74h, 61h	; "ta"
    db 82h, BDh	; "た"
    db 2, 2
    db 74h, 65h	; "te"
    db 82h, C4h	; "て"
    db 2, 2
    db 74h, 69h	; "ti"
    db 82h, BFh	; "ち"
    db 2, 2
    db 74h, 6Fh	; "to"
    db 82h, C6h	; "と"
    db 3, 2
    db 74h, 73h, 75h	; "tsu"
    db 82h, C2h	; "つ"
    db 2, 2
    db 74h, 75h	; "tu"
    db 82h, C2h	; "つ"
    db 3, 4
    db 74h, 79h, 61h	; "tya"
    db 82h, BFh, 82h, E1h	; "ちゃ"
    db 3, 4
    db 74h, 79h, 6Fh	; "tyo"
    db 82h, BFh, 82h, E5h	; "ちょ"
    db 3, 4
    db 74h, 79h, 75h	; "tyu"
    db 82h, BFh, 82h, E3h	; "ちゅ"
    db 1, 2
    db 75h	; "u"
    db 82h, A4h	; "う"
    db 2, 2
    db 77h, 61h	; "wa"
    db 82h, EDh	; "わ"
    db 2, 2
    db 77h, 6Fh	; "wo"
    db 82h, F0h	; "を"
    db 2, 2
    db 78h, 61h	; "xa"
    db 82h, 9Fh	; "ぁ"
    db 2, 2
    db 78h, 65h	; "xe"
    db 82h, A5h	; "ぇ"
    db 2, 2
    db 78h, 69h	; "xi"
    db 82h, A1h	; "ぃ"
    db 2, 2
    db 78h, 6Fh	; "xo"
    db 82h, A7h	; "ぉ"
    db 2, 2
    db 78h, 75h	; "xu"
    db 82h, A3h	; "ぅ"
    db 2, 2
    db 79h, 61h	; "ya"
    db 82h, E2h	; "や"
    db 2, 2
    db 79h, 6Fh	; "yo"
    db 82h, E6h	; "よ"
    db 2, 2
    db 79h, 75h	; "yu"
    db 82h, E4h	; "ゆ"
    db 2, 2
    db 7Ah, 61h	; "za"
    db 82h, B4h	; "ざ"
    db 2, 2
    db 7Ah, 65h	; "ze"
    db 82h, BAh	; "ぜ"
    db 2, 2
    db 7Ah, 69h	; "zi"
    db 82h, B6h	; "じ"
    db 2, 2
    db 7Ah, 6Fh	; "zo"
    db 82h, BCh	; "ぞ"
    db 2, 2
    db 7Ah, 75h	; "zu"
    db 82h, B8h	; "ず"
    db 3, 4
    db 7Ah, 79h, 61h	; "zya"
    db 82h, B6h, 82h, E1h	; "じゃ"
    db 3, 4
    db 7Ah, 79h, 6Fh	; "zyo"
    db 82h, B6h, 82h, E5h	; "じょ"
    db 3, 4
    db 7Ah, 79h, 75h	; "zyu"
    db 82h, B6h, 82h, E3h	; "じゅ"
    db 0, 0
