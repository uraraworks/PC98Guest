#!/usr/bin/env python3
"""out/esctest.xdf の指定LBAの先頭32バイトを16進で出す。

RAWRD.COM の実機/エミュレータ実測値とホスト側の期待値を突き合わせるための
小さなツール。esctest.xdf はTHDのようなヘッダを持たないベタイメージ
(1024バイト/セクタ)なので、オフセットは単純に lba * 1024。

使い方:
  python3 tools/expect_sector.py <LBA>
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent
IMAGE = FEP_DIR / 'out' / 'esctest.xdf'

BYTES_PER_SECTOR = 1024
DUMP_LEN = 32


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <LBA>", file=sys.stderr)
        return 1
    lba = int(sys.argv[1])
    data = IMAGE.read_bytes()
    off = lba * BYTES_PER_SECTOR
    chunk = data[off:off + DUMP_LEN]
    print(' '.join(f'{b:02X}' for b in chunk))
    return 0


if __name__ == '__main__':
    sys.exit(main())
