#!/usr/bin/env python3
"""FEPL.DIC(v2, 疎索引形式)を「疎索引を二分探索→ブロック内線形走査」で
引く Python 参照実装。

ASM 側の検索は、これと同じ手順で書く:
  1. 疎索引(u32の配列。各要素はそのブロックの先頭エントリのデータ部
     絶対オフセット)を二分探索し、「先頭エントリの読み <= 目的の読み」
     である最後のブロックを探す。
     (= 先頭エントリの読み > 目的の読み となる最初のブロックの
        ひとつ手前のブロック。該当ブロックが無ければ見つからない)
  2. そのブロックの先頭エントリから、読みが目的の読みを超えるまで
     最大 SPARSE 件を線形に見る(ブロックの終端はエントリ総数か
     次のブロックの先頭で打ち切る)。
  3. 読みが一致したら候補リストを返す。読みが目的を追い越したら
     (または総エントリ数に達したら)見つからない。

ファイル形式は tools/mkdic2.py のモジュールdocstringを参照。
"""
import struct
import sys
from pathlib import Path

MAGIC = b'FEPDIC02'
HEADER_LEN = 0x20


class Dic2Error(Exception):
    pass


class Dic2:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < HEADER_LEN:
            raise Dic2Error('ファイルが短すぎてヘッダが読めない')
        magic = data[0:8]
        if magic != MAGIC:
            raise Dic2Error(f'マジックが一致しない: {magic!r}')
        (self.count, self.sparse_count, self.sparse_off,
         self.data_off, self.data_len, self.sparse) = struct.unpack(
            '<IIIIIH', data[8:8 + 4 + 4 + 4 + 4 + 4 + 2])
        # +0x1E は予約(u16)。読み捨てる。
        need = self.sparse_off + self.sparse_count * 4
        if len(data) < need:
            raise Dic2Error('疎索引がファイル長を超えている')
        if len(data) < self.data_off + self.data_len:
            raise Dic2Error('データ部がファイル長を超えている')

    def _sparse_entry_off(self, block: int) -> int:
        off = self.sparse_off + block * 4
        return struct.unpack('<I', self.data[off:off + 4])[0]

    def _read_entry_at(self, abs_off: int):
        """データ部の絶対オフセット abs_off にあるエントリを読み、
        (よみ(str), 候補リスト, 次エントリの絶対オフセット) を返す。"""
        pos = abs_off
        yomi_len = self.data[pos]
        pos += 1
        yomi_b = self.data[pos:pos + yomi_len]
        pos += yomi_len
        yomi = yomi_b.decode('cp932')

        n = self.data[pos]
        pos += 1
        cands = []
        for _ in range(n):
            length = self.data[pos]
            pos += 1
            b = self.data[pos:pos + length]
            pos += length
            cands.append(b.decode('cp932'))
        return yomi, cands, pos

    def _block_first_entry_off(self, block: int) -> int:
        return self._sparse_entry_off(block)

    def all_yomis_sorted(self):
        """全エントリの読みをデータ部の並び順(=読みの昇順であるべき)で返す。"""
        result = []
        pos = self.data_off
        for _ in range(self.count):
            yomi, _cands, pos = self._read_entry_at(pos)
            result.append(yomi)
        return result

    def index_is_sorted(self) -> bool:
        """疎索引の各ブロック先頭エントリの読みが、ブロック番号順に
        昇順(bytes比較)になっているかを確認する。"""
        prev = None
        for block in range(self.sparse_count):
            off = self._sparse_entry_off(block)
            yomi, _cands, _next = self._read_entry_at(off)
            key = yomi.encode('cp932')
            if prev is not None and key < prev:
                return False
            prev = key
        return True

    def lookup(self, yomi: str):
        """よみ(str)を引く。見つかれば候補リスト(順序どおり)、無ければ None。

        手順:
          1. 疎索引を二分探索し、「先頭エントリの読み <= target」である
             最後のブロックを探す。
          2. そのブロックの先頭から、読みが target を超えるまで最大
             SPARSE 件を線形に見る。
          3. 一致したら候補を返す。超えたら(または全体の終端に達したら)
             見つからない。
        """
        target = yomi.encode('cp932')
        if self.count == 0:
            return None

        # 1. 疎索引を二分探索: 先頭エントリの読み <= target である
        #    最後のブロックを探す。
        lo, hi = 0, self.sparse_count - 1
        best_block = None
        while lo <= hi:
            mid = (lo + hi) // 2
            off = self._sparse_entry_off(mid)
            first_yomi, _cands, _next = self._read_entry_at(off)
            key = first_yomi.encode('cp932')
            if key <= target:
                best_block = mid
                lo = mid + 1
            else:
                hi = mid - 1

        if best_block is None:
            return None

        # 2. ブロック内を線形走査(最大 SPARSE 件)
        start_entry = best_block * self.sparse
        end_entry = min(start_entry + self.sparse, self.count)
        pos = self._block_first_entry_off(best_block)
        for _ in range(start_entry, end_entry):
            entry_yomi, cands, next_pos = self._read_entry_at(pos)
            key = entry_yomi.encode('cp932')
            if key == target:
                return cands
            if key > target:
                return None
            pos = next_pos
        return None


def load(path) -> Dic2:
    data = Path(path).read_bytes()
    return Dic2(data)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(f'usage: {sys.argv[0]} <FEPL.DIC> <yomi>')
    dic = load(sys.argv[1])
    result = dic.lookup(sys.argv[2])
    if result is None:
        print('not found')
    else:
        print('/'.join(result))
