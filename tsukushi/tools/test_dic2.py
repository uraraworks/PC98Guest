#!/usr/bin/env python3
"""FEPL.DIC(v2) 生成物のテスト。

dic/base.txt から v2 を作って:
  - 全エントリが引けること(順序も一致)
  - 存在しないよみが「見つからない」になること
  - 索引(疎索引の各ブロック先頭)が昇順であること
  - 陽性対照: 疎索引の2レコードを入れ替えたコピーで、二分探索が
    少なくとも1件を見落とすことを確認する

dic/upstream/SKK-JISYO.L があれば、さらに:
  - v2 を作り、決め打ちの200語(先頭100語・末尾100語、SKK-JISYO.L の
    okuri-nasi 区間かつかな限定フィルタ通過後の並び)を引いて元データと
    一致することを確認する
  - 全エントリの読みが昇順に並んでいることを確認する
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent           # tsukushi/
REPO_ROOT = FEP_DIR.parent       # リポジトリルート
BASE_TXT = FEP_DIR / 'dic' / 'base.txt'
UPSTREAM_L = FEP_DIR / 'dic' / 'upstream' / 'SKK-JISYO.L'
OUT_BASE2 = REPO_ROOT / 'out' / 'TESTBASE2.DIC'
OUT_L2 = REPO_ROOT / 'out' / 'TESTL2.DIC'

sys.path.insert(0, str(HERE))
import dic2_ref  # noqa: E402
import mkdic2  # noqa: E402

failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)
        print(f'NG: {msg}')


def run_mkdic2(src, out, extra_args=()):
    subprocess.run(
        [sys.executable, str(HERE / 'mkdic2.py'), '--src', str(src), '--out', str(out), *extra_args],
        check=True)


def load_expected_base():
    out = []
    for line in BASE_TXT.read_text(encoding='utf-8').splitlines():
        line = line.strip('\n')
        if not line:
            continue
        yomi, cands = line.split(' ', 1)
        assert cands.startswith('/') and cands.endswith('/')
        parts = cands[1:-1].split('/')
        out.append((yomi, parts))
    return out


def test_base():
    print('== dic/base.txt 由来のテスト ==')
    run_mkdic2(BASE_TXT, OUT_BASE2)
    check(OUT_BASE2.exists(), 'FEPL.DIC(base) が生成されなかった')

    dic = dic2_ref.load(OUT_BASE2)
    expected = load_expected_base()

    check(dic.count == len(expected),
          f'エントリ数が一致しない: dic={dic.count} expected={len(expected)}')

    # 1. 全エントリの往復確認(順序も含む)
    for yomi, cands in expected:
        got = dic.lookup(yomi)
        check(got is not None, f'{yomi!r} が見つからない')
        check(got == cands, f'{yomi!r} の候補が不一致: got={got} expected={cands}')

    # 2. 存在しないよみ
    check(dic.lookup('ぬぬぬぬぬ') is None, '"ぬぬぬぬぬ" が見つかってしまった')
    check(dic.lookup('') is None, '空文字が見つかってしまった')

    # 3. 索引(疎索引ブロック先頭)が昇順
    check(dic.index_is_sorted(), '疎索引が昇順になっていない')

    # データ部全体も昇順であること(base.txt は疎索引1ブロックしか
    # 無い可能性が高いので、こちらも別途確認する)
    yomis = dic.all_yomis_sorted()
    sorted_keys = sorted(y.encode('cp932') for y in yomis)
    check([y.encode('cp932') for y in yomis] == sorted_keys,
          'データ部の読みが昇順になっていない')

    # 4. 陽性対照: 疎索引の2レコードを入れ替えて壊す
    #    (base.txt は51件しか無く既定 --sparse 64 だと疎索引が1レコードに
    #    なってしまうため、専用に --sparse を小さくして組み直す)
    positive_control(BASE_TXT, expected)


def positive_control(src, expected):
    """疎索引レコードが2未満だと入れ替えが組めないので、
    --sparse を小さくして確実に複数レコードにしたコピーで陽性対照を取る。"""
    out = REPO_ROOT / 'out' / 'TESTBASE2_SPARSE.DIC'
    run_mkdic2(src, out, extra_args=['--sparse', '4'])
    dic = dic2_ref.load(out)
    check(dic.sparse_count >= 2, f'--sparse 4 でも疎索引が2レコード未満: {dic.sparse_count}')
    if dic.sparse_count < 2:
        return

    check(dic.index_is_sorted(), '(sparse=4) 疎索引が昇順になっていない')

    data = bytearray(out.read_bytes())
    i, j = 0, dic.sparse_count - 1
    off_i = dic.sparse_off + i * 4
    off_j = dic.sparse_off + j * 4
    rec_i = bytes(data[off_i:off_i + 4])
    rec_j = bytes(data[off_j:off_j + 4])
    data[off_i:off_i + 4] = rec_j
    data[off_j:off_j + 4] = rec_i

    broken = dic2_ref.Dic2(bytes(data))
    check(not broken.index_is_sorted(), '入れ替えた疎索引が(たまたま)昇順のままになっている')

    misses = 0
    for yomi, cands in expected:
        got = broken.lookup(yomi)
        if got != cands:
            misses += 1
    check(misses >= 1,
          '疎索引を入れ替えても二分探索が全件を見つけてしまった(検出器が機能していない)')
    print(f'陽性対照(sparse=4): 入れ替え後に見つからなくなった件数 = {misses} / {len(expected)}')


def cp932_roundtrip(s: str) -> str:
    """CP932 に一度エンコードしてからデコードし直した文字列を返す。
    辞書はCP932バイト列でしか候補を保持しないため、Unicode上は別の
    コードポイントでもCP932エンコードが同じバイト列になる文字
    (例: U+301C波ダッシュ と U+FF5E全角チルダ はどちらも 0x8160 に
    エンコードされ、デコードすると常に U+FF5E に化ける)は、
    生成物を経由すると見た目が変わる。これは実装のバグではなく
    CP932往復の性質なので、期待値の側もこの変換を通してから比較する。"""
    return s.encode('cp932').decode('cp932')


def load_skk_nasi_entries():
    """SKK-JISYO.L の okuri-nasi 区間を、mkdic2.py と同じロジックで
    (よみ, 候補リスト) に変換したものを返す(かなフィルタ適用済み、
    重複は先勝ち、元の出現順)。CP932往復による表記の変化も反映済み
    (cp932_roundtrip 参照)。"""
    entries = mkdic2.load_entries(UPSTREAM_L)
    out = []
    for yomi, cands in entries:
        try:
            yomi_rt = cp932_roundtrip(yomi)
        except UnicodeEncodeError:
            continue  # mkdic2.build_records も同様にエンコード不能なら捨てる
        cands_rt = []
        for c in cands:
            try:
                cands_rt.append(cp932_roundtrip(c))
            except UnicodeEncodeError:
                continue
        if not cands_rt:
            continue
        out.append((yomi_rt, cands_rt))
    return out


def test_upstream():
    if not UPSTREAM_L.exists():
        print(f'{UPSTREAM_L} が無いので SKK-JISYO.L 由来のテストは省略')
        return

    print('== dic/upstream/SKK-JISYO.L 由来のテスト ==')
    run_mkdic2(UPSTREAM_L, OUT_L2)
    check(OUT_L2.exists(), 'FEPL.DIC(L) が生成されなかった')

    dic = dic2_ref.load(OUT_L2)
    entries = load_skk_nasi_entries()
    check(dic.count == len(entries),
          f'エントリ数が一致しない: dic={dic.count} expected={len(entries)}')

    sample = entries[:100] + entries[-100:]
    check(len(sample) == min(200, len(entries)),
          f'決め打ちサンプルの件数がおかしい: {len(sample)}')

    for yomi, cands in sample:
        got = dic.lookup(yomi)
        check(got is not None, f'(SKK-JISYO.L) {yomi!r} が見つからない')
        check(got == cands, f'(SKK-JISYO.L) {yomi!r} の候補が不一致: got={got} expected={cands}')

    yomis = dic.all_yomis_sorted()
    keys = [y.encode('cp932') for y in yomis]
    check(keys == sorted(keys), '(SKK-JISYO.L) データ部の読みが昇順になっていない')
    check(dic.index_is_sorted(), '(SKK-JISYO.L) 疎索引が昇順になっていない')

    print(f'OK(SKK-JISYO.L): {dic.count} entries, {OUT_L2.stat().st_size} bytes, '
          f'決め打ち{len(sample)}語一致確認済み')


def main():
    test_base()
    test_upstream()

    if failures:
        print(f'\n{len(failures)} 件失敗')
        sys.exit(1)
    print('\nOK: all tests passed')


if __name__ == '__main__':
    main()
