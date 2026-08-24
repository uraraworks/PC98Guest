#!/usr/bin/env python3
"""Release用配布物(FDイメージ + zip)を組み立てるスクリプト。

既存の成果物は一切信用せず、毎回ソースから作り直す:
  - TSUKUSHI.COM は毎回 nasm でアセンブルし直す
  - TSUKUSHI.DIC は毎回 mkdic2.py で生成し直す
どちらかが失敗したら即座に停止し、途中までの配布物を out/release/ に
残さない。

使い方:
  python3 common/make_release.py --version 0.1.0
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
UPSTREAM_DIC = TSUKUSHI_DIR / 'dic' / 'upstream' / 'SKK-JISYO.L'
GPL2_TXT = TSUKUSHI_DIR / 'dic' / 'COPYING-GPL2.txt'
LICENSE_TXT = REPO_ROOT / 'LICENSE'
READ_ME_TEMPLATE = TSUKUSHI_DIR / 'READ.ME.md'
BUILD_DISK_MJS = HERE / 'build-disk.mjs'
MKDIC2_PY = TSUKUSHI_DIR / 'tools' / 'mkdic2.py'
TSUKUSHI_ASM = TSUKUSHI_DIR / 'TSUKUSHI.ASM'
OUT_RELEASE = REPO_ROOT / 'out' / 'release'

MAX_LINE_WIDTH = 80

# FD 用辞書の読みかな数上限。zip 用はこの制限を付けず全語彙を収録する。
FD_MAX_YOMI_KANA = 4

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

def build_dic(tmp_dir: Path, out_name: str, max_yomi_kana: int | None) -> Path:
    if not UPSTREAM_DIC.exists():
        die(
            f'上流辞書が見つからない: {UPSTREAM_DIC}\n'
            '  SKK-JISYO.L (GPL v2 以降) を下記から取得して上記の場所に置いてください:\n'
            '    https://skk-dev.github.io/dict/\n'
            '  (直接ファイル: https://github.com/skk-dev/dict/raw/master/SKK-JISYO.L)\n'
            '  リポジトリには含まれません(.gitignore対象、数MBのため)。'
        )
    out_dic = tmp_dir / out_name
    cmd = [sys.executable, str(MKDIC2_PY)]
    if max_yomi_kana is not None:
        cmd += ['--max-yomi-kana', str(max_yomi_kana)]
    cmd += ['--out', str(out_dic)]
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

def build_readme_cp932(tmp_dir: Path) -> Path:
    if not READ_ME_TEMPLATE.exists():
        die(f'{READ_ME_TEMPLATE} が無い')
    text = READ_ME_TEMPLATE.read_text(encoding='utf-8')
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines = lines[:-1]

    for i, line in enumerate(lines, 1):
        w = display_width(line)
        if w > MAX_LINE_WIDTH:
            die(f'{READ_ME_TEMPLATE}:{i}: 表示幅 {w} 桁 (上限 {MAX_LINE_WIDTH}) を超えている: {line!r}')

    crlf_text = '\r\n'.join(lines) + '\r\n'
    try:
        cp932_bytes = crlf_text.encode('cp932')
    except UnicodeEncodeError as e:
        die(f'{READ_ME_TEMPLATE}: CP932 に変換できない文字がある: {e}')

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
            f'--max-yomi-kana を絞るか、FD に同梱するファイルを減らしてください。'
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

def extract_skk_header() -> str:
    raw = UPSTREAM_DIC.read_bytes()
    text = raw.decode('euc-jp')
    lines = text.split('\n')
    header_lines = []
    for line in lines:
        if line.startswith(';;'):
            header_lines.append(line[2:].strip())
        else:
            break
    return '\n'.join(header_lines)


def build_dictionary_license_txt() -> str:
    header = extract_skk_header()
    return f"""TSUKUSHI.DIC のライセンスについて (DICTIONARY-LICENSE)

TSUKUSHI.DIC は SKK-JISYO.L から生成した派生物であり、SKK-JISYO.L 自体が
GNU General Public License version 2 (またはそれ以降) で配布されている
ため、TSUKUSHI.DIC も同様に GPL v2 以降が適用されます。
ライセンス全文は同梱の COPYING-GPL2.txt を参照してください。

入手元:
  https://github.com/skk-dev/dict

生成に使ったスクリプト:
  tsukushi/tools/mkdic2.py (このリポジトリに含まれる MIT License のコード)

--- SKK-JISYO.L 冒頭の著作権表示 (EUC-JP から変換) ---

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
  この zip に同梱した TSUKUSHI.DIC は全語彙版です({zip_entry_count}語)。
  容量制限が無い zip だけの特典で、「ありがとう」「東京」「入力」のような
  読み5かな以上の語も変換できます。
  配布 FD イメージ(.xdf)側の辞書は、FD(1232KB)の容量に収まる読み4かな
  以内版です({fd_entry_count}語)。より語彙の多いこの zip 版の辞書に
  差し替えて使うこともできます。

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
    dict_license_text = build_dictionary_license_txt()

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
# main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description='つくし Release 配布物を組み立てる')
    ap.add_argument('--version', required=True, help='版番号(例: 0.1.0)。省略不可。')
    args = ap.parse_args()
    version = args.version

    if OUT_RELEASE.exists():
        shutil.rmtree(OUT_RELEASE)
    OUT_RELEASE.mkdir(parents=True)

    with tempfile.TemporaryDirectory(prefix='tsukushi-release-') as tmp:
        tmp_dir = Path(tmp)

        print('[1/7] TSUKUSHI.COM をアセンブル中...')
        com = build_com(tmp_dir)

        print('[2/7] TSUKUSHI.DIC (FD用/zip用の2種) を生成中...')
        fd_dic = build_dic(tmp_dir, 'TSUKUSHI.DIC.fd', FD_MAX_YOMI_KANA)
        zip_dic = build_dic(tmp_dir, 'TSUKUSHI.DIC.zip', None)
        fd_entry_count = dic_entry_count(fd_dic)
        zip_entry_count = dic_entry_count(zip_dic)

        print('[3/7] READ.ME (CP932/CRLF) を生成中...')
        readme = build_readme_cp932(tmp_dir)

        print('[4/7] FD 容量を検査中...')
        check_fd_capacity(com, fd_dic, readme)

        print('[5/7] FD イメージを組み立て中...')
        xdf = build_fd_image(version, com, fd_dic, readme)
        runs = check_dic_contiguous(xdf)
        if runs != 1:
            die(f'{xdf}: TSUKUSHI.DIC が断片化している(区間数={runs})。'
                'build-disk.mjs --release の配置ロジックを確認してください。')
        print(f'  TSUKUSHI.DIC の区間数: {runs} (OK)')

        print('[6/7] zip を組み立て中...')
        zip_path = build_zip(version, com, zip_dic, fd_entry_count, zip_entry_count)

        print('[7/7] マニフェストを出力中...')
        manifest_files = [
            ('TSUKUSHI.COM', com),
            ('TSUKUSHI.DIC (FD用)', fd_dic),
            ('TSUKUSHI.DIC (zip用/全語彙)', zip_dic),
            ('READ.ME (CP932)', readme),
            (xdf.name, xdf),
            (zip_path.name, zip_path),
        ]
        print()
        print('=== マニフェスト ===')
        for label, path in manifest_files:
            size = path.stat().st_size
            digest = sha256_of(path)
            print(f'  {label}: {size} bytes  sha256={digest}')
        print(f'  TSUKUSHI.DIC (FD用) 語数: {fd_entry_count}')
        print(f'  TSUKUSHI.DIC (zip用/全語彙) 語数: {zip_entry_count}')
        print()
        print(f'完了: {OUT_RELEASE}')


if __name__ == '__main__':
    main()
