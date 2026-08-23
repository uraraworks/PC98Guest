#!/usr/bin/env python3
"""ローマ字→ひらがな変換テーブルを生成し、ROMAJI.INC(nasm include)として出力する。

このファイル(tools/gen_romaji.py)の生成物。ROMAJI.INC は手で編集しない。
変更したいときはこのスクリプトを直す。

かなは Shift_JIS(CP932)でエンコードする。'shift_jis' ではなく 'cp932' を使うこと
(0x5C 問題を避けるため。詳細は _emulator の記憶
 feedback_sjis_source_cp932_not_shiftjis.md 参照)。

テーブルの並び順はローマ字文字列の昇順(ソート済み)。
実行のたびに変わる値(生成日時等)は出力に含めない。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT_INC = HERE.parent / "ROMAJI.INC"

# (ローマ字, かな) のリスト。かなは Python の str(Unicode)。
ENTRIES = []


def add(romaji, kana):
    ENTRIES.append((romaji, kana))


# --- 母音 ---
for r, k in zip("aiueo", "あいうえお"):
    add(r, k)

# --- 清音(五十音・行ごと) ---
GYOU = {
    "k": ("ka", "ki", "ku", "ke", "ko", "かきくけこ"),
    "s": ("sa", "si", "su", "se", "so", "さしすせそ"),
    "t": ("ta", "ti", "tu", "te", "to", "たちつてと"),
    "n": ("na", "ni", "nu", "ne", "no", "なにぬねの"),
    "h": ("ha", "hi", "hu", "he", "ho", "はひふへほ"),
    "m": ("ma", "mi", "mu", "me", "mo", "まみむめも"),
    "r": ("ra", "ri", "ru", "re", "ro", "らりるれろ"),
}
for _, (r1, r2, r3, r4, r5, kana5) in GYOU.items():
    for r, k in zip((r1, r2, r3, r4, r5), kana5):
        add(r, k)

# ta 行の ti/tu の「ち」「つ」を明示(上のGYOUで既に "たちつてと" を使用しているので済み)

# ya 行(yi, ye は無し)
add("ya", "や")
add("yu", "ゆ")
add("yo", "よ")

# wa 行
add("wa", "わ")
add("wo", "を")

# --- ヘボン式の別綴り ---
add("shi", "し")
add("chi", "ち")
add("tsu", "つ")
add("fu", "ふ")
add("ji", "じ")

# --- 濁音・半濁音 ---
DAKU = {
    "g": ("ga", "gi", "gu", "ge", "go", "がぎぐげご"),
    "z": ("za", "zi", "zu", "ze", "zo", "ざじずぜぞ"),
    "d": ("da", "di", "du", "de", "do", "だぢづでど"),
    "b": ("ba", "bi", "bu", "be", "bo", "ばびぶべぼ"),
    "p": ("pa", "pi", "pu", "pe", "po", "ぱぴぷぺぽ"),
}
for _, (r1, r2, r3, r4, r5, kana5) in DAKU.items():
    for r, k in zip((r1, r2, r3, r4, r5), kana5):
        add(r, k)

# --- 拗音 ---
# 拗音は「い段の子音」+「ゃゅょ」。かなは2文字(4バイト)。
YOUON_BASE = {
    "ky": "き", "sh": "し", "sy": "し", "ch": "ち", "ty": "ち",
    "ny": "に", "hy": "ひ", "my": "み", "ry": "り",
    "gy": "ぎ", "j": "じ", "zy": "じ", "by": "び", "py": "ぴ",
}
SUTEGANA = {"a": "ゃ", "u": "ゅ", "o": "ょ"}

# ローマ字プレフィックスと基底かなの対応(prefix, base_kana)
YOUON_PREFIX = [
    ("ky", "き"),
    ("sh", "し"),
    ("sy", "し"),
    ("ch", "ち"),
    ("ty", "ち"),
    ("ny", "に"),
    ("hy", "ひ"),
    ("my", "み"),
    ("ry", "り"),
    ("gy", "ぎ"),
    ("j", "じ"),
    ("zy", "じ"),
    ("by", "び"),
    ("py", "ぴ"),
]
for prefix, base in YOUON_PREFIX:
    for vowel, sute in (("a", "ゃ"), ("u", "ゅ"), ("o", "ょ")):
        add(prefix + vowel, base + sute)

# --- 撥音 ---
add("nn", "ん")
add("n'", "ん")

# --- 小書き単独 ---
KOGAKI = {
    "l": ("la", "li", "lu", "le", "lo"),
    "x": ("xa", "xi", "xu", "xe", "xo"),
}
KOGAKI_KANA = "ぁぃぅぇぉ"
for _, romajis in KOGAKI.items():
    for r, k in zip(romajis, KOGAKI_KANA):
        add(r, k)

# --- 記号 ---
add("-", "ー")
add(",", "、")
add(".", "。")

# 重複除去(同じローマ字が複数回定義されていないかチェック)
seen = {}
for r, k in ENTRIES:
    if r in seen and seen[r] != k:
        print(f"ERROR: duplicate romaji '{r}' with different kana: "
              f"'{seen[r]}' vs '{k}'", file=sys.stderr)
        sys.exit(1)
    seen[r] = k

# 重複エントリ(同一ローマ字・同一かな)を除去して安定ソート
unique = list({r: (r, k) for r, k in ENTRIES}.values())
unique.sort(key=lambda rk: rk[0])

ENTRIES = unique


def gen_inc():
    lines = []
    lines.append("; ROMAJI.INC")
    lines.append("; このファイルは tools/gen_romaji.py の生成物。手で編集しない。")
    lines.append("; ローマ字->ひらがな変換テーブル(nasm %include 用)")
    lines.append(";")
    lines.append("; レコード形式(すべて db):")
    lines.append(";   db romlen, kanalen")
    lines.append(";   db <ローマ字 romlen バイト>")
    lines.append(";   db <かなの Shift_JIS(CP932) kanalen バイト>")
    lines.append("; テーブル終端は db 0, 0")
    lines.append("")
    lines.append("romaji_table:")
    for romaji, kana in ENTRIES:
        rom_bytes = romaji.encode("ascii")
        kana_bytes = kana.encode("cp932")
        # nasmはA-Fで始まる16進数リテラルを識別子と誤認するため先頭に0を付ける
        rom_hex = ", ".join(f"0{b:02X}h" for b in rom_bytes)
        kana_hex = ", ".join(f"0{b:02X}h" for b in kana_bytes)
        lines.append(f"    db {len(rom_bytes)}, {len(kana_bytes)}")
        lines.append(f"    db {rom_hex}\t; \"{romaji}\"")
        lines.append(f"    db {kana_hex}\t; \"{kana}\"")
    lines.append("    db 0, 0")
    lines.append("")
    return "\n".join(lines)


def main():
    text = gen_inc()
    OUT_INC.write_text(text, encoding="utf-8")

    max_romlen = max(len(r) for r, _ in ENTRIES)
    print(f"entries: {len(ENTRIES)}")
    print(f"max romaji length: {max_romlen}")


if __name__ == "__main__":
    main()
