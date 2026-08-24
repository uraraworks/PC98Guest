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
    db 02Ch	; ","
    db 081h, 041h	; "、"
    db 1, 2
    db 02Dh	; "-"
    db 081h, 05Bh	; "ー"
    db 1, 2
    db 02Eh	; "."
    db 081h, 042h	; "。"
    db 1, 2
    db 061h	; "a"
    db 082h, 0A0h	; "あ"
    db 2, 2
    db 062h, 061h	; "ba"
    db 082h, 0CEh	; "ば"
    db 2, 2
    db 062h, 065h	; "be"
    db 082h, 0D7h	; "べ"
    db 2, 2
    db 062h, 069h	; "bi"
    db 082h, 0D1h	; "び"
    db 2, 2
    db 062h, 06Fh	; "bo"
    db 082h, 0DAh	; "ぼ"
    db 2, 2
    db 062h, 075h	; "bu"
    db 082h, 0D4h	; "ぶ"
    db 3, 4
    db 062h, 079h, 061h	; "bya"
    db 082h, 0D1h, 082h, 0E1h	; "びゃ"
    db 3, 4
    db 062h, 079h, 06Fh	; "byo"
    db 082h, 0D1h, 082h, 0E5h	; "びょ"
    db 3, 4
    db 062h, 079h, 075h	; "byu"
    db 082h, 0D1h, 082h, 0E3h	; "びゅ"
    db 3, 4
    db 063h, 068h, 061h	; "cha"
    db 082h, 0BFh, 082h, 0E1h	; "ちゃ"
    db 3, 2
    db 063h, 068h, 069h	; "chi"
    db 082h, 0BFh	; "ち"
    db 3, 4
    db 063h, 068h, 06Fh	; "cho"
    db 082h, 0BFh, 082h, 0E5h	; "ちょ"
    db 3, 4
    db 063h, 068h, 075h	; "chu"
    db 082h, 0BFh, 082h, 0E3h	; "ちゅ"
    db 2, 2
    db 064h, 061h	; "da"
    db 082h, 0BEh	; "だ"
    db 2, 2
    db 064h, 065h	; "de"
    db 082h, 0C5h	; "で"
    db 2, 2
    db 064h, 069h	; "di"
    db 082h, 0C0h	; "ぢ"
    db 2, 2
    db 064h, 06Fh	; "do"
    db 082h, 0C7h	; "ど"
    db 2, 2
    db 064h, 075h	; "du"
    db 082h, 0C3h	; "づ"
    db 1, 2
    db 065h	; "e"
    db 082h, 0A6h	; "え"
    db 2, 2
    db 066h, 075h	; "fu"
    db 082h, 0D3h	; "ふ"
    db 2, 2
    db 067h, 061h	; "ga"
    db 082h, 0AAh	; "が"
    db 2, 2
    db 067h, 065h	; "ge"
    db 082h, 0B0h	; "げ"
    db 2, 2
    db 067h, 069h	; "gi"
    db 082h, 0ACh	; "ぎ"
    db 2, 2
    db 067h, 06Fh	; "go"
    db 082h, 0B2h	; "ご"
    db 2, 2
    db 067h, 075h	; "gu"
    db 082h, 0AEh	; "ぐ"
    db 3, 4
    db 067h, 079h, 061h	; "gya"
    db 082h, 0ACh, 082h, 0E1h	; "ぎゃ"
    db 3, 4
    db 067h, 079h, 06Fh	; "gyo"
    db 082h, 0ACh, 082h, 0E5h	; "ぎょ"
    db 3, 4
    db 067h, 079h, 075h	; "gyu"
    db 082h, 0ACh, 082h, 0E3h	; "ぎゅ"
    db 2, 2
    db 068h, 061h	; "ha"
    db 082h, 0CDh	; "は"
    db 2, 2
    db 068h, 065h	; "he"
    db 082h, 0D6h	; "へ"
    db 2, 2
    db 068h, 069h	; "hi"
    db 082h, 0D0h	; "ひ"
    db 2, 2
    db 068h, 06Fh	; "ho"
    db 082h, 0D9h	; "ほ"
    db 2, 2
    db 068h, 075h	; "hu"
    db 082h, 0D3h	; "ふ"
    db 3, 4
    db 068h, 079h, 061h	; "hya"
    db 082h, 0D0h, 082h, 0E1h	; "ひゃ"
    db 3, 4
    db 068h, 079h, 06Fh	; "hyo"
    db 082h, 0D0h, 082h, 0E5h	; "ひょ"
    db 3, 4
    db 068h, 079h, 075h	; "hyu"
    db 082h, 0D0h, 082h, 0E3h	; "ひゅ"
    db 1, 2
    db 069h	; "i"
    db 082h, 0A2h	; "い"
    db 2, 4
    db 06Ah, 061h	; "ja"
    db 082h, 0B6h, 082h, 0E1h	; "じゃ"
    db 2, 2
    db 06Ah, 069h	; "ji"
    db 082h, 0B6h	; "じ"
    db 2, 4
    db 06Ah, 06Fh	; "jo"
    db 082h, 0B6h, 082h, 0E5h	; "じょ"
    db 2, 4
    db 06Ah, 075h	; "ju"
    db 082h, 0B6h, 082h, 0E3h	; "じゅ"
    db 2, 2
    db 06Bh, 061h	; "ka"
    db 082h, 0A9h	; "か"
    db 2, 2
    db 06Bh, 065h	; "ke"
    db 082h, 0AFh	; "け"
    db 2, 2
    db 06Bh, 069h	; "ki"
    db 082h, 0ABh	; "き"
    db 2, 2
    db 06Bh, 06Fh	; "ko"
    db 082h, 0B1h	; "こ"
    db 2, 2
    db 06Bh, 075h	; "ku"
    db 082h, 0ADh	; "く"
    db 3, 4
    db 06Bh, 079h, 061h	; "kya"
    db 082h, 0ABh, 082h, 0E1h	; "きゃ"
    db 3, 4
    db 06Bh, 079h, 06Fh	; "kyo"
    db 082h, 0ABh, 082h, 0E5h	; "きょ"
    db 3, 4
    db 06Bh, 079h, 075h	; "kyu"
    db 082h, 0ABh, 082h, 0E3h	; "きゅ"
    db 2, 2
    db 06Ch, 061h	; "la"
    db 082h, 09Fh	; "ぁ"
    db 2, 2
    db 06Ch, 065h	; "le"
    db 082h, 0A5h	; "ぇ"
    db 2, 2
    db 06Ch, 069h	; "li"
    db 082h, 0A1h	; "ぃ"
    db 2, 2
    db 06Ch, 06Fh	; "lo"
    db 082h, 0A7h	; "ぉ"
    db 2, 2
    db 06Ch, 075h	; "lu"
    db 082h, 0A3h	; "ぅ"
    db 2, 2
    db 06Dh, 061h	; "ma"
    db 082h, 0DCh	; "ま"
    db 2, 2
    db 06Dh, 065h	; "me"
    db 082h, 0DFh	; "め"
    db 2, 2
    db 06Dh, 069h	; "mi"
    db 082h, 0DDh	; "み"
    db 2, 2
    db 06Dh, 06Fh	; "mo"
    db 082h, 0E0h	; "も"
    db 2, 2
    db 06Dh, 075h	; "mu"
    db 082h, 0DEh	; "む"
    db 3, 4
    db 06Dh, 079h, 061h	; "mya"
    db 082h, 0DDh, 082h, 0E1h	; "みゃ"
    db 3, 4
    db 06Dh, 079h, 06Fh	; "myo"
    db 082h, 0DDh, 082h, 0E5h	; "みょ"
    db 3, 4
    db 06Dh, 079h, 075h	; "myu"
    db 082h, 0DDh, 082h, 0E3h	; "みゅ"
    db 2, 2
    db 06Eh, 027h	; "n'"
    db 082h, 0F1h	; "ん"
    db 2, 2
    db 06Eh, 061h	; "na"
    db 082h, 0C8h	; "な"
    db 2, 2
    db 06Eh, 065h	; "ne"
    db 082h, 0CBh	; "ね"
    db 2, 2
    db 06Eh, 069h	; "ni"
    db 082h, 0C9h	; "に"
    db 2, 2
    db 06Eh, 06Eh	; "nn"
    db 082h, 0F1h	; "ん"
    db 2, 2
    db 06Eh, 06Fh	; "no"
    db 082h, 0CCh	; "の"
    db 2, 2
    db 06Eh, 075h	; "nu"
    db 082h, 0CAh	; "ぬ"
    db 3, 4
    db 06Eh, 079h, 061h	; "nya"
    db 082h, 0C9h, 082h, 0E1h	; "にゃ"
    db 3, 4
    db 06Eh, 079h, 06Fh	; "nyo"
    db 082h, 0C9h, 082h, 0E5h	; "にょ"
    db 3, 4
    db 06Eh, 079h, 075h	; "nyu"
    db 082h, 0C9h, 082h, 0E3h	; "にゅ"
    db 1, 2
    db 06Fh	; "o"
    db 082h, 0A8h	; "お"
    db 2, 2
    db 070h, 061h	; "pa"
    db 082h, 0CFh	; "ぱ"
    db 2, 2
    db 070h, 065h	; "pe"
    db 082h, 0D8h	; "ぺ"
    db 2, 2
    db 070h, 069h	; "pi"
    db 082h, 0D2h	; "ぴ"
    db 2, 2
    db 070h, 06Fh	; "po"
    db 082h, 0DBh	; "ぽ"
    db 2, 2
    db 070h, 075h	; "pu"
    db 082h, 0D5h	; "ぷ"
    db 3, 4
    db 070h, 079h, 061h	; "pya"
    db 082h, 0D2h, 082h, 0E1h	; "ぴゃ"
    db 3, 4
    db 070h, 079h, 06Fh	; "pyo"
    db 082h, 0D2h, 082h, 0E5h	; "ぴょ"
    db 3, 4
    db 070h, 079h, 075h	; "pyu"
    db 082h, 0D2h, 082h, 0E3h	; "ぴゅ"
    db 2, 2
    db 072h, 061h	; "ra"
    db 082h, 0E7h	; "ら"
    db 2, 2
    db 072h, 065h	; "re"
    db 082h, 0EAh	; "れ"
    db 2, 2
    db 072h, 069h	; "ri"
    db 082h, 0E8h	; "り"
    db 2, 2
    db 072h, 06Fh	; "ro"
    db 082h, 0EBh	; "ろ"
    db 2, 2
    db 072h, 075h	; "ru"
    db 082h, 0E9h	; "る"
    db 3, 4
    db 072h, 079h, 061h	; "rya"
    db 082h, 0E8h, 082h, 0E1h	; "りゃ"
    db 3, 4
    db 072h, 079h, 06Fh	; "ryo"
    db 082h, 0E8h, 082h, 0E5h	; "りょ"
    db 3, 4
    db 072h, 079h, 075h	; "ryu"
    db 082h, 0E8h, 082h, 0E3h	; "りゅ"
    db 2, 2
    db 073h, 061h	; "sa"
    db 082h, 0B3h	; "さ"
    db 2, 2
    db 073h, 065h	; "se"
    db 082h, 0B9h	; "せ"
    db 3, 4
    db 073h, 068h, 061h	; "sha"
    db 082h, 0B5h, 082h, 0E1h	; "しゃ"
    db 3, 2
    db 073h, 068h, 069h	; "shi"
    db 082h, 0B5h	; "し"
    db 3, 4
    db 073h, 068h, 06Fh	; "sho"
    db 082h, 0B5h, 082h, 0E5h	; "しょ"
    db 3, 4
    db 073h, 068h, 075h	; "shu"
    db 082h, 0B5h, 082h, 0E3h	; "しゅ"
    db 2, 2
    db 073h, 069h	; "si"
    db 082h, 0B5h	; "し"
    db 2, 2
    db 073h, 06Fh	; "so"
    db 082h, 0BBh	; "そ"
    db 2, 2
    db 073h, 075h	; "su"
    db 082h, 0B7h	; "す"
    db 3, 4
    db 073h, 079h, 061h	; "sya"
    db 082h, 0B5h, 082h, 0E1h	; "しゃ"
    db 3, 4
    db 073h, 079h, 06Fh	; "syo"
    db 082h, 0B5h, 082h, 0E5h	; "しょ"
    db 3, 4
    db 073h, 079h, 075h	; "syu"
    db 082h, 0B5h, 082h, 0E3h	; "しゅ"
    db 2, 2
    db 074h, 061h	; "ta"
    db 082h, 0BDh	; "た"
    db 2, 2
    db 074h, 065h	; "te"
    db 082h, 0C4h	; "て"
    db 2, 2
    db 074h, 069h	; "ti"
    db 082h, 0BFh	; "ち"
    db 2, 2
    db 074h, 06Fh	; "to"
    db 082h, 0C6h	; "と"
    db 3, 2
    db 074h, 073h, 075h	; "tsu"
    db 082h, 0C2h	; "つ"
    db 2, 2
    db 074h, 075h	; "tu"
    db 082h, 0C2h	; "つ"
    db 3, 4
    db 074h, 079h, 061h	; "tya"
    db 082h, 0BFh, 082h, 0E1h	; "ちゃ"
    db 3, 4
    db 074h, 079h, 06Fh	; "tyo"
    db 082h, 0BFh, 082h, 0E5h	; "ちょ"
    db 3, 4
    db 074h, 079h, 075h	; "tyu"
    db 082h, 0BFh, 082h, 0E3h	; "ちゅ"
    db 1, 2
    db 075h	; "u"
    db 082h, 0A4h	; "う"
    db 2, 2
    db 077h, 061h	; "wa"
    db 082h, 0EDh	; "わ"
    db 2, 2
    db 077h, 06Fh	; "wo"
    db 082h, 0F0h	; "を"
    db 2, 2
    db 078h, 061h	; "xa"
    db 082h, 09Fh	; "ぁ"
    db 2, 2
    db 078h, 065h	; "xe"
    db 082h, 0A5h	; "ぇ"
    db 2, 2
    db 078h, 069h	; "xi"
    db 082h, 0A1h	; "ぃ"
    db 2, 2
    db 078h, 06Fh	; "xo"
    db 082h, 0A7h	; "ぉ"
    db 2, 2
    db 078h, 075h	; "xu"
    db 082h, 0A3h	; "ぅ"
    db 2, 2
    db 079h, 061h	; "ya"
    db 082h, 0E2h	; "や"
    db 2, 2
    db 079h, 06Fh	; "yo"
    db 082h, 0E6h	; "よ"
    db 2, 2
    db 079h, 075h	; "yu"
    db 082h, 0E4h	; "ゆ"
    db 2, 2
    db 07Ah, 061h	; "za"
    db 082h, 0B4h	; "ざ"
    db 2, 2
    db 07Ah, 065h	; "ze"
    db 082h, 0BAh	; "ぜ"
    db 2, 2
    db 07Ah, 069h	; "zi"
    db 082h, 0B6h	; "じ"
    db 2, 2
    db 07Ah, 06Fh	; "zo"
    db 082h, 0BCh	; "ぞ"
    db 2, 2
    db 07Ah, 075h	; "zu"
    db 082h, 0B8h	; "ず"
    db 3, 4
    db 07Ah, 079h, 061h	; "zya"
    db 082h, 0B6h, 082h, 0E1h	; "じゃ"
    db 3, 4
    db 07Ah, 079h, 06Fh	; "zyo"
    db 082h, 0B6h, 082h, 0E5h	; "じょ"
    db 3, 4
    db 07Ah, 079h, 075h	; "zyu"
    db 082h, 0B6h, 082h, 0E3h	; "じゅ"
    db 0, 0
romaji_table_end:
