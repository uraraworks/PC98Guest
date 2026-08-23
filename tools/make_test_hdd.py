#!/usr/bin/env python3
"""NEC MS-DOS 6.20 HDD イメージ(THD)から測定用の作業コピーを作る。

入力: WebNP2/public/test/HDDimage.thd (読むだけ。絶対に書き換えない)
出力: out/msdos620.thd

改変:
  1. AUTOEXEC.BAT の中身を "@ECHO OFF\r\n" に差し替える(FDが起動しないように)
  2. ROOT_FILES に列挙した各 .COM をルートディレクトリに追加(既存なら上書き)

FAT16 のレイアウトパラメータは WebPaint98/tools/extract_hdd.py の
Fat16Image と同じ考え方(BPBから読む。ハードコードしない)。

実行するたびに入力から出力へコピーし直してから改変する。
"""
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FEP_DIR = HERE.parent
SRC_THD = FEP_DIR.parent.parent / 'WebNP2' / 'public' / 'test' / 'HDDimage.thd'
OUT_DIR = FEP_DIR / 'out'
OUT_THD = OUT_DIR / 'msdos620.thd'

# ルートディレクトリへ収録するファイル一覧(FEP_DIR からの相対名で ROOT_FILES に足す)
ROOT_FILES = [
    FEP_DIR / 'ESCT1.COM',
    FEP_DIR / 'ESCT2.COM',
]

THD_HEADER = 0x100
CYL_BYTES = 8 * 33 * 256  # heads * spt * bytes/sector

AUTOEXEC_NEW = b'@ECHO OFF\r\n'


class Fat16Writer:
    """FAT16 パーティションの読み書き用(bytearray上で操作)。"""

    def __init__(self, data: bytearray):
        self.data = data
        self.part = THD_HEADER + 1 * CYL_BYTES  # partition at cylinder 1
        b = bytes(self.data[self.part:self.part + 64])
        self.bps = struct.unpack('<H', b[11:13])[0]
        self.spc = b[13]
        self.rsvd = struct.unpack('<H', b[14:16])[0]
        self.nfat = b[16]
        self.rootent = struct.unpack('<H', b[17:19])[0]
        self.spf = struct.unpack('<H', b[22:24])[0]

        self.fat_off = [self.part + (self.rsvd + i * self.spf) * self.bps
                         for i in range(self.nfat)]
        self.root_off = self.part + (self.rsvd + self.nfat * self.spf) * self.bps
        self.data_off = self.root_off + self.rootent * 32
        self.clsize = self.bps * self.spc
        # クラスタ数(FATの有効エントリ範囲の上限を知るため)
        total_fat_bytes = self.spf * self.bps
        self.max_entries = total_fat_bytes // 2

    # --- FAT read/write (両方のFATコピーに同じ内容を書く) ---
    def fat_get(self, cluster):
        off = self.fat_off[0] + cluster * 2
        return struct.unpack('<H', self.data[off:off + 2])[0]

    def fat_set(self, cluster, value):
        for base in self.fat_off:
            off = base + cluster * 2
            self.data[off:off + 2] = struct.pack('<H', value)

    def find_free_cluster(self):
        # クラスタ0,1は予約。2から順に走査。
        for c in range(2, self.max_entries):
            if self.fat_get(c) == 0:
                return c
        raise RuntimeError('空きクラスタが見つからない')

    def cluster_data_off(self, cluster):
        return self.data_off + (cluster - 2) * self.clsize

    def chain(self, c):
        out = []
        while 2 <= c < 0xFFF0:
            out.append(c)
            c = self.fat_get(c)
        return out

    # --- root dir ---
    def root_entries_raw(self):
        return [self.root_off + i * 32 for i in range(self.rootent)]

    def find_root_entry(self, name83):
        """name83: 8+3 の固定長バイト列(スペース詰め)。見つかればエントリのオフセット、なければNone。"""
        for off in self.root_entries_raw():
            e = self.data[off:off + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5:
                continue
            if bytes(e[0:11]) == name83:
                return off
        return None

    def find_free_root_slot(self):
        for off in self.root_entries_raw():
            e = self.data[off]
            if e == 0x00 or e == 0xE5:
                return off
        raise RuntimeError('ルートディレクトリに空きスロットがない')


def to_83(name: str) -> bytes:
    base, _, ext = name.partition('.')
    base = base.upper().ljust(8)[:8]
    ext = ext.upper().ljust(3)[:3]
    return (base + ext).encode('ascii')


def write_file_to_root(fs: Fat16Writer, name: str, content: bytes,
                        date=0x5000, time=0x0000):
    """既存の同名エントリがあれば再利用、なければ新規追加してファイルを書き込む。"""
    name83 = to_83(name)
    off = fs.find_root_entry(name83)
    is_new = off is None
    if is_new:
        off = fs.find_free_root_slot()

    n_clusters_needed = max(1, (len(content) + fs.clsize - 1) // fs.clsize)

    if not is_new:
        cur_cluster = struct.unpack('<H', fs.data[off + 26:off + 28])[0]
        existing_chain = fs.chain(cur_cluster) if cur_cluster >= 2 else []
    else:
        existing_chain = []

    if existing_chain and len(existing_chain) >= n_clusters_needed:
        # 既存チェーンを使い、余分なクラスタはFATから解放する
        chain = existing_chain[:n_clusters_needed]
        for extra in existing_chain[n_clusters_needed:]:
            fs.fat_set(extra, 0)
    else:
        # 既存チェーンを解放してから新規に確保し直す(単純化のため)
        for c in existing_chain:
            fs.fat_set(c, 0)
        chain = []
        for _ in range(n_clusters_needed):
            c = fs.find_free_cluster()
            fs.fat_set(c, 0xFFFF)  # 仮に確保済みマーク(EOC)。あとでリンクを張り直す
            chain.append(c)

    # チェーンをリンクし、最後のクラスタをEOC(0xFFFF)にする
    for i, c in enumerate(chain):
        nxt = chain[i + 1] if i + 1 < len(chain) else 0xFFFF
        fs.fat_set(c, nxt)

    # データを書き込み(最終クラスタの余りは0埋め)
    remaining = content
    for c in chain:
        doff = fs.cluster_data_off(c)
        chunk = remaining[:fs.clsize]
        remaining = remaining[fs.clsize:]
        fs.data[doff:doff + len(chunk)] = chunk
        if len(chunk) < fs.clsize:
            fs.data[doff + len(chunk):doff + fs.clsize] = b'\x00' * (fs.clsize - len(chunk))

    # ディレクトリエントリを書く
    entry = bytearray(32)
    entry[0:11] = name83
    entry[11] = 0x20  # ARCHIVE
    entry[12] = 0
    entry[13] = 0
    struct.pack_into('<H', entry, 14, time)  # crt time
    struct.pack_into('<H', entry, 16, date)  # crt date
    struct.pack_into('<H', entry, 18, date)  # last access date
    struct.pack_into('<H', entry, 20, 0)     # ea/hi cluster (FAT16では未使用)
    struct.pack_into('<H', entry, 22, time)  # write time
    struct.pack_into('<H', entry, 24, date)  # write date
    struct.pack_into('<H', entry, 26, chain[0])
    struct.pack_into('<I', entry, 28, len(content))
    fs.data[off:off + 32] = entry


def rewrite_file_inplace_same_cluster(fs: Fat16Writer, name: str, content: bytes):
    """1クラスタに収まる新内容で、既存ファイルの先頭クラスタへ上書きする(FATチェーン変更なし)。"""
    name83 = to_83(name)
    off = fs.find_root_entry(name83)
    if off is None:
        raise RuntimeError(f'{name} が見つからない')
    if len(content) > fs.clsize:
        raise RuntimeError('この関数は1クラスタに収まる内容専用')
    cluster = struct.unpack('<H', fs.data[off + 26:off + 28])[0]
    doff = fs.cluster_data_off(cluster)
    fs.data[doff:doff + len(content)] = content
    if len(content) < fs.clsize:
        fs.data[doff + len(content):doff + fs.clsize] = b'\x00' * (fs.clsize - len(content))
    struct.pack_into('<I', fs.data, off + 28, len(content))


def build():
    if not SRC_THD.exists():
        sys.exit(f'入力が見つからない: {SRC_THD}')
    for f in ROOT_FILES:
        if not f.exists():
            sys.exit(f'収録対象が見つからない: {f}')

    src_bytes = SRC_THD.read_bytes()
    data = bytearray(src_bytes)  # 常に入力から作り直す

    fs = Fat16Writer(data)

    # 改変1: AUTOEXEC.BAT
    rewrite_file_inplace_same_cluster(fs, 'AUTOEXEC.BAT', AUTOEXEC_NEW)

    # 改変2: ROOT_FILES の各ファイルをルートに収録
    for f in ROOT_FILES:
        write_file_to_root(fs, f.name, f.read_bytes())

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    OUT_THD.write_bytes(bytes(data))
    print(f'書き出し完了: {OUT_THD} ({OUT_THD.stat().st_size} bytes)')

    # 入力が変わっていないことをその場で確認
    if SRC_THD.read_bytes() != src_bytes:
        sys.exit('入力ファイルが変化してしまった!')


if __name__ == '__main__':
    build()
