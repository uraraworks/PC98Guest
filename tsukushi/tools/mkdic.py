#!/usr/bin/env python3
"""dic/base.txt を読み、FEP.DIC(かな漢字変換 辞書ファイル)を生成する。

入力形式(dic/base.txt, UTF-8): 1行 = "よみ /候補/候補/.../"
  - よみと候補列の間は半角スペース1つ
  - 候補列は先頭と末尾がスラッシュで、間をスラッシュ区切り

出力(out/FEP.DIC, リトルエンディアン, すべて Shift_JIS(CP932)):
  +0000  8 bytes  マジック "FEPDIC01"
  +0008  u16      エントリ数
  +000A  u16      索引の開始オフセット (= 16)
  +000C  u32      予約(0)
  +0010  索引: エントリ数 × 20 バイト
           +00  16 bytes  よみ(Shift_JIS。余りは 0x00 で埋める。最大 8 かな)
           +10  u32       候補ブロックの絶対ファイルオフセット
  索引の後ろに候補ブロックを並べる:
           u8    候補数
           候補数回: u8 バイト長, その長さの Shift_JIS バイト列

索引は「よみ」のバイト列で昇順にソートする(16バイトに0埋めしたあとの
bytes比較 = memcmp と同じ順序)。よみが16バイトを超えるエントリは
エラーで止める(切り詰めない)。

生成のたびに変わる値(日時など)は出力に含めない。
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent
SRC_TXT = FEP_DIR / 'dic' / 'base.txt'
OUT_DIC = FEP_DIR / 'out' / 'FEP.DIC'

MAGIC = b'FEPDIC01'
YOMI_FIELD_LEN = 16
INDEX_ENTRY_LEN = 20  # 16 (yomi) + 4 (u32 offset)
HEADER_LEN = 16


def parse_line(line, lineno):
    line = line.rstrip('\n').rstrip('\r')
    if not line:
        return None
    if ' ' not in line:
        sys.exit(f'{SRC_TXT}:{lineno}: よみと候補の区切り(半角スペース)が無い: {line!r}')
    yomi, cands = line.split(' ', 1)
    if not (cands.startswith('/') and cands.endswith('/')):
        sys.exit(f'{SRC_TXT}:{lineno}: 候補列は / で始まり / で終わる必要がある: {cands!r}')
    inner = cands[1:-1]
    parts = inner.split('/')
    if not parts or any(p == '' for p in parts):
        sys.exit(f'{SRC_TXT}:{lineno}: 空の候補がある: {line!r}')
    return yomi, parts


def load_entries():
    if not SRC_TXT.exists():
        sys.exit(f'入力が見つからない: {SRC_TXT}')
    entries = []
    seen = set()
    with SRC_TXT.open('r', encoding='utf-8') as f:
        for lineno, line in enumerate(f, 1):
            parsed = parse_line(line, lineno)
            if parsed is None:
                continue
            yomi, cands = parsed
            if yomi in seen:
                sys.exit(f'{SRC_TXT}:{lineno}: よみが重複している: {yomi!r}')
            seen.add(yomi)
            entries.append((yomi, cands))
    if not entries:
        sys.exit(f'{SRC_TXT}: エントリが1件も無い')
    return entries


def encode_yomi_field(yomi):
    b = yomi.encode('cp932')
    if len(b) > YOMI_FIELD_LEN:
        sys.exit(f'よみが{YOMI_FIELD_LEN}バイトを超えている(切り詰めない仕様): {yomi!r} ({len(b)} bytes)')
    return b + b'\x00' * (YOMI_FIELD_LEN - len(b))


def build():
    entries = load_entries()

    # (16バイト固定長よみフィールド, 候補リスト) に変換してからソートする。
    # ソート鍵は 0 埋め後のバイト列そのもの(memcmp相当)。
    records = []
    for yomi, cands in entries:
        field = encode_yomi_field(yomi)
        cand_bytes = [c.encode('cp932') for c in cands]
        for cb in cand_bytes:
            if len(cb) > 255:
                sys.exit(f'候補が255バイトを超えている: {cb!r}')
        if len(cand_bytes) > 255:
            sys.exit(f'候補数が255を超えている: {yomi!r}')
        records.append((field, cand_bytes))

    records.sort(key=lambda r: r[0])

    count = len(records)
    index_off = HEADER_LEN
    index_len = count * INDEX_ENTRY_LEN
    cand_area_start = index_off + index_len

    # 候補ブロックを組み立て、各エントリの絶対オフセットを確定する。
    cand_blob = bytearray()
    cand_offsets = []
    for field, cand_bytes in records:
        cand_offsets.append(cand_area_start + len(cand_blob))
        cand_blob.append(len(cand_bytes))
        for cb in cand_bytes:
            cand_blob.append(len(cb))
            cand_blob.extend(cb)

    out = bytearray()
    out += MAGIC
    out += struct.pack('<H', count)
    out += struct.pack('<H', index_off)
    out += struct.pack('<I', 0)
    assert len(out) == HEADER_LEN

    for (field, _), off in zip(records, cand_offsets):
        out += field
        out += struct.pack('<I', off)

    assert len(out) == cand_area_start
    out += cand_blob

    OUT_DIC.parent.mkdir(parents=True, exist_ok=True)
    OUT_DIC.write_bytes(bytes(out))
    print(f'{OUT_DIC}: {count} entries, {len(out)} bytes')


if __name__ == '__main__':
    build()
