#!/usr/bin/env python3
"""FEP.DIC を二分探索で引く Python 参照実装。

ASM(DICTEST.ASM)側の二分探索はこれと同じ結果になるように書く。
ファイル形式は tools/mkdic.py のモジュールdocstringを参照。
"""
import struct
import sys
from pathlib import Path

MAGIC = b'FEPDIC01'
YOMI_FIELD_LEN = 16
INDEX_ENTRY_LEN = 20
HEADER_LEN = 16


class DicError(Exception):
    pass


class Dic:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < HEADER_LEN:
            raise DicError('ファイルが短すぎてヘッダが読めない')
        magic = data[0:8]
        if magic != MAGIC:
            raise DicError(f'マジックが一致しない: {magic!r}')
        self.count = struct.unpack('<H', data[8:10])[0]
        self.index_off = struct.unpack('<H', data[10:12])[0]
        need = self.index_off + self.count * INDEX_ENTRY_LEN
        if len(data) < need:
            raise DicError('索引がファイル長を超えている')

    def _entry(self, i):
        off = self.index_off + i * INDEX_ENTRY_LEN
        field = self.data[off:off + YOMI_FIELD_LEN]
        cand_off = struct.unpack('<I', self.data[off + YOMI_FIELD_LEN:off + INDEX_ENTRY_LEN])[0]
        return field, cand_off

    def _read_candidates(self, cand_off):
        n = self.data[cand_off]
        pos = cand_off + 1
        cands = []
        for _ in range(n):
            length = self.data[pos]
            pos += 1
            b = self.data[pos:pos + length]
            pos += length
            cands.append(b.decode('cp932'))
        return cands

    def index_is_sorted(self):
        prev = None
        for i in range(self.count):
            field, _ = self._entry(i)
            if prev is not None and field < prev:
                return False
            prev = field
        return True

    def lookup(self, yomi: str):
        """よみ(str)を引く。見つかれば候補リスト(順序どおり)、無ければ None。"""
        target = yomi.encode('cp932')
        if len(target) > YOMI_FIELD_LEN:
            return None
        target = target + b'\x00' * (YOMI_FIELD_LEN - len(target))

        lo, hi = 0, self.count - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            field, cand_off = self._entry(mid)
            if field == target:
                return self._read_candidates(cand_off)
            elif field < target:
                lo = mid + 1
            else:
                hi = mid - 1
        return None


def load(path) -> Dic:
    data = Path(path).read_bytes()
    return Dic(data)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(f'usage: {sys.argv[0]} <FEP.DIC> <yomi>')
    dic = load(sys.argv[1])
    result = dic.lookup(sys.argv[2])
    if result is None:
        print('not found')
    else:
        print('/'.join(result))
