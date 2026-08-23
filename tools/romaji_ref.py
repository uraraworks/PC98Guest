#!/usr/bin/env python3
"""ローマ字->かな変換アルゴリズムの Python 参照実装。

tools/gen_romaji.py が作るテーブル(ENTRIES)を使う。ASM 側はこの実装と
同じ挙動になるように書く(次のセッションで実装する)ので、ここでアルゴリズムを
厳密に決めるのが目的。

基本アルゴリズム(状態は未確定バッファ buf のみ):
  1. buf += c
  2. buf がテーブルのエントリと完全一致 -> そのかなを確定出力、buf を空にして終了
  3. buf がどれかのエントリの先頭一致(prefix) -> 何も出さず終了(続きを待つ)
  4. どちらでもない場合:
     a. len(buf) >= 2 かつ buf[0] == 'n' -> 「ん」を確定出力、buf = buf[1:] にして 2 へ
     b. len(buf) >= 2 かつ buf[0] == buf[1] かつ buf[0] が子音(aiueo, n 以外の英小文字)
        -> 「っ」を確定出力、buf = buf[1:] にして 2 へ
     c. それ以外 -> buf[0] をそのまま素通しで出力、buf = buf[1:] にして 2 へ

確定(Enter)時: buf が 'n' 一文字なら「ん」を出力してから確定する。
それ以外の残りは素通しで出す。

大文字は小文字に落としてから処理する。

--- 実装上の補足(重要): "nn" の特別扱い ---

上の基本アルゴリズムを文字通り実装すると、"nn" がテーブルの完全一致エントリ
(撥音の別綴り)であるせいで、buf="nn" になった瞬間に問答無用で「ん」が
確定してしまう(ステップ2がステップ4より先に評価されるため)。

これは実際に2つの要求テストと衝突する:
  - "kanna"(k,a,n,n,a) は「かんな」を期待する。
    buf="nn" を即確定すると na が食われず、残る "a" が単独で「あ」になり
    「かんあ」になってしまう。
  - "konnnichiha"(k,o,n,n,n,i,...) は「こんにちは」を期待する。
    buf="nn" を確定せず1文字ずつ ん を出す(ルール4aを2回)方式にすると、
    3個目の n の直前で「ん」が2回出てしまい「こんんに...」になる。

つまり buf="nn" の時点では、3文字目が母音かどうかを見るまで
"nn"を1つの「ん」として確定すべきか、1文字目だけ「ん」として離して
2文字目を次の母音と組ませるべきか、決定できない。標準的なローマ字入力の
実際の挙動(みんな->みんな、こんにちは->こんにちは が両立する)を再現するには
1文字先読みが必要で、これは「状態はbufだけ」という制約の範囲内で
(bufが伸びた3文字目を見てから判断する形で)実現できる。

そのため、この実装では buf の先頭2文字が "nn" になったら:
  - まだ3文字目が来ていなければ(buf=="nn") 何も確定せず待つ
    (テーブルの完全一致より優先してこの待ちを行う)
  - 3文字目 X が来たら:
      - X が母音(aiueo)なら: 1文字目の n だけ「ん」として確定し、
        buf を "n"+X (2文字目のnとX)に戻して再評価する
        (na/ni/nu/ne/no のいずれかとして確定するはず)
      - X が母音でなければ: "nn" を撥音の別綴りとして丸ごと「ん」に確定し、
        buf は3文字目以降(X を含む)に戻して再評価する
確定(Enter)時に buf が "nn" で止まっている場合も同様に「ん」として確定する
(この点は元のアルゴリズム記述の「buf が 'n' 一文字なら」を "nn" の場合にも
拡張する形の補足)。

"n'"(アポストロフィ)は曖昧さがない(母音と紛れない)ので、この特別扱いは
適用せず、通常通りテーブルの完全一致で即座に確定する。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_romaji  # noqa: E402

# ローマ字 -> かな の辞書
TABLE = {r: k for r, k in gen_romaji.ENTRIES}
# 全エントリのローマ字文字列の集合(prefix判定に使う)
ROMAJI_SET = set(TABLE.keys())

VOWELS = set("aiueo")


def is_consonant(ch):
    return ch.isalpha() and ch not in VOWELS and ch != "n"


def is_prefix_of_some_entry(s):
    if s == "":
        return False
    for r in ROMAJI_SET:
        if r.startswith(s):
            return True
    return False


class RomajiConverter:
    """1文字ずつ feed() して、確定したかな/文字を output に積むコンバータ。"""

    def __init__(self):
        self.buf = ""
        self.output = []

    def feed(self, c):
        c = c.lower()
        self.buf += c
        self._resolve()

    def _resolve(self):
        while self.buf:
            buf = self.buf

            # --- "nn" の特別扱い(モジュールdocstring参照) ---
            if len(buf) >= 2 and buf[0] == "n" and buf[1] == "n":
                if len(buf) == 2:
                    return  # 3文字目が来るまで待つ
                third = buf[2]
                if third in VOWELS:
                    # 1文字目のnだけ「ん」に確定し、2文字目のn+母音を再評価
                    self.output.append("ん")
                    self.buf = buf[1:]
                    continue
                else:
                    # "nn" を丸ごと撥音の「ん」に確定
                    self.output.append("ん")
                    self.buf = buf[2:]
                    continue

            if buf in TABLE:
                self.output.append(TABLE[buf])
                self.buf = ""
                return
            if is_prefix_of_some_entry(buf):
                return  # 続きを待つ
            # 完全一致でも prefix でもない
            if len(buf) >= 2 and buf[0] == "n":
                self.output.append("ん")
                self.buf = buf[1:]
                continue
            if len(buf) >= 2 and buf[0] == buf[1] and is_consonant(buf[0]):
                self.output.append("っ")
                self.buf = buf[1:]
                continue
            # 素通し
            self.output.append(buf[0])
            self.buf = buf[1:]
        # buf が空になったらループ終了

    def finalize(self):
        """Enter確定時の処理。残った buf を処理して output に積む。"""
        while self.buf:
            buf = self.buf
            if buf[0] == "n" and buf[1:2] == "n":
                # 確定待ちで止まっていた "nn" もここで「ん」に確定する
                self.output.append("ん")
                self.buf = buf[2:]
                continue
            if buf == "n":
                self.output.append("ん")
                self.buf = ""
                continue
            # それ以外の残りは素通し
            for ch in buf:
                self.output.append(ch)
            self.buf = ""

    def result(self):
        return "".join(self.output)


def convert(text, finalize=True):
    """text(ローマ字)をかなに変換して文字列で返すヘルパ。"""
    conv = RomajiConverter()
    for c in text:
        conv.feed(c)
    if finalize:
        conv.finalize()
    return conv.result()


if __name__ == "__main__":
    import sys as _sys
    for line in _sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        print(convert(line))
