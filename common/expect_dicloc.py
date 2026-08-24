#!/usr/bin/env python3
"""DICLOC.COM のホスト側期待値。

out/esctest.xdf を DICLOC.ASM と同じ手順で解析し、同じ書式で5行を出す
(画面の実測出力と1文字ずつ突き合わせるため)。

esctest.xdf はTHDのようなヘッダを持たないベタイメージ(1024バイト/セクタ)
なので、論理セクタ N のバイトオフセットは単純に N * bps。

使い方: python3 tools/expect_dicloc.py
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent
IMAGE = FEP_DIR / 'out' / 'esctest.xdf'

TARGET_NAME = b'TSUKUSHIDIC'  # 8.3の生の形(11バイト、TSUKUSHIが8文字なのでスペース埋め不要)


def sector(data: bytes, bps: int, lba: int, count: int) -> bytes:
    off = lba * bps
    return data[off:off + count * bps]


def main():
    data = IMAGE.read_bytes()

    # --- ブートセクタ(論理セクタ0) ---
    boot = data[0:1024]  # BPBはbps未知の段階でも先頭1024バイト以内にある
    bps = struct.unpack('<H', boot[0x0B:0x0D])[0]
    spc = boot[0x0D]
    rsvd = struct.unpack('<H', boot[0x0E:0x10])[0]
    nfat = boot[0x10]
    rootent = struct.unpack('<H', boot[0x11:0x13])[0]
    totsec = struct.unpack('<H', boot[0x13:0x15])[0]
    spf = struct.unpack('<H', boot[0x16:0x18])[0]
    spt = struct.unpack('<H', boot[0x18:0x1A])[0]
    heads = struct.unpack('<H', boot[0x1A:0x1C])[0]

    print(f'[bpb] bps={bps} spc={spc} rsvd={rsvd} nfat={nfat} '
          f'rootent={rootent} spf={spf} spt={spt} heads={heads}')

    # --- 位置計算 ---
    fat_start = rsvd
    root_start = fat_start + nfat * spf
    root_sects = (rootent * 32 + bps - 1) // bps
    data_start = root_start + root_sects

    print(f'[calc] fat={fat_start} root={root_start} rootsec={root_sects} data={data_start}')

    # --- FAT全体 ---
    fat = sector(data, bps, fat_start, spf)

    def fat12_entry(cluster: int) -> int:
        off = cluster * 3 // 2
        raw = fat[off] | (fat[off + 1] << 8)
        if cluster % 2 == 0:
            return raw & 0x0FFF
        return raw >> 4

    # --- ルートディレクトリ全体から "TSUKUSHIDIC" を探す ---
    root = sector(data, bps, root_start, root_sects)
    file_clus = None
    file_size = None
    for i in range(rootent):
        entry = root[i * 32:(i + 1) * 32]
        if entry[0] == 0x00:
            break
        if entry[0] == 0xE5:
            continue
        if entry[0:11] == TARGET_NAME:
            file_clus = struct.unpack('<H', entry[26:28])[0]
            file_size = struct.unpack('<I', entry[28:32])[0]
            break

    if file_clus is None:
        print('error: TSUKUSHI.DIC not found in root directory', file=sys.stderr)
        return 1

    print(f'[file] clus={file_clus} size={file_size}')

    # --- FAT12チェーンを辿り、連続する区間ごとにまとめる(TSUKUSHI.ASM の
    # 段階Eと同じ考え方。区間は出現順)。従来の contiguous=yes/no は
    # 区間数1個かどうかと同じ意味になる ---
    def clus_to_lba(clus: int) -> int:
        return data_start + (clus - 2) * spc

    runs = []  # [{clus_start, clus_count}]
    count = 1
    contiguous = True
    cur = file_clus
    run_start_clus = file_clus
    run_len_clus = 1
    safety = 8192
    while True:
        nxt = fat12_entry(cur)
        if nxt >= 0x0FF8:
            runs.append({'clus_start': run_start_clus, 'clus_count': run_len_clus})
            break
        if nxt != cur + 1:
            contiguous = False
            runs.append({'clus_start': run_start_clus, 'clus_count': run_len_clus})
            run_start_clus = nxt
            run_len_clus = 1
        else:
            run_len_clus += 1
        count += 1
        cur = nxt
        safety -= 1
        if safety <= 0:
            print('error: FAT chain too long (possible loop)', file=sys.stderr)
            return 1

    print(f'[chain] count={count} contiguous={"yes" if contiguous else "no"}')

    # --- 開始LBA・セクタ数(先頭区間。従来互換の表示) ---
    lba = clus_to_lba(file_clus)
    sectors = count * spc

    print(f'[loc] lba={lba} sectors={sectors}')

    # --- 区間リスト(段階E: 断片化した辞書向け) ---
    print(f'[runs] n={len(runs)}')
    for i, r in enumerate(runs):
        run_lba = clus_to_lba(r['clus_start'])
        run_sectors = r['clus_count'] * spc
        print(f'  run {i}: clus={r["clus_start"]} clus_count={r["clus_count"]} '
              f'lba={run_lba} sectors={run_sectors}')

    return 0


if __name__ == '__main__':
    sys.exit(main())
