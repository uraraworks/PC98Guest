#!/usr/bin/env python3
"""Release用配布物(FDイメージ + zip)を組み立てるスクリプト。

既存の成果物は一切信用せず、毎回ソースから作り直す:
  - TSUKUSHI.COM は毎回 nasm でアセンブルし直す
  - TSUKUSHI.DIC は毎回 mkdic2.py で生成し直す
  - SUMIRE.EXE / TSUBAKI.EXE は毎回 compile.mjs でコンパイルし直す
どれかが失敗したら即座に停止し、途中までの配布物を out/release/ に
残さない。

使い方:
  python3 common/make_release.py --product tsukushi --version 0.1.0
  python3 common/make_release.py --product sumire   --version 0.1.0
  python3 common/make_release.py --product tsubaki  --version 0.1.0
  python3 common/make_release.py --product combined --version 0.1.0

  combined は3本入りのまとめ FD イメージ。中身の版番号ではなく
  まとめ版自身の版番号を --version に渡す。

すみれ・つばきのビルドには WorkbenchNP2 のツールチェーン
(toolchain/compile.mjs)が要る。既定では ../WorkbenchNP2/toolchain/
compile.mjs を見る。別の場所にあるときは --compile-mjs で渡すこと。
"""
import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
TSUKUSHI_DIR = REPO_ROOT / 'tsukushi'
# FD 用: SKK-JISYO.ML (Medium-Large)。読みの長さを切らずに FD(1232KB)へ
# 収めるための語彙量が絞られた辞書。
UPSTREAM_DIC_ML = TSUKUSHI_DIR / 'dic' / 'upstream' / 'SKK-JISYO.ML'
# zip 用: SKK-JISYO.L (Large)。全語彙、容量制限なし。
UPSTREAM_DIC_L = TSUKUSHI_DIR / 'dic' / 'upstream' / 'SKK-JISYO.L'
GPL2_TXT = TSUKUSHI_DIR / 'dic' / 'COPYING-GPL2.txt'
LICENSE_TXT = REPO_ROOT / 'LICENSE'
READ_ME_TEMPLATE = TSUKUSHI_DIR / 'READ.ME.md'
BUILD_DISK_MJS = HERE / 'build-disk.mjs'
MKDIC2_PY = TSUKUSHI_DIR / 'tools' / 'mkdic2.py'
TSUKUSHI_ASM = TSUKUSHI_DIR / 'TSUKUSHI.ASM'
OUT_RELEASE = REPO_ROOT / 'out' / 'release'

SUMIRE_DIR = REPO_ROOT / 'sumire'
TSUBAKI_DIR = REPO_ROOT / 'tsubaki'
SUMIRE_C = SUMIRE_DIR / 'SUMIRE.C'
TSUBAKI_C = TSUBAKI_DIR / 'TSUBAKI.C'
SUMIRE_READ_ME = SUMIRE_DIR / 'READ.ME.md'
TSUBAKI_READ_ME = TSUBAKI_DIR / 'READ.ME.md'
COMBINED_READ_ME = HERE / 'READ.ME.combined.md'
# WorkbenchNP2 のツールチェーン。このリポジトリの外にあるので既定値は
# 「隣に置いてある」前提。--compile-mjs で上書きできる。
DEFAULT_COMPILE_MJS = REPO_ROOT.parent / 'WorkbenchNP2' / 'toolchain' / 'compile.mjs'

MAX_LINE_WIDTH = 80

# 以下は common/build-disk.mjs --release モードの FAT12 レイアウト定数の
# 写し(このスクリプト側で「辞書が収まるか」を build-disk.mjs 実行前に
# 検査するため)。build-disk.mjs は Buffer.copy でイメージバッファに
# 書き込むため、収まらない場合も例外を出さず黙って末尾が切り詰められる。
# それを避けるため、node を呼ぶ前にここで容量を検査して止める。
FD_BYTES_PER_SECTOR = 1024
FD_RESERVED_SECTORS = 1
FD_FAT_COUNT = 2
FD_SECTORS_PER_FAT = 2
FD_ROOT_ENTRIES = 192
FD_TOTAL_SECTORS = 1232


READ_ME_TEMPLATE_USED = []


def die(msg: str) -> None:
    print(f'エラー: {msg}', file=sys.stderr)
    sys.exit(1)


def run(cmd, **kwargs):
    print(f'$ {" ".join(str(c) for c in cmd)}')
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        die(f'コマンドが失敗した(終了コード {result.returncode}): {" ".join(str(c) for c in cmd)}')
    return result


def display_width(line: str) -> int:
    w = 0
    for ch in line:
        ea = unicodedata.east_asian_width(ch)
        w += 2 if ea in ('W', 'F') else 1
    return w


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()



# ---------------------------------------------------------------------------
# すみれ / つばき: C ソースのコンパイル(WorkbenchNP2 のツールチェーン)
# ---------------------------------------------------------------------------

def compile_c(tmp_dir: Path, src: Path, exe_name: str, compile_mjs: Path) -> Path:
    """src を compile.mjs でコンパイルし、生成された .exe を返す。

    compile.mjs は <出力先>.xdf と同じディレクトリへ <元の名前>.exe /
    .asm / .exe.map も書き出す。ここで欲しいのは .exe のほうなので、
    使い捨てのディレクトリへ出させてから .exe を拾う。"""
    if not src.exists():
        die(f'{src} が無い')
    if not compile_mjs.exists():
        die(
            f'ツールチェーンが見つからない: {compile_mjs}\n'
            '  すみれ・つばきのビルドには WorkbenchNP2 の\n'
            '  toolchain/compile.mjs が要ります(このリポジトリには含まれません)。\n'
            '    https://github.com/uraraworks/WorkbenchNP2\n'
            '  別の場所にある場合は --compile-mjs で渡してください。'
        )
    work = tmp_dir / ('build-' + src.stem.lower())
    work.mkdir(parents=True, exist_ok=True)
    out_xdf = work / (src.stem.lower() + '.xdf')
    run(['node', str(compile_mjs), str(src), '-o', str(out_xdf)], cwd=compile_mjs.parent)
    produced = work / (src.stem + '.exe')
    if not produced.exists():
        die(f'compile.mjs はエラーを返さなかったが {produced} が生成されなかった')
    out_exe = tmp_dir / exe_name
    shutil.copyfile(produced, out_exe)
    check_data_segment(work / (src.stem + '.exe.map'), src.name)
    return out_exe


def check_data_segment(map_path: Path, label: str) -> None:
    """.map の __stop__bss が 64KB(スモールモデルのデータ上限)に
    収まっていることを確認する。check.py の見積りは静的な下限なので、
    実際のビルド結果を見るこちらが最終確認になる。"""
    if not map_path.exists():
        die(f'{map_path} が無い(compile.mjs の出力が変わった?)')
    stop = None
    for line in map_path.read_text(encoding='utf-8', errors='replace').split('\n'):
        if '__stop__bss' in line:
            token = line.strip().split()[0]
            stop = int(token, 16)
            break
    if stop is None:
        die(f'{map_path}: __stop__bss が見つからない')
    if stop > 65536:
        die(f'{label}: データセグメントが {stop} バイトで 65536 を超えている')
    print(f'  {label}: データセグメント {stop} / 65536 バイト (OK)')


# ---------------------------------------------------------------------------
# 汎用の FD イメージ組み立て(--file NAME.EXT=パス を並べる)
# ---------------------------------------------------------------------------

def cluster_bytes(size: int) -> int:
    clusters = (size + FD_BYTES_PER_SECTOR - 1) // FD_BYTES_PER_SECTOR
    return clusters * FD_BYTES_PER_SECTOR


def fd_data_bytes() -> int:
    root_sectors = (FD_ROOT_ENTRIES * 32) // FD_BYTES_PER_SECTOR
    data_start = FD_RESERVED_SECTORS + FD_FAT_COUNT * FD_SECTORS_PER_FAT + root_sectors
    return (FD_TOTAL_SECTORS - data_start) * FD_BYTES_PER_SECTOR


def build_fd_image_files(out_name: str, label: str, entries) -> Path:
    """entries: [(DOS名, Path), ...]。指定した順にクラスタを割り当てる。

    build-disk.mjs は収まらない分を黙って切り詰めるので、node を呼ぶ前に
    ここで容量を検査して止める。"""
    total = sum(cluster_bytes(path.stat().st_size) for _name, path in entries)
    capacity = fd_data_bytes()
    if total > capacity:
        die(f'{out_name}: 収録物が FD に収まらない '
            f'(クラスタ単位で {total} バイト / 空き {capacity} バイト)')
    print(f'  FD 容量検査: 収録物 {total} バイト / データ領域 {capacity} バイト (OK)')

    out_xdf = OUT_RELEASE / out_name
    cmd = ['node', str(BUILD_DISK_MJS), '--release', '--label', label,
           '--out', str(out_xdf)]
    for name, path in entries:
        cmd += ['--file', f'{name}={path}']
    run(cmd, cwd=REPO_ROOT)
    if not out_xdf.exists():
        die('build-disk.mjs はエラーを返さなかったが FD イメージが生成されなかった')
    return out_xdf


def check_image_contents(xdf_path: Path, expected) -> None:
    """出来上がったイメージのルートディレクトリを読み直し、狙った
    ファイルが狙ったサイズで入っているかを確認する。作った側の
    正しさは、入っていることの証明にならないため。"""
    data = xdf_path.read_bytes()
    boot = data[0:FD_BYTES_PER_SECTOR]
    bps = struct.unpack('<H', boot[0x0B:0x0D])[0]
    rsvd = struct.unpack('<H', boot[0x0E:0x10])[0]
    nfat = boot[0x10]
    rootent = struct.unpack('<H', boot[0x11:0x13])[0]
    spf = struct.unpack('<H', boot[0x16:0x18])[0]
    root_start = rsvd + nfat * spf
    root_sects = (rootent * 32 + bps - 1) // bps
    root = data[root_start * bps: (root_start + root_sects) * bps]

    found = {}
    for i in range(rootent):
        entry = root[i * 32:(i + 1) * 32]
        if entry[0] == 0x00:
            break
        if entry[0] == 0xE5:
            continue
        name = entry[0:8].decode('ascii').strip()
        ext = entry[8:11].decode('ascii').strip()
        dos = name + ('.' + ext if ext else '')
        found[dos] = struct.unpack('<I', entry[28:32])[0]

    for dos_name, path in expected:
        want = path.stat().st_size
        got = found.get(dos_name)
        if got is None:
            die(f'{xdf_path.name}: {dos_name} がイメージのルートに無い(入っているのは {sorted(found)})')
        if got != want:
            die(f'{xdf_path.name}: {dos_name} のサイズが {got} で、元の {want} と違う')
    if len(found) != len(expected):
        die(f'{xdf_path.name}: 収録数が {len(found)} 件で、指定した {len(expected)} 件と違う({sorted(found)})')
    print(f'  イメージ内容の確認: {len(expected)} 件すべて一致 (OK)')


# ---------------------------------------------------------------------------
# 手順1: TSUKUSHI.COM の再アセンブル
# ---------------------------------------------------------------------------

def build_com(tmp_dir: Path) -> Path:
    if not TSUKUSHI_ASM.exists():
        die(f'{TSUKUSHI_ASM} が無い')
    out_com = tmp_dir / 'TSUKUSHI.COM'
    run(['nasm', '-I', str(TSUKUSHI_DIR) + '/', '-f', 'bin',
         str(TSUKUSHI_ASM), '-o', str(out_com)], cwd=REPO_ROOT)
    if not out_com.exists():
        die('nasm はエラーを返さなかったが TSUKUSHI.COM が生成されなかった')
    return out_com


# ---------------------------------------------------------------------------
# 手順2: TSUKUSHI.DIC の再生成
# ---------------------------------------------------------------------------

def check_upstream_dics_exist() -> None:
    missing = []
    if not UPSTREAM_DIC_ML.exists():
        missing.append(
            f'  - {UPSTREAM_DIC_ML} (FD 用)\n'
            '    SKK-JISYO.ML (Medium-Large, GPL v2 以降) を下記から取得して\n'
            '    上記の場所に置いてください:\n'
            '      https://skk-dev.github.io/dict/\n'
            '    (直接ファイル: https://github.com/skk-dev/dict/raw/master/SKK-JISYO.ML)'
        )
    if not UPSTREAM_DIC_L.exists():
        missing.append(
            f'  - {UPSTREAM_DIC_L} (zip 用)\n'
            '    SKK-JISYO.L (Large, GPL v2 以降) を下記から取得して\n'
            '    上記の場所に置いてください:\n'
            '      https://skk-dev.github.io/dict/\n'
            '    (直接ファイル: https://github.com/skk-dev/dict/raw/master/SKK-JISYO.L)'
        )
    if missing:
        die(
            '上流辞書が見つからない:\n' + '\n'.join(missing) + '\n'
            'どちらもリポジトリには含まれません(.gitignore対象、数MBのため)。'
        )


def build_dic(tmp_dir: Path, out_name: str, src: Path) -> Path:
    out_dic = tmp_dir / out_name
    cmd = [sys.executable, str(MKDIC2_PY), '--src', str(src), '--out', str(out_dic)]
    run(cmd, cwd=REPO_ROOT)
    if not out_dic.exists():
        die(f'mkdic2.py はエラーを返さなかったが {out_name} が生成されなかった')
    return out_dic


def dic_entry_count(dic_path: Path) -> int:
    data = dic_path.read_bytes()
    if data[:8] != b'FEPDIC02':
        die(f'{dic_path}: マジックが FEPDIC02 でない')
    return struct.unpack('<I', data[8:12])[0]


# ---------------------------------------------------------------------------
# 手順3: READ.ME (CP932/CRLF) の生成
# ---------------------------------------------------------------------------

def build_readme_cp932(tmp_dir: Path, template: Path = None) -> Path:
    template = template or READ_ME_TEMPLATE
    if not template.exists():
        die(f'{template} が無い')
    READ_ME_TEMPLATE_USED.append(template)
    text = template.read_text(encoding='utf-8')
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines = lines[:-1]

    for i, line in enumerate(lines, 1):
        w = display_width(line)
        if w > MAX_LINE_WIDTH:
            die(f'{template}:{i}: 表示幅 {w} 桁 (上限 {MAX_LINE_WIDTH}) を超えている: {line!r}')

    crlf_text = '\r\n'.join(lines) + '\r\n'
    try:
        cp932_bytes = crlf_text.encode('cp932')
    except UnicodeEncodeError as e:
        die(f'{template}: CP932 に変換できない文字がある: {e}')

    out_path = tmp_dir / 'READ.ME'
    out_path.write_bytes(cp932_bytes)
    return out_path


# ---------------------------------------------------------------------------
# 手順3.5: FD 容量検査(build-disk.mjs を呼ぶ前に、辞書が収まるか検査する)
# ---------------------------------------------------------------------------

def check_fd_capacity(com: Path, dic: Path, readme: Path) -> None:
    root_sectors = (FD_ROOT_ENTRIES * 32) // FD_BYTES_PER_SECTOR
    data_start = FD_RESERVED_SECTORS + FD_FAT_COUNT * FD_SECTORS_PER_FAT + root_sectors
    data_sectors = FD_TOTAL_SECTORS - data_start
    data_bytes = data_sectors * FD_BYTES_PER_SECTOR

    def cluster_bytes(size: int) -> int:
        clusters = (size + FD_BYTES_PER_SECTOR - 1) // FD_BYTES_PER_SECTOR
        return clusters * FD_BYTES_PER_SECTOR

    other_bytes = (cluster_bytes(com.stat().st_size)
                   + cluster_bytes(readme.stat().st_size)
                   + cluster_bytes(LICENSE_TXT.stat().st_size)
                   + cluster_bytes(GPL2_TXT.stat().st_size))
    free_for_dic = data_bytes - other_bytes
    dic_size = dic.stat().st_size
    if dic_size > free_for_dic:
        die(
            f'FD 用辞書 {dic.name} ({dic_size} bytes) が FD に収まらない。'
            f'FD の辞書用空き容量は約 {free_for_dic} bytes '
            f'(全データ領域 {data_bytes} bytes から COM/READ.ME/LICENSE/GPL2 の '
            f'{other_bytes} bytes を引いた残り)。'
            f'語彙のより少ない上流辞書に差し替える'
            f'(または mkdic2.py --max-yomi-kana で読みの長さを絞る)か、'
            f'FD に同梱するファイルを減らしてください。'
        )
    print(f'  FD 容量検査: 辞書 {dic_size} bytes / 空き容量 {free_for_dic} bytes (OK)')


# ---------------------------------------------------------------------------
# 手順4: FD イメージ
# ---------------------------------------------------------------------------

def build_fd_image(version: str, com: Path, dic: Path, readme: Path) -> Path:
    out_xdf = OUT_RELEASE / f'tsukushi-v{version}.xdf'
    run(['node', str(BUILD_DISK_MJS), '--release',
         '--com', str(com), '--dic', str(dic), '--readme', str(readme),
         '--license', str(LICENSE_TXT), '--gpl2', str(GPL2_TXT),
         '--out', str(out_xdf)], cwd=REPO_ROOT)
    if not out_xdf.exists():
        die('build-disk.mjs はエラーを返さなかったが FD イメージが生成されなかった')
    return out_xdf


# expect_dicloc.py 相当の検査(このイメージ専用、TARGET探索とFAT12チェーンの
# 区間数を数える)。区間が1個であることを確認する。
def check_dic_contiguous(xdf_path: Path) -> int:
    data = xdf_path.read_bytes()
    boot = data[0:1024]
    bps = struct.unpack('<H', boot[0x0B:0x0D])[0]
    spc = boot[0x0D]
    rsvd = struct.unpack('<H', boot[0x0E:0x10])[0]
    nfat = boot[0x10]
    rootent = struct.unpack('<H', boot[0x11:0x13])[0]
    spf = struct.unpack('<H', boot[0x16:0x18])[0]

    fat_start = rsvd
    root_start = fat_start + nfat * spf
    root_sects = (rootent * 32 + bps - 1) // bps
    data_start = root_start + root_sects

    fat_off = fat_start * bps
    fat = data[fat_off:fat_off + spf * bps]

    def fat12_entry(cluster: int) -> int:
        off = cluster * 3 // 2
        raw = fat[off] | (fat[off + 1] << 8)
        if cluster % 2 == 0:
            return raw & 0x0FFF
        return raw >> 4

    root_off = root_start * bps
    root = data[root_off:root_off + root_sects * bps]
    target = b'TSUKUSHIDIC'
    file_clus = None
    for i in range(rootent):
        entry = root[i * 32:(i + 1) * 32]
        if entry[0] == 0x00:
            break
        if entry[0] == 0xE5:
            continue
        if entry[0:11] == target:
            file_clus = struct.unpack('<H', entry[26:28])[0]
            break
    if file_clus is None:
        die(f'{xdf_path}: TSUKUSHI.DIC がルートディレクトリに見つからない')

    runs = 1
    cur = file_clus
    safety = 8192
    while True:
        nxt = fat12_entry(cur)
        if nxt >= 0x0FF8:
            break
        if nxt != cur + 1:
            runs += 1
        cur = nxt
        safety -= 1
        if safety <= 0:
            die(f'{xdf_path}: FATチェーンが異常に長い(ループの疑い)')
    return runs


# ---------------------------------------------------------------------------
# 手順5: DICTIONARY-LICENSE.txt
# ---------------------------------------------------------------------------

def extract_skk_header(src: Path) -> str:
    raw = src.read_bytes()
    text = raw.decode('euc-jp')
    lines = text.split('\n')
    header_lines = []
    for line in lines:
        if line.startswith(';;'):
            header_lines.append(line[2:].strip())
        else:
            break
    return '\n'.join(header_lines)


def build_dictionary_license_txt(src: Path) -> str:
    header = extract_skk_header(src)
    upstream_name = src.name
    return f"""TSUKUSHI.DIC のライセンスについて (DICTIONARY-LICENSE)

この配布物の TSUKUSHI.DIC は {upstream_name} から生成した派生物であり、
{upstream_name} 自体が GNU General Public License version 2
(またはそれ以降) で配布されているため、TSUKUSHI.DIC も同様に
GPL v2 以降が適用されます。
ライセンス全文は同梱の COPYING-GPL2.txt を参照してください。

入手元:
  https://github.com/skk-dev/dict

生成に使ったスクリプト:
  tsukushi/tools/mkdic2.py (このリポジトリに含まれる MIT License のコード)

--- {upstream_name} 冒頭の著作権表示 (EUC-JP から変換) ---

{header}

--- ここまで ---

つくし本体(TSUKUSHI.COM および同梱のソースコード)は MIT License です
(LICENSE を参照)。TSUKUSHI.DIC (GPL) とは別のライセンスが適用される
別個の著作物であり、同じディスク/配布物に同梱しているのは単なる
寄せ集め(mere aggregation)です。つくし本体のライセンスが GPL に
変わるものではありません。
"""


# ---------------------------------------------------------------------------
# 手順6: zip
# ---------------------------------------------------------------------------

def build_zip_readme_text(fd_entry_count: int, zip_entry_count: int) -> str:
    base_text = READ_ME_TEMPLATE.read_text(encoding='utf-8')

    dic_note = f"""■ 辞書について(zip 版)
  この zip に同梱した TSUKUSHI.DIC は SKK-JISYO.L から作った全語彙版です
  ({zip_entry_count}語)。容量制限が無い zip だけの特典です。
  配布 FD イメージ(.xdf)側の辞書は、FD(1232KB)の容量に収まるよう
  SKK-JISYO.ML から作った版です({fd_entry_count}語)。読みの長さでは
  絞っていないので「ありがとう」「東京」のような語も変換できますが、
  より語彙の多いこの zip 版の辞書に差し替えて使うこともできます。

■ HDD へのコピー方法
  TSUKUSHI.COM と TSUKUSHI.DIC を、常駐させたいドライブのルート
  ディレクトリ(A:\\ 等)にそのままコピーしてください。2ファイルは
  必ず同じドライブに置く必要があります。
"""

    marker = '■ 動作環境'
    idx = base_text.find(marker)
    if idx == -1:
        die(f'{READ_ME_TEMPLATE}: zip 用 README.txt を組み立てられない({marker!r} が見つからない)')
    return base_text[:idx] + dic_note + '\n' + base_text[idx:]


def build_zip(version: str, com: Path, dic: Path, fd_entry_count: int, zip_entry_count: int) -> Path:
    readme_text = build_zip_readme_text(fd_entry_count, zip_entry_count)
    dict_license_text = build_dictionary_license_txt(UPSTREAM_DIC_L)

    zip_path = OUT_RELEASE / f'tsukushi-v{version}.zip'
    root_name = f'tsukushi-v{version}'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.write(com, f'{root_name}/TSUKUSHI.COM')
        zf.write(dic, f'{root_name}/TSUKUSHI.DIC')
        zf.writestr(f'{root_name}/README.txt', readme_text)
        zf.write(LICENSE_TXT, f'{root_name}/LICENSE')
        zf.writestr(f'{root_name}/DICTIONARY-LICENSE.txt', dict_license_text)
        zf.write(GPL2_TXT, f'{root_name}/COPYING-GPL2.txt')
    return zip_path


# ---------------------------------------------------------------------------
# 製品ごとの組み立て
# ---------------------------------------------------------------------------

def build_simple_zip(version: str, product: str, exe_name: str,
                     exe: Path, readme_md: Path) -> Path:
    """すみれ・つばき用の zip。README.txt は元の Markdown ではなく、FD へ
    入れるのと同じ本文(UTF-8/LF)。FD 側は CP932/CRLF、zip 側は UTF-8。"""
    zip_path = OUT_RELEASE / f'{product}-v{version}.zip'
    root_name = f'{product}-v{version}'
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.write(exe, f'{root_name}/{exe_name}')
        zf.writestr(f'{root_name}/README.txt', readme_md.read_text(encoding='utf-8'))
        zf.write(LICENSE_TXT, f'{root_name}/LICENSE')
    return zip_path


def release_tsukushi(version: str, tmp_dir: Path) -> list:
    print('[1/6] TSUKUSHI.COM をアセンブル中...')
    com = build_com(tmp_dir)

    check_upstream_dics_exist()
    print('[2/6] TSUKUSHI.DIC (FD用=SKK-JISYO.ML/zip用=SKK-JISYO.L) を生成中...')
    fd_dic = build_dic(tmp_dir, 'TSUKUSHI.DIC.fd', UPSTREAM_DIC_ML)
    zip_dic = build_dic(tmp_dir, 'TSUKUSHI.DIC.zip', UPSTREAM_DIC_L)
    fd_entry_count = dic_entry_count(fd_dic)
    zip_entry_count = dic_entry_count(zip_dic)

    print('[3/6] READ.ME (CP932/CRLF) を生成中...')
    readme = build_readme_cp932(tmp_dir)

    print('[4/6] FD 容量を検査し、イメージを組み立て中...')
    check_fd_capacity(com, fd_dic, readme)
    xdf = build_fd_image(version, com, fd_dic, readme)
    runs = check_dic_contiguous(xdf)
    if runs != 1:
        die(f'{xdf}: TSUKUSHI.DIC が断片化している(区間数={runs})。'
            'build-disk.mjs --release の配置ロジックを確認してください。')
    print(f'  TSUKUSHI.DIC の区間数: {runs} (OK)')

    print('[5/6] zip を組み立て中...')
    zip_path = build_zip(version, com, zip_dic, fd_entry_count, zip_entry_count)

    print('[6/6] 完了')
    return [
        ('TSUKUSHI.COM', com),
        ('TSUKUSHI.DIC (FD用)', fd_dic),
        ('TSUKUSHI.DIC (zip用/全語彙)', zip_dic),
        ('READ.ME (CP932)', readme),
        (xdf.name, xdf),
        (zip_path.name, zip_path),
    ]


def release_c_product(version: str, tmp_dir: Path, product: str,
                      src: Path, exe_name: str, readme_md: Path,
                      compile_mjs: Path) -> list:
    print(f'[1/4] {exe_name} をコンパイル中...')
    exe = compile_c(tmp_dir, src, exe_name, compile_mjs)

    print('[2/4] READ.ME (CP932/CRLF) を生成中...')
    readme = build_readme_cp932(tmp_dir, readme_md)

    print('[3/4] FD イメージを組み立て中...')
    entries = [(exe_name, exe), ('READ.ME', readme), ('LICENSE.TXT', LICENSE_TXT)]
    xdf = build_fd_image_files(f'{product}-v{version}.xdf', product.upper()[:8], entries)
    check_image_contents(xdf, entries)

    print('[4/4] zip を組み立て中...')
    zip_path = build_simple_zip(version, product, exe_name, exe, readme_md)

    return [
        (exe_name, exe),
        ('READ.ME (CP932)', readme),
        (xdf.name, xdf),
        (zip_path.name, zip_path),
    ]


def release_combined(version: str, tmp_dir: Path, compile_mjs: Path) -> list:
    """3本入りのまとめ FD。辞書は最後に置く(連続配置が要るため)。"""
    print('[1/5] SUMIRE.EXE / TSUBAKI.EXE をコンパイル中...')
    sumire_exe = compile_c(tmp_dir, SUMIRE_C, 'SUMIRE.EXE', compile_mjs)
    tsubaki_exe = compile_c(tmp_dir, TSUBAKI_C, 'TSUBAKI.EXE', compile_mjs)

    print('[2/5] TSUKUSHI.COM をアセンブル中...')
    com = build_com(tmp_dir)

    check_upstream_dics_exist()
    print('[3/5] TSUKUSHI.DIC (FD用=SKK-JISYO.ML) を生成中...')
    fd_dic = build_dic(tmp_dir, 'TSUKUSHI.DIC.fd', UPSTREAM_DIC_ML)
    fd_entry_count = dic_entry_count(fd_dic)

    print('[4/5] READ.ME (CP932/CRLF) を生成中...')
    readme = build_readme_cp932(tmp_dir, COMBINED_READ_ME)

    print('[5/5] FD イメージを組み立て中...')
    entries = [
        ('SUMIRE.EXE', sumire_exe),
        ('TSUBAKI.EXE', tsubaki_exe),
        ('TSUKUSHI.COM', com),
        ('READ.ME', readme),
        ('LICENSE.TXT', LICENSE_TXT),
        ('GPL2.TXT', GPL2_TXT),
        # 辞書は最後。つくしは辞書が連続配置であることを前提にしている。
        ('TSUKUSHI.DIC', fd_dic),
    ]
    xdf = build_fd_image_files(f'pc98guest-v{version}.xdf', 'PC98GST', entries)
    check_image_contents(xdf, entries)
    runs = check_dic_contiguous(xdf)
    if runs != 1:
        die(f'{xdf}: TSUKUSHI.DIC が断片化している(区間数={runs})')
    print(f'  TSUKUSHI.DIC の区間数: {runs} (OK)')
    print(f'  TSUKUSHI.DIC 語数: {fd_entry_count}')

    return [
        ('SUMIRE.EXE', sumire_exe),
        ('TSUBAKI.EXE', tsubaki_exe),
        ('TSUKUSHI.COM', com),
        ('TSUKUSHI.DIC', fd_dic),
        ('READ.ME (CP932)', readme),
        (xdf.name, xdf),
    ]


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description='PC98Guest の Release 配布物を組み立てる')
    ap.add_argument('--product', required=True,
                    choices=['tsukushi', 'sumire', 'tsubaki', 'combined'],
                    help='組み立てる対象。combined は3本入りのまとめ FD。')
    ap.add_argument('--version', required=True, help='版番号(例: 0.1.0)。省略不可。')
    ap.add_argument('--compile-mjs', default=None,
                    help=f'WorkbenchNP2 の toolchain/compile.mjs のパス(既定: {DEFAULT_COMPILE_MJS})')
    args = ap.parse_args()
    version = args.version
    compile_mjs = Path(args.compile_mjs) if args.compile_mjs else DEFAULT_COMPILE_MJS

    OUT_RELEASE.mkdir(parents=True, exist_ok=True)
    # この製品の配布物だけを消す(他の製品の成果物は残す。まとめて
    # Release するときに前の製品を消してしまわないため)。
    stem = 'pc98guest' if args.product == 'combined' else args.product
    for old in OUT_RELEASE.glob(f'{stem}-v*'):
        old.unlink()

    with tempfile.TemporaryDirectory(prefix='pc98guest-release-') as tmp:
        tmp_dir = Path(tmp)

        if args.product == 'tsukushi':
            manifest_files = release_tsukushi(version, tmp_dir)
        elif args.product == 'sumire':
            manifest_files = release_c_product(
                version, tmp_dir, 'sumire', SUMIRE_C, 'SUMIRE.EXE',
                SUMIRE_READ_ME, compile_mjs)
        elif args.product == 'tsubaki':
            manifest_files = release_c_product(
                version, tmp_dir, 'tsubaki', TSUBAKI_C, 'TSUBAKI.EXE',
                TSUBAKI_READ_ME, compile_mjs)
        else:
            manifest_files = release_combined(version, tmp_dir, compile_mjs)

        print()
        print('=== マニフェスト ===')
        for label, path in manifest_files:
            size = path.stat().st_size
            digest = sha256_of(path)
            print(f'  {label}: {size} bytes  sha256={digest}')
        print()
        print(f'完了: {OUT_RELEASE}')


if __name__ == '__main__':
    main()
