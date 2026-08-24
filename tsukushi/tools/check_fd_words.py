#!/usr/bin/env python3
"""FD用辞書(SKK-JISYO.MLから生成)に、読みの長さで切り捨てられていた
頻出語が収録されているかを確認する簡易チェックスクリプト。

使い方:
  python3 tsukushi/tools/check_fd_words.py [TSUKUSHI.DIC]
既定の DIC パスは out/release/tsukushi-v<最新>.xdf 内ではなく、
mkdic2.py で直接生成したファイルを指定して使う想定。
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import dic2_ref  # noqa: E402

WORDS = [
    'ありがとう', 'とうきょう', 'にゅうりょく', 'だいじょうぶ',
    'こんにちは', 'かんじ', 'がっこう', 'にほんご',
]


def main():
    if len(sys.argv) != 2:
        sys.exit(f'usage: {sys.argv[0]} <TSUKUSHI.DIC>')
    dic = dic2_ref.load(sys.argv[1])
    ok = True
    for w in WORDS:
        result = dic.lookup(w)
        status = 'OK' if result is not None else 'NG'
        if result is None:
            ok = False
        cands = '/'.join(result) if result else '(not found)'
        print(f'  [{status}] {w} -> {cands}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
