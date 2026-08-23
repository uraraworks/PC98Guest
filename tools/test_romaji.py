#!/usr/bin/env python3
"""tools/romaji_ref.py の変換アルゴリズムのテスト。

python3 tools/test_romaji.py で実行。失敗したら非ゼロ終了する。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_romaji  # noqa: E402
import romaji_ref  # noqa: E402

failures = []


def check(label, got, expected):
    if got != expected:
        failures.append(f"{label}: got {got!r}, expected {expected!r}")


# --- 表の意味を使った基本ケース ---
check("aiueo", romaji_ref.convert("aiueo"), "あいうえお")
check("kakikukeko", romaji_ref.convert("kakikukeko"), "かきくけこ")
check("sasisuseso", romaji_ref.convert("sasisuseso"), "さしすせそ")
check("shi", romaji_ref.convert("shi"), "し")
check("tsu", romaji_ref.convert("tsu"), "つ")
check("chi", romaji_ref.convert("chi"), "ち")
check("nihon+確定", romaji_ref.convert("nihon", finalize=True), "にほん")
check("nn", romaji_ref.convert("nn"), "ん")
check("kanna", romaji_ref.convert("kanna"), "かんな")
check("konnnichiha+確定", romaji_ref.convert("konnnichiha", finalize=True), "こんにちは")
check("matte", romaji_ref.convert("matte"), "まって")
check("kitte", romaji_ref.convert("kitte"), "きって")
check("kyou", romaji_ref.convert("kyou"), "きょう")
check("syashin+確定", romaji_ref.convert("syashin", finalize=True), "しゃしん")
check("ja", romaji_ref.convert("ja"), "じゃ")
check("ra-men+確定", romaji_ref.convert("ra-men", finalize=True), "らーめん")
check("abc1", romaji_ref.convert("abc1"), "あbc1")

# 大文字は小文字に落として処理されること
check("uppercase KA", romaji_ref.convert("KA"), "か")
check("uppercase KANNA", romaji_ref.convert("KaNNa"), "かんな")

# --- 全エントリの総当たり: そのローマ字を1文字ずつ入力したら
#     対応するかなが出ること ---
entry_failures = 0
for romaji, kana in gen_romaji.ENTRIES:
    conv = romaji_ref.RomajiConverter()
    for c in romaji:
        conv.feed(c)
    conv.finalize()
    got = conv.result()
    if got != kana:
        entry_failures += 1
        failures.append(
            f"entry '{romaji}': got {got!r}, expected {kana!r}"
        )

# --- cp932 往復一致 ---
cp932_failures = 0
for romaji, kana in gen_romaji.ENTRIES:
    encoded = kana.encode("cp932")
    decoded = encoded.decode("cp932")
    if decoded != kana:
        cp932_failures += 1
        failures.append(
            f"cp932 roundtrip '{romaji}' -> {kana!r}: got {decoded!r}"
        )

# 変換結果全体(テスト文字列)も cp932 往復一致すること
for label, text, expected in (
    ("aiueo", "aiueo", "あいうえお"),
    ("konnnichiha", "konnnichiha", "こんにちは"),
    ("kanna", "kanna", "かんな"),
):
    result = romaji_ref.convert(text)
    encoded = result.encode("cp932")
    decoded = encoded.decode("cp932")
    if decoded != result:
        failures.append(
            f"cp932 roundtrip of convert('{text}')={result!r}: got {decoded!r}"
        )

print(f"entries checked: {len(gen_romaji.ENTRIES)}")
print(f"entry roundtrip failures: {entry_failures}")
print(f"cp932 roundtrip failures: {cp932_failures}")

if failures:
    print(f"\nFAILED: {len(failures)} failure(s)")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)
else:
    print("\nALL TESTS PASSED")
    sys.exit(0)
