#!/usr/bin/env python3
"""空の PC-98 SASI HDD イメージ(.thd)を作る。

FreeDOS(98) を HDD にインストールする実験のための入れ物。
中身は空なので、ゲスト側で BTNPART.EXE(領域確保) → SYS(システム転送) を行う。

ジオメトリは NP2kai の .thd の扱いに合わせて固定:
  8 ヘッド × 33 セクタ × 256 バイト/セクタ（= 1 シリンダ 67584 バイト）
NP2kai は THD ヘッダ 256 バイトの先頭ワードだけをシリンダ数として読み、
ほかは無視する（NP2kai/fdd/sxsihdd.c）。

使い方:
  python3 common/make_blank_hdd.py [--cylinders N] [--out PATH]
"""
import argparse
import struct
from pathlib import Path

HEADER = 256
HEADS = 8
SECTORS = 33
SECTOR_SIZE = 256
CYL_BYTES = HEADS * SECTORS * SECTOR_SIZE

REPO_ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--cylinders', type=int, default=512)
    ap.add_argument('--out', default=str(REPO_ROOT / 'out' / 'blankhdd.thd'))
    args = ap.parse_args()

    if not 1 <= args.cylinders <= 0xFFFF:
        raise SystemExit(f'シリンダ数が範囲外: {args.cylinders}')

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    header = bytearray(HEADER)
    struct.pack_into('<H', header, 0, args.cylinders)

    total = args.cylinders * CYL_BYTES
    with out.open('wb') as f:
        f.write(header)
        # まとめて書くとメモリを食うので 1 シリンダずつ
        blank = bytes(CYL_BYTES)
        for _ in range(args.cylinders):
            f.write(blank)

    size = HEADER + total
    print(f'{out}: {size} バイト '
          f'({args.cylinders} シリンダ × {HEADS} ヘッド × {SECTORS} セクタ '
          f'× {SECTOR_SIZE} バイト = {total / 1024 / 1024:.1f} MB)')
    print(f'物理セクタ数: {args.cylinders * HEADS * SECTORS}')


if __name__ == '__main__':
    main()
