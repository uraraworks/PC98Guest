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

def build_dic(tmp_dir: Path) -> Path:
    if not UPSTREAM_DIC.exists():
        die(
            f'上流辞書が見つからない: {UPSTREAM_DIC}\n'
            '  SKK-JISYO.L (GPL v2 以降) を下記から取得して上記の場所に置いてください:\n'
            '    https://skk-dev.github.io/dict/\n'
            '  (直接ファイル: https://github.com/skk-dev/dict/raw/master/SKK-JISYO.L)\n'
            '  リポジトリには含まれません(.gitignore対象、数MBのため)。'
        )
    out_dic = tmp_dir / 'TSUKUSHI.DIC'
    run([sys.executable, str(MKDIC2_PY), '--max-yomi-kana', '4',
         '--out', str(out_dic)], cwd=REPO_ROOT)
    if not out_dic.exists():
        die('mkdic2.py はエラーを返さなかったが TSUKUSHI.DIC が生成されなかった')
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

def build_zip(version: str, com: Path, dic: Path) -> Path:
    readme_text = READ_ME_TEMPLATE.read_text(encoding='utf-8')
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

        print('[1/6] TSUKUSHI.COM をアセンブル中...')
        com = build_com(tmp_dir)

        print('[2/6] TSUKUSHI.DIC を生成中...')
        dic = build_dic(tmp_dir)
        entry_count = dic_entry_count(dic)

        print('[3/6] READ.ME (CP932/CRLF) を生成中...')
        readme = build_readme_cp932(tmp_dir)

        print('[4/6] FD イメージを組み立て中...')
        xdf = build_fd_image(version, com, dic, readme)
        runs = check_dic_contiguous(xdf)
        if runs != 1:
            die(f'{xdf}: TSUKUSHI.DIC が断片化している(区間数={runs})。'
                'build-disk.mjs --release の配置ロジックを確認してください。')
        print(f'  TSUKUSHI.DIC の区間数: {runs} (OK)')

        print('[5/6] zip を組み立て中...')
        zip_path = build_zip(version, com, dic)

        print('[6/6] マニフェストを出力中...')
        manifest_files = [
            ('TSUKUSHI.COM', com),
            ('TSUKUSHI.DIC', dic),
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
        print(f'  TSUKUSHI.DIC 語数: {entry_count}')
        print()
        print(f'完了: {OUT_RELEASE}')


if __name__ == '__main__':
    main()
