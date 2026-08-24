#!/usr/bin/env python3
"""測定用の小さなHDDイメージ(out/datahdd.thd)をゼロから合成するツール。

msdos620.thd(手元の実イメージのコピー、124MB)はページ遷移のたびに
WebNP2がIndexedDBへ永続化しレンダラが数十秒固まるため、測定には
使いたくない。ここではジオメトリとFAT16パラメータだけ実イメージに
合わせた小さなイメージを新規に作る(コピーではなく合成)。

参考にした実測値(WebNP2/public/test/HDDimage.thd, 2026-08-01調査):
  THDヘッダ: 256バイト。先頭ワード = シリンダ数(実イメージは0x0780=1920)
  1シリンダ = 8ヘッド × 33セクタ/トラック × 256バイト/セクタ = 264物理セクタ
  パーティションはシリンダ1から(シリンダ0は使わない)
  BPB: 論理セクタ2048バイト / セクタ・クラスタ2 / 予約1 / FAT2個 /
       ルートエントリ3072 / セクタ/FAT31 / セクタ/トラック33 / ヘッド8 /
       hidden sectors=264(物理セクタ単位でのパーティション開始位置)
       media=0xF8

このイメージはシリンダ数だけ小さくして(128)、BPBの他のパラメータは
実イメージと完全に同じ値のまま使う(比較しやすくするため)。パーティション
サイズが小さくなる分、FATの空き容量に余裕ができるだけで機能上は問題ない。

出力: out/datahdd.thd
収録: out/FEP.DIC, probes/*.COM, tsukushi/TSUKUSHI.COM (すべてルート直下、
      クラスタ連続配置)

実装は WebPaint98/tools/extract_hdd.py (読み取り参照実装) と
common/make_test_hdd.py (FAT16書き込みの考え方)を踏襲している。
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
PROBES = REPO_ROOT / 'probes'
TSUKUSHI = REPO_ROOT / 'tsukushi'
OUT_DIR = REPO_ROOT / 'out'
OUT_THD = OUT_DIR / 'datahdd.thd'
FEP_DIC = OUT_DIR / 'FEP.DIC'

THD_HEADER = 0x100
HEADS = 8
SPT = 33                       # セクタ/トラック(物理)
PHYS_SECTOR = 256              # 物理セクタサイズ(バイト)
CYL_BYTES = HEADS * SPT * PHYS_SECTOR  # 67584

N_CYLINDERS = 128              # ヘッダに書くシリンダ数(≒8.6MB)
PART_START_CYL = 1             # パーティションはシリンダ1から(実イメージと同じ)

# --- BPB パラメータ(実イメージと同じ値) ---
BPS = 2048                     # 論理セクタサイズ
SPC = 2                        # セクタ/クラスタ
RSVD = 1                       # 予約セクタ数
NFAT = 2                       # FATの個数
ROOTENT = 3072                 # ルートディレクトリエントリ数
MEDIA = 0xF8
SPF = 31                       # セクタ/FAT
HIDDEN = HEADS * SPT           # = 264。パーティション開始位置(物理セクタ単位)

CLSIZE = BPS * SPC             # クラスタサイズ(バイト) = 4096
ROOT_SECTORS = (ROOTENT * 32) // BPS  # 48
DATA_START_LOGICAL = RSVD + NFAT * SPF + ROOT_SECTORS  # データ領域開始(パーティション内論理セクタ, 0起点)


def part_total_logical_sectors():
    part_phys_sectors = (N_CYLINDERS - PART_START_CYL) * HEADS * SPT
    total_bytes = part_phys_sectors * PHYS_SECTOR
    if total_bytes % BPS != 0:
        sys.exit('パーティションの物理バイト数が論理セクタ境界に合わない')
    return total_bytes // BPS


def max_clusters():
    total_logical = part_total_logical_sectors()
    data_sectors = total_logical - DATA_START_LOGICAL
    return data_sectors // SPC


def to_83(name: str) -> bytes:
    base, _, ext = name.partition('.')
    base = base.upper().ljust(8)[:8]
    ext = ext.upper().ljust(3)[:3]
    return (base + ext).encode('ascii')


def physical_sector_of_logical(logical_sector: int) -> int:
    """パーティション内論理セクタ(0起点) -> ディスク先頭からの物理セクタ(線形, 0起点)。"""
    assert BPS % PHYS_SECTOR == 0
    return HIDDEN + logical_sector * (BPS // PHYS_SECTOR)


def build():
    if not FEP_DIC.exists():
        sys.exit(
            f'収録対象が見つからない: {FEP_DIC}\n'
            '先に python3 tsukushi/tools/mkdic2.py --out out/FEP.DIC を実行すること'
        )

    files = [FEP_DIC]
    files += sorted(PROBES.glob('*.COM'))
    files.append(TSUKUSHI / 'TSUKUSHI.COM')

    for f in files:
        if not f.exists():
            sys.exit(f'収録対象が見つからない: {f}')

    total_size = THD_HEADER + N_CYLINDERS * CYL_BYTES
    data = bytearray(total_size)

    # --- THDヘッダ: 先頭ワード = シリンダ数 ---
    struct.pack_into('<H', data, 0, N_CYLINDERS)

    part = THD_HEADER + PART_START_CYL * CYL_BYTES
    total_logical = part_total_logical_sectors()
    if total_logical > 0xFFFF:
        sys.exit('総論理セクタ数が16bitに収まらない(N_CYLINDERSを減らすこと)')

    # --- BPB(ブートセクタ) ---
    bs = bytearray(BPS)
    bs[0] = 0xEB
    bs[1] = 0x45
    bs[2] = 0x90
    bs[3:11] = b'NEC  6.2'
    struct.pack_into('<H', bs, 11, BPS)
    bs[13] = SPC
    struct.pack_into('<H', bs, 14, RSVD)
    bs[16] = NFAT
    struct.pack_into('<H', bs, 17, ROOTENT)
    struct.pack_into('<H', bs, 19, total_logical)   # totsec16
    bs[21] = MEDIA
    struct.pack_into('<H', bs, 22, SPF)
    struct.pack_into('<H', bs, 24, SPT)
    struct.pack_into('<H', bs, 26, HEADS)
    struct.pack_into('<I', bs, 28, HIDDEN)
    struct.pack_into('<I', bs, 32, 0)                # totsec32(totsec16を使うので0)
    bs[510] = 0x55
    bs[511] = 0xAA
    data[part:part + BPS] = bs

    # --- FAT領域(2セクタ目以降。両コピーとも同じ内容にする) ---
    fat_off = [part + (RSVD + i * SPF) * BPS for i in range(NFAT)]
    n_max = max_clusters()

    def fat_get(cluster):
        off = fat_off[0] + cluster * 2
        return struct.unpack('<H', data[off:off + 2])[0]

    def fat_set(cluster, value):
        for base in fat_off:
            off = base + cluster * 2
            data[off:off + 2] = struct.pack('<H', value)

    root_off = part + (RSVD + NFAT * SPF) * BPS
    data_off = part + DATA_START_LOGICAL * BPS

    def cluster_data_off(cluster):
        return data_off + (cluster - 2) * CLSIZE

    # --- ファイルを連続クラスタで詰めていく ---
    next_free = 2
    report = []
    for f in files:
        content = f.read_bytes()
        n_clusters = max(1, (len(content) + CLSIZE - 1) // CLSIZE)
        if next_free + n_clusters - 1 - 2 >= n_max:
            sys.exit(f'空き容量不足: {f.name} を配置できない(データ領域が小さすぎる)')
        start_cluster = next_free
        chain = list(range(start_cluster, start_cluster + n_clusters))
        for i, c in enumerate(chain):
            nxt = chain[i + 1] if i + 1 < len(chain) else 0xFFFF
            fat_set(c, nxt)
        # データ書き込み(最終クラスタの余りは0埋め)
        remaining = content
        for c in chain:
            doff = cluster_data_off(c)
            chunk = remaining[:CLSIZE]
            remaining = remaining[CLSIZE:]
            data[doff:doff + len(chunk)] = chunk
            if len(chunk) < CLSIZE:
                data[doff + len(chunk):doff + CLSIZE] = b'\x00' * (CLSIZE - len(chunk))

        # ルートディレクトリエントリ
        entry = bytearray(32)
        entry[0:11] = to_83(f.name)
        entry[11] = 0x20  # ARCHIVE
        struct.pack_into('<H', entry, 14, 0)       # crt time
        struct.pack_into('<H', entry, 16, 0x5000)  # crt date
        struct.pack_into('<H', entry, 18, 0x5000)  # access date
        struct.pack_into('<H', entry, 20, 0)
        struct.pack_into('<H', entry, 22, 0)       # write time
        struct.pack_into('<H', entry, 24, 0x5000)  # write date
        struct.pack_into('<H', entry, 26, start_cluster)
        struct.pack_into('<I', entry, 28, len(content))
        entry_off = root_off + len(report) * 32
        data[entry_off:entry_off + 32] = entry

        start_logical = DATA_START_LOGICAL + (start_cluster - 2) * SPC
        start_phys = physical_sector_of_logical(start_logical)
        report.append((f.name, start_cluster, start_logical, start_phys, len(content)))

        next_free += n_clusters

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_THD.write_bytes(bytes(data))

    print(f'書き出し完了: {OUT_THD} ({OUT_THD.stat().st_size} bytes, '
          f'{N_CYLINDERS}シリンダ, hidden={HIDDEN})')
    print(f'{"file":14s} {"cluster":>7s} {"logical":>8s} {"physical":>9s} {"bytes":>8s}')
    for name, cluster, logical, phys, size in report:
        print(f'{name:14s} {cluster:7d} {logical:8d} {phys:9d} {size:8d}')


if __name__ == '__main__':
    build()
