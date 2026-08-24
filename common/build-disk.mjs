// ESCT1.COM 測定用FD (out/esctest.xdf) を組み立てるスクリプト。
// PC-98 の 2HD 1232KB (1024バイト/セクタ, 8セクタ/トラック, 2ヘッド, 77シリンダ)
// FAT12 ベタイメージを生成する。
//
//   nasm -f bin ESCT1.ASM -o ESCT1.COM
//   node build-disk.mjs
//
// 出力: out/esctest.xdf (このリポジトリ内のみ。WebNP2側には一切書き込まない)

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = dirname(HERE);
const PROBES = join(REPO_ROOT, 'probes');
const TSUKUSHI = join(REPO_ROOT, 'tsukushi');
const OUT_DIR = join(REPO_ROOT, 'out');

const BYTES_PER_SECTOR = 1024;
const SECTORS_PER_CLUSTER = 1;
const RESERVED_SECTORS = 1;
const FAT_COUNT = 2;
const ROOT_ENTRIES = 192;
const TOTAL_SECTORS = 1232;
const MEDIA = 0xfe;
const SECTORS_PER_FAT = 2;
const SECTORS_PER_TRACK = 8;
const HEADS = 2;

const ROOT_SECTORS = (ROOT_ENTRIES * 32) / BYTES_PER_SECTOR; // 6
const FAT_START = RESERVED_SECTORS;
const ROOT_START = FAT_START + FAT_COUNT * SECTORS_PER_FAT;
const DATA_START = ROOT_START + ROOT_SECTORS;

// --fragment N : TSUKUSHI.DIC を N個の断片に分けて配置する(常駐側の
// 区間対応(段階E)をテストするため)。断片の間には「不良クラスタ」
// マーカー(FAT12の0xFF7)で埋めたダミークラスタを挟み、クラスタ
// チェーンが連続にならないようにする。FATのチェーン自体はDOSから
// 普通に読める正しい形で張る。
const FRAGMENT_GAP_CLUSTERS = 1;

let fragmentCount = 1;
for (let i = 0; i < process.argv.length; i++) {
  if (process.argv[i] === '--fragment') {
    fragmentCount = parseInt(process.argv[i + 1], 10);
    if (!Number.isInteger(fragmentCount) || fragmentCount < 1) {
      console.error('--fragment には1以上の整数を指定してください');
      process.exit(1);
    }
  }
}

const image = Buffer.alloc(TOTAL_SECTORS * BYTES_PER_SECTOR, 0);

// --- ブートセクタ (BPB のみ。起動はしない) ---
image[0] = 0xeb; // jmp short +0x3c
image[1] = 0x3c;
image[2] = 0x90; // nop
image.write('ESCT1   ', 3, 8, 'ascii');
image.writeUInt16LE(BYTES_PER_SECTOR, 11);
image[13] = SECTORS_PER_CLUSTER;
image.writeUInt16LE(RESERVED_SECTORS, 14);
image[16] = FAT_COUNT;
image.writeUInt16LE(ROOT_ENTRIES, 17);
image.writeUInt16LE(TOTAL_SECTORS, 19);
image[21] = MEDIA;
image.writeUInt16LE(SECTORS_PER_FAT, 22);
image.writeUInt16LE(SECTORS_PER_TRACK, 24);
image.writeUInt16LE(HEADS, 26);
image.writeUInt32LE(0, 28); // hidden sectors
image[510] = 0x55;
image[511] = 0xaa;

// --- FAT ---
const fat = Buffer.alloc(SECTORS_PER_FAT * BYTES_PER_SECTOR, 0);
function setFatEntry(cluster, value) {
  const offset = Math.floor(cluster * 3 / 2);
  if (cluster % 2 === 0) {
    fat[offset] = value & 0xff;
    fat[offset + 1] = (fat[offset + 1] & 0xf0) | ((value >> 8) & 0x0f);
  } else {
    fat[offset] = (fat[offset] & 0x0f) | ((value << 4) & 0xf0);
    fat[offset + 1] = (value >> 4) & 0xff;
  }
}
setFatEntry(0, 0xf00 | MEDIA);
setFatEntry(1, 0xfff);

// --- ファイル配置 ---
// 測定用プログラム一式。FreeDOS(98) 側での確認にも使うので つくし本体と辞書も入れる。
const files = [
  { name: 'ESCT1   ', ext: 'COM', path: join(PROBES, 'ESCT1.COM') },
  { name: 'ESCT2   ', ext: 'COM', path: join(PROBES, 'ESCT2.COM') },
  { name: 'FEP     ', ext: 'COM', path: join(PROBES, 'FEP.COM') },
  { name: 'TSUKUSHI', ext: 'COM', path: join(TSUKUSHI, 'TSUKUSHI.COM') },
  // DICTEST.COM は v1辞書(FEPDIC01)専用。TSUKUSHIは v2(FEPDIC02, ディスク版)を
  // 使うようになったため、DICTEST.COM 自体はリポジトリに残すがイメージには
  // 収録しない(source of confusion を避けるため)。
  { name: 'TSUKUSHI', ext: 'DIC', path: join(OUT_DIR, 'TSUKUSHI.DIC') },
  { name: 'AHSPY   ', ext: 'COM', path: join(PROBES, 'AHSPY.COM') },
  { name: 'INTEST  ', ext: 'COM', path: join(PROBES, 'INTEST.COM') },
  { name: 'K18SPY  ', ext: 'COM', path: join(PROBES, 'K18SPY.COM') },
  { name: 'RAWRD   ', ext: 'COM', path: join(PROBES, 'RAWRD.COM') },
  { name: 'RAWRD18 ', ext: 'COM', path: join(PROBES, 'RAWRD18.COM') },
  { name: 'DICLOC  ', ext: 'COM', path: join(PROBES, 'DICLOC.COM') },
  { name: 'HDDRD   ', ext: 'COM', path: join(PROBES, 'HDDRD.COM') },
];

const root = Buffer.alloc(ROOT_SECTORS * BYTES_PER_SECTOR, 0);
let nextCluster = 2;
let rootOffset = 0;

// 通常配置: クラスタを連続で割り当てる(従来どおり)
function allocateContiguous(data) {
  const clusterCount = Math.max(1, Math.ceil(data.length / (BYTES_PER_SECTOR * SECTORS_PER_CLUSTER)));
  const firstCluster = nextCluster;
  for (let i = 0; i < clusterCount; i++) {
    const cluster = firstCluster + i;
    const isLast = i === clusterCount - 1;
    setFatEntry(cluster, isLast ? 0xfff : cluster + 1);
    const sector = DATA_START + (cluster - 2) * SECTORS_PER_CLUSTER;
    data.copy(image, sector * BYTES_PER_SECTOR, i * BYTES_PER_SECTOR, Math.min((i + 1) * BYTES_PER_SECTOR, data.length));
  }
  nextCluster += clusterCount;
  return firstCluster;
}

// 断片化配置: N個の断片に分け、断片の間に不良クラスタマーカーで埋めた
// ダミークラスタを挟む。FATチェーン自体は各断片をまたいで正しくつながる
// (DOSから見て普通に読める1本のファイル)。
function allocateFragmented(data, nFrags) {
  const clusterBytes = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER;
  const totalClusters = Math.max(1, Math.ceil(data.length / clusterBytes));
  const n = Math.min(nFrags, totalClusters);
  if (n < nFrags) {
    console.log(`  (note: fragment count reduced from ${nFrags} to ${n}: file has only ${totalClusters} clusters)`);
  }

  const base = Math.floor(totalClusters / n);
  const extra = totalClusters % n;
  const fragSizes = [];
  for (let i = 0; i < n; i++) fragSizes.push(base + (i < extra ? 1 : 0));

  const fragRuns = [];
  let clusterOffset = 0; // ファイル内の0起点クラスタ番号
  let firstClusterOfFile = null;
  let prevLastCluster = null;

  for (let f = 0; f < fragSizes.length; f++) {
    const size = fragSizes[f];
    const fragFirst = nextCluster;
    fragRuns.push({ start: fragFirst, count: size });
    if (firstClusterOfFile === null) firstClusterOfFile = fragFirst;

    for (let i = 0; i < size; i++) {
      const cluster = fragFirst + i;
      const fileClusterIndex = clusterOffset + i;
      const isLastOfFile = (f === fragSizes.length - 1) && (i === size - 1);
      if (i < size - 1) {
        setFatEntry(cluster, cluster + 1);
      } else if (isLastOfFile) {
        setFatEntry(cluster, 0xfff);
      }
      // 断片内最終クラスタ(かつファイル末尾でない)は、次の断片の先頭が
      // 決まってから(次のループの先頭で)つなぐ。ここでは仮値のまま。
      const sector = DATA_START + (cluster - 2) * SECTORS_PER_CLUSTER;
      data.copy(image, sector * BYTES_PER_SECTOR, fileClusterIndex * BYTES_PER_SECTOR,
                Math.min((fileClusterIndex + 1) * BYTES_PER_SECTOR, data.length));
    }

    if (prevLastCluster !== null) {
      setFatEntry(prevLastCluster, fragFirst); // 前の断片の最終クラスタ -> この断片の先頭
    }
    prevLastCluster = fragFirst + size - 1;
    clusterOffset += size;
    nextCluster += size;

    if (f < fragSizes.length - 1) {
      // 断片間にダミークラスタ(不良クラスタマーカー)を挟む。
      // どのファイルにも属さないのでFATチェーン上は無関係。
      for (let g = 0; g < FRAGMENT_GAP_CLUSTERS; g++) {
        setFatEntry(nextCluster, 0xff7);
        nextCluster += 1;
      }
    }
  }

  console.log(`  TSUKUSHI.DIC fragmented into ${fragRuns.length} run(s):`);
  for (const r of fragRuns) {
    console.log(`    start cluster ${r.start}  cluster count ${r.count}`);
  }

  return firstClusterOfFile;
}

for (const file of files) {
  const data = readFileSync(file.path);
  const isDic = file.name === 'TSUKUSHI' && file.ext === 'DIC';

  const firstCluster = (isDic && fragmentCount > 1)
    ? allocateFragmented(data, fragmentCount)
    : allocateContiguous(data);

  root.write(file.name, rootOffset, 8, 'ascii');
  root.write(file.ext, rootOffset + 8, 3, 'ascii');
  root[rootOffset + 11] = 0x20; // archive
  // 時刻 00:00:00 / 日付 2026-08-23
  root.writeUInt16LE(0, rootOffset + 22);
  root.writeUInt16LE(((2026 - 1980) << 9) | (8 << 5) | 23, rootOffset + 24);
  root.writeUInt16LE(firstCluster, rootOffset + 26);
  root.writeUInt32LE(data.length, rootOffset + 28);
  rootOffset += 32;

  console.log(`  ${file.name.trim()}.${file.ext}  ${data.length} bytes  cluster ${firstCluster}`);
}

// --- 書き戻し ---
for (let i = 0; i < FAT_COUNT; i++) {
  fat.copy(image, (FAT_START + i * SECTORS_PER_FAT) * BYTES_PER_SECTOR);
}
root.copy(image, ROOT_START * BYTES_PER_SECTOR);

mkdirSync(OUT_DIR, { recursive: true });
const outPath = join(OUT_DIR, 'esctest.xdf');
writeFileSync(outPath, image);
console.log(`wrote ${outPath} (${image.length} bytes)`);
