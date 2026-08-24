#!/usr/bin/env python3
"""ひらがな -> カタカナ 変換規則(TSUKUSHI.ASM kata_convert)の検算。

ぁ(U+3041)〜ん(U+3093) の全83文字について、TSUKUSHI.ASM に実装した規則
(0x829F<=sjis<=0x82DD なら+0xA1、0x82DE<=sjis<=0x82F1 なら+0xA2)で計算した
Shift_JIS が、Python の chr(ord(h)+0x60).encode('cp932') と一致することを
確認する。
"""
import sys


def kata_convert_rule(sjis_value: int) -> int:
    """TSUKUSHI.ASM kata_convert と同じ規則。"""
    if 0x829F <= sjis_value <= 0x82DD:
        return sjis_value + 0xA1
    elif 0x82DE <= sjis_value <= 0x82F1:
        return sjis_value + 0xA2
    return sjis_value


def main() -> int:
    failures = 0
    checked = 0
    for code in range(0x3041, 0x3093 + 1):  # ぁ..ん
        h = chr(code)
        try:
            h_sjis_bytes = h.encode("cp932")
        except UnicodeEncodeError:
            print(f"skip U+{code:04X} {h!r}: not encodable in cp932")
            continue
        h_sjis = (h_sjis_bytes[0] << 8) | h_sjis_bytes[1]

        expected_kata = chr(ord(h) + 0x60).encode("cp932")
        expected_value = (expected_kata[0] << 8) | expected_kata[1]

        got_value = kata_convert_rule(h_sjis)
        checked += 1
        if got_value != expected_value:
            failures += 1
            print(
                f"MISMATCH {h!r} sjis={h_sjis:04X} "
                f"rule->{got_value:04X} expected->{expected_value:04X}"
            )

    print(f"checked: {checked}")
    print(f"failures: {failures}")
    if failures == 0 and checked == 83:
        print("ALL TESTS PASSED")
        return 0
    print("FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
