#!/usr/bin/env python3
"""out/datahdd.thd の指定物理セクタ(線形, 256バイト単位)の先頭32バイトを16進で出す。

HDDRD.COM の実機/エミュレータ実測値とホスト側の期待値を突き合わせるための
小さなツール。物理セクタはディスク先頭(THDヘッダの直後、シリンダ0セクタ0)
からの線形番号(0起点)で、256バイト単位。THDヘッダ256バイトを飛ばすのを
忘れないこと。

使い方:
  python3 common/expect_hdd_sector.py <物理セクタ番号>
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
IMAGE = REPO_ROOT / 'out' / 'datahdd.thd'

THD_HEADER = 0x100
PHYS_SECTOR = 256
DUMP_LEN = 32


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <physical sector>", file=sys.stderr)
        return 1
    sector = int(sys.argv[1])
    data = IMAGE.read_bytes()
    off = THD_HEADER + sector * PHYS_SECTOR
    chunk = data[off:off + DUMP_LEN]
    print(' '.join(f'{b:02X}' for b in chunk))
    return 0


if __name__ == '__main__':
    sys.exit(main())
