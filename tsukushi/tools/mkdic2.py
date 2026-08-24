#!/usr/bin/env python3
"""SKK-JISYO 形式(または dic/base.txt 形式)を読み、
TSUKUSHI.DIC(v2, 疎索引形式のかな漢字変換辞書)を生成する。

v1(tools/mkdic.py, FEPDIC01)は索引が「よみ16B固定長+オフセット4B」の
固定長索引で、SKK-JISYO.L 全体だと索引だけで数MBになってしまう。
v2 では索引にオフセットだけを持たせ(疎索引)、実際のよみはデータ部から
読んで比較する。

出力(既定 out/TSUKUSHI.DIC, リトルエンディアン, すべて Shift_JIS(CP932)):
  +0000  8 bytes  マジック "FEPDIC02"
  +0008  u32      エントリ総数
  +000C  u32      疎索引のレコード数
  +0010  u32      疎索引の先頭オフセット (= 0x20)
  +0014  u32      データ部の先頭オフセット
  +0018  u32      データ部のバイト長
  +001C  u16      疎索引の間隔 SPARSE
  +001E  u16      予約(0)
  +0020  疎索引: レコード数 × u32
             各レコードは「そのブロックの先頭エントリのデータ部オフセット
             (ファイル先頭からの絶対値)」。ブロック i は
             エントリ [i*SPARSE, min((i+1)*SPARSE, count)) を含む。
         データ部: エントリを読みの昇順に並べたもの。各エントリは
             u8  読みのバイト長
             読みバイト列(CP932)
             u8  候補数
             候補数回: u8 バイト長, バイト列(CP932)

索引には読みを持たせない(検索側はデータ部の読みを読んで比較する。
これは索引に読みを持たせると切り詰めによる曖昧さが生まれるのを
避けるため)。

読みの比較は常にバイト列の memcmp 順(短いほうが小さい)。この順で
ソートする。

入力形式の判定:
  - ファイル名が "SKK-JISYO" で始まる、または内容に
    ";; okuri-nasi entries." という行が含まれる場合は SKK 形式
    (EUC-JP)とみなす。";; okuri-ari entries." から
    ";; okuri-nasi entries." までは送りありエントリなので使わない。
    ";;" で始まる行はコメント。行は "<よみ> /候補/候補/" の形式で、
    候補の ";" 以降は注釈なので捨てる。
  - それ以外は dic/base.txt と同じ形式(UTF-8, 1行 = "よみ /候補/候補/")
    とみなす。

いずれの形式でも、採用する読みは「ひらがな(ぁ-ゖ と ー)だけ」に限る。
CP932 にエンコードできない候補は個別に捨てる。候補が1つも残らない
エントリは捨てる。

生成のたびに変わる値(日時など)は出力に含めない。
"""
import argparse
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent           # tsukushi/
REPO_ROOT = FEP_DIR.parent       # リポジトリルート
DEFAULT_SRC = FEP_DIR / 'dic' / 'upstream' / 'SKK-JISYO.L'
DEFAULT_OUT = REPO_ROOT / 'out' / 'TSUKUSHI.DIC'

MAGIC = b'FEPDIC02'
HEADER_LEN = 0x20
DEFAULT_SPARSE = 64

SKK_OKURI_ARI_MARK = ';; okuri-ari entries.'
SKK_OKURI_NASI_MARK = ';; okuri-nasi entries.'


def is_kana_only(yomi: str) -> bool:
    if not yomi:
        return False
    for ch in yomi:
        if not ('ぁ' <= ch <= 'ゖ' or ch == 'ー'):
            return False
    return True


def split_cands(inner: str):
    """"cand1/cand2/..." (先頭末尾の / は既に剥がされている) を分解し、
    各候補の ";" 以降(注釈)を捨てたリストを返す。空になった候補は捨てる。"""
    cands = []
    for part in inner.split('/'):
        cand = part.split(';', 1)[0]
        if cand:
            cands.append(cand)
    return cands


def parse_entry_line(line: str):
    """"よみ /候補/候補/" 形式の1行を (よみ, [候補,...]) に分解する。
    形式に合わなければ None を返す(呼び出し側で読み飛ばす)。"""
    if ' ' not in line:
        return None
    yomi, cands = line.split(' ', 1)
    if not (cands.startswith('/') and cands.endswith('/')):
        return None
    inner = cands[1:-1]
    if not inner:
        return None
    parsed_cands = split_cands(inner)
    if not parsed_cands:
        return None
    return yomi, parsed_cands


def detect_is_skk(raw: bytes, path: Path) -> bool:
    if path.name.upper().startswith('SKK-JISYO'):
        return True
    return SKK_OKURI_NASI_MARK.encode('ascii') in raw


def iter_skk_lines(raw: bytes):
    text = raw.decode('euc-jp')
    lines = text.split('\n')
    in_nasi = False
    for line in lines:
        line = line.rstrip('\r')
        if not line:
            continue
        if line.startswith(SKK_OKURI_ARI_MARK):
            in_nasi = False
            continue
        if line.startswith(SKK_OKURI_NASI_MARK):
            in_nasi = True
            continue
        if line.startswith(';;'):
            continue
        if not in_nasi:
            # okuri-ari 区間(または okuri-nasi マーカーより前)は使わない
            continue
        yield line


def iter_plain_lines(raw: bytes, path: Path):
    text = raw.decode('utf-8')
    for line in text.split('\n'):
        line = line.rstrip('\r')
        if not line:
            continue
        yield line


def load_entries(src_path: Path):
    if not src_path.exists():
        sys.exit(f'入力辞書が見つからない: {src_path}\n'
                  f'SKK-JISYO.L を {src_path} に置いてください'
                  f'(EUC-JP, GPL v2+, git 管理外)。')
    raw = src_path.read_bytes()
    is_skk = detect_is_skk(raw, src_path)
    line_iter = iter_skk_lines(raw) if is_skk else iter_plain_lines(raw, src_path)

    entries = {}  # yomi(str) -> cands(list[str])  (後勝ちにせず先勝ちで重複解決)
    order_hint = []
    for line in line_iter:
        parsed = parse_entry_line(line)
        if parsed is None:
            continue
        yomi, cands = parsed
        if not is_kana_only(yomi):
            continue
        if yomi not in entries:
            entries[yomi] = cands
            order_hint.append(yomi)
    if not entries:
        sys.exit(f'{src_path}: 採用できるエントリが1件も無い')
    return [(y, entries[y]) for y in order_hint]


def encode_cp932_or_none(s: str):
    try:
        return s.encode('cp932')
    except UnicodeEncodeError:
        return None


def build_records(entries, max_yomi_kana, max_cands):
    """(yomi_bytes, [cand_bytes,...]) のリストを、読みバイト列昇順で返す。"""
    records = []
    for yomi, cands in entries:
        if max_yomi_kana is not None and len(yomi) > max_yomi_kana:
            continue
        yomi_b = encode_cp932_or_none(yomi)
        if yomi_b is None:
            continue
        if len(yomi_b) > 255:
            continue

        cand_list = cands if max_cands is None else cands[:max_cands]
        cand_bytes = []
        for c in cand_list:
            cb = encode_cp932_or_none(c)
            if cb is None:
                continue
            if len(cb) > 255:
                continue
            cand_bytes.append(cb)
        if not cand_bytes:
            continue
        if len(cand_bytes) > 255:
            cand_bytes = cand_bytes[:255]

        records.append((yomi_b, cand_bytes))

    records.sort(key=lambda r: r[0])
    return records


def build(src_path: Path, out_path: Path, max_yomi_kana, max_cands, sparse: int):
    entries = load_entries(src_path)
    records = build_records(entries, max_yomi_kana, max_cands)
    if not records:
        sys.exit(f'{src_path}: フィルタ適用後にエントリが1件も残らなかった')

    count = len(records)

    # データ部を組み立てる(読みバイト長昇順ソート済みのrecordsをそのまま並べる)
    data_blob = bytearray()
    data_offsets = []  # 各エントリの「データ部先頭からの相対」オフセット
    for yomi_b, cand_bytes in records:
        data_offsets.append(len(data_blob))
        data_blob.append(len(yomi_b))
        data_blob.extend(yomi_b)
        data_blob.append(len(cand_bytes))
        for cb in cand_bytes:
            data_blob.append(len(cb))
            data_blob.extend(cb)

    sparse_count = (count + sparse - 1) // sparse
    sparse_index_off = HEADER_LEN
    sparse_index_len = sparse_count * 4
    data_off = sparse_index_off + sparse_index_len
    data_len = len(data_blob)

    sparse_records = []
    for i in range(sparse_count):
        first_entry = i * sparse
        sparse_records.append(data_off + data_offsets[first_entry])

    out = bytearray()
    out += MAGIC
    out += struct.pack('<I', count)
    out += struct.pack('<I', sparse_count)
    out += struct.pack('<I', sparse_index_off)
    out += struct.pack('<I', data_off)
    out += struct.pack('<I', data_len)
    out += struct.pack('<H', sparse)
    out += struct.pack('<H', 0)
    assert len(out) == HEADER_LEN

    for off in sparse_records:
        out += struct.pack('<I', off)
    assert len(out) == data_off

    out += data_blob
    assert len(out) == data_off + data_len

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(out))
    print(f'{out_path}: {count} entries, {sparse_count} sparse records, '
          f'{data_len} data bytes, {len(out)} total bytes')
    return count, sparse_count, data_len, len(out)


def main():
    ap = argparse.ArgumentParser(description='SKK-JISYO 形式から TSUKUSHI.DIC(v2)を生成する')
    ap.add_argument('--src', type=Path, default=DEFAULT_SRC)
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT)
    ap.add_argument('--max-yomi-kana', type=int, default=None,
                     help='読みの文字数上限(既定は無制限)')
    ap.add_argument('--max-cands', type=int, default=None,
                     help='候補数の上限(既定は無制限)')
    ap.add_argument('--sparse', type=int, default=DEFAULT_SPARSE,
                     help=f'疎索引の間隔(既定 {DEFAULT_SPARSE})')
    args = ap.parse_args()

    if args.sparse < 1:
        sys.exit('--sparse は1以上でなければならない')

    build(args.src, args.out, args.max_yomi_kana, args.max_cands, args.sparse)


if __name__ == '__main__':
    main()
