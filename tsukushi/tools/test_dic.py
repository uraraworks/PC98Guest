#!/usr/bin/env python3
"""FEP.DIC 生成物のテスト。

- dic/base.txt の全エントリが、生成した FEP.DIC を dic_ref.py で引くと
  元の候補リストが順序どおり返ること
- 存在しないよみを引くと None(見つからない)が返ること
- 索引が本当に昇順になっていること
- 陽性対照: 索引の2レコードを入れ替えたコピーを作り、二分探索が
  少なくとも1件を見つけられなくなること(常に見つかる壊れた検索器
  でないことの確認)
"""
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent
SRC_TXT = FEP_DIR / 'dic' / 'base.txt'
OUT_DIC = FEP_DIR / 'out' / 'FEP.DIC'

sys.path.insert(0, str(HERE))
import dic_ref  # noqa: E402
import mkdic  # noqa: E402

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
        print(f'NG: {msg}')


def load_expected():
    """dic/base.txt を独自にパースして (よみ, [候補,...]) のリストを返す
    (mkdic.py のパーサとは別経路で検算するため、あえて簡易に再実装する)。"""
    out = []
    for line in SRC_TXT.read_text(encoding='utf-8').splitlines():
        line = line.strip('\n')
        if not line:
            continue
        yomi, cands = line.split(' ', 1)
        assert cands.startswith('/') and cands.endswith('/')
        parts = cands[1:-1].split('/')
        out.append((yomi, parts))
    return out


def main():
    # 1. 生成
    subprocess.run([sys.executable, str(HERE / 'mkdic.py')], check=True)
    check(OUT_DIC.exists(), 'FEP.DIC が生成されなかった')

    dic = dic_ref.load(OUT_DIC)
    expected = load_expected()

    check(dic.count == len(expected),
          f'エントリ数が一致しない: dic={dic.count} expected={len(expected)}')

    # 2. 全エントリの往復確認
    for yomi, cands in expected:
        got = dic.lookup(yomi)
        check(got is not None, f'{yomi!r} が見つからない')
        check(got == cands, f'{yomi!r} の候補が不一致: got={got} expected={cands}')

    # 3. 存在しないよみ
    check(dic.lookup('ぬぬぬ') is None, '"ぬぬぬ" が見つかってしまった')
    check(dic.lookup('') is None, '空文字が見つかってしまった')

    # 4. 索引が昇順
    check(dic.index_is_sorted(), '索引が昇順になっていない')

    # 5. 陽性対照: 索引の2レコードを入れ替えて壊す
    check(dic.count >= 2, 'エントリ数が2未満で陽性対照が組めない')
    data = bytearray(OUT_DIC.read_bytes())
    i, j = 0, dic.count - 1
    off_i = dic.index_off + i * dic_ref.INDEX_ENTRY_LEN
    off_j = dic.index_off + j * dic_ref.INDEX_ENTRY_LEN
    rec_i = bytes(data[off_i:off_i + dic_ref.INDEX_ENTRY_LEN])
    rec_j = bytes(data[off_j:off_j + dic_ref.INDEX_ENTRY_LEN])
    data[off_i:off_i + dic_ref.INDEX_ENTRY_LEN] = rec_j
    data[off_j:off_j + dic_ref.INDEX_ENTRY_LEN] = rec_i

    broken = dic_ref.Dic(bytes(data))
    check(not broken.index_is_sorted(), '入れ替えた索引が(たまたま)昇順のままになっている')

    misses = 0
    for yomi, cands in expected:
        got = broken.lookup(yomi)
        if got != cands:
            misses += 1
    check(misses >= 1,
          '索引を入れ替えても二分探索が全件を見つけてしまった(検出器が機能していない)')
    print(f'陽性対照: 入れ替え後に見つからなくなった件数 = {misses} / {len(expected)}')

    if failures:
        print(f'\n{len(failures)} 件失敗')
        sys.exit(1)
    print(f'OK: {dic.count} entries, {OUT_DIC.stat().st_size} bytes, '
          f'positive-control misses={misses}')


if __name__ == '__main__':
    main()
