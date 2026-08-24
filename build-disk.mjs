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
// 測定用プログラム一式。FreeDOS(98) 側での確認にも使うので FEP 本体と辞書も入れる。
const files = [
  { name: 'ESCT1   ', ext: 'COM', path: join(HERE, 'ESCT1.COM') },
  { name: 'ESCT2   ', ext: 'COM', path: join(HERE, 'ESCT2.COM') },
  { name: 'FEP     ', ext: 'COM', path: join(HERE, 'FEP.COM') },
  { name: 'DICTEST ', ext: 'COM', path: join(HERE, 'DICTEST.COM') },
  { name: 'FEP     ', ext: 'DIC', path: join(HERE, 'out', 'FEP.DIC') },
  { name: 'AHSPY   ', ext: 'COM', path: join(HERE, 'AHSPY.COM') },
  { name: 'INTEST  ', ext: 'COM', path: join(HERE, 'INTEST.COM') },
  { name: 'K18SPY  ', ext: 'COM', path: join(HERE, 'K18SPY.COM') },
];

const root = Buffer.alloc(ROOT_SECTORS * BYTES_PER_SECTOR, 0);
let nextCluster = 2;
let rootOffset = 0;

for (const file of files) {
  const data = readFileSync(file.path);
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

const outDir = join(HERE, 'out');
mkdirSync(outDir, { recursive: true });
const outPath = join(outDir, 'esctest.xdf');
writeFileSync(outPath, image);
console.log(`wrote ${outPath} (${image.length} bytes)`);
