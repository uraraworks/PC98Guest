# PC98Guest

Software that runs *inside* the PC-98 guest (under DOS), developed alongside
the WebNP2 PC-98 emulator project. It includes **つくし (Tsukushi)**, a
resident kana-kanji conversion FEP for FreeDOS(98), and **probes**, a set of
small measurement programs used to determine undocumented PC-98 behavior.
See [LICENSE](LICENSE) (MIT) and the "Contribution policy" section below
before sending anything.

## これは何か

このリポジトリは、PC-98 の**ゲスト側**(DOS が動くエミュレータの中)で
動作させるソフトウェアを置く場所です。母体は WebNP2 PC-98 エミュレータ
プロジェクトで、ここには「エミュレータ本体」ではなく「エミュレータの中で
動くもの」を収めています。

## 収録物

### つくし (TSUKUSHI) — `tsukushi/`

FreeDOS(98) 用のかな漢字変換 FEP(フロントエンドプロセッサ)です。
INT 18h(キーボードBIOS)をフックして常駐し、ローマ字入力をひらがなに
変換したうえで、ディスク上の辞書を引いて単文節のかな漢字変換を行います。
詳細な使い方・キー操作・仕組みは [`tsukushi/README.md`](tsukushi/README.md)
を参照してください。

### probes — `probes/`

PC-98 の挙動を実測するために書いた小さなプログラム群です。
**製品ではなく測定器**です。INT 18h のフック順序、ディスクI/O(INT 1Bh)の
挙動、FAT12/FAT16 のレイアウトなど、資料だけでは確定しない事実を
実機・エミュレータ上で確かめるために作られました。つくしの実装は
これらの測定結果の上に成り立っています。

## 動作環境

- **FreeDOS(98)** / **PC-98**(実機相当のエミュレーション)
- 動作確認は Neko Project II kai(WebNP2 が使用しているコア)上で
  行っています
- **NEC MS-DOS では動作を保証しません**(FreeDOS(98) 前提で作られています。
  部分的に動く可能性はありますが未検証です)
- 辞書の置き場は**フロッピーディスク**または **SASI ハードディスク**です。
  **SCSI ハードディスクは対象外**です(エミュレータ側に SCSI を指定する経路が
  無く、動作を確認できないため)
- ノーマルモード(640x400)のみに対応。ハイレゾモードは対象外です
  (Neko Project II kai がハイレゾを実装しておらず、確認できないため)

## ディレクトリ構成

```
guest/
  tsukushi/     つくし本体(FEP)。ソース・辞書生成ツール・辞書ソース
  probes/       測定用プログラム群
  common/       ディスク/HDDイメージの組み立て・検証スクリプト
  out/          生成物(gitignore対象。リポジトリには含めない)
```

## ビルド方法

必要なもの: [nasm](https://www.nasm.us/)、Python 3、Node.js

```sh
# つくし本体のアセンブル(リポジトリルートで実行すること。-I でインクルードパスを指定)
nasm -I tsukushi/ -f bin tsukushi/TSUKUSHI.ASM -o tsukushi/TSUKUSHI.COM

# 辞書の生成(SKK-JISYO.L が tsukushi/dic/upstream/ にある場合)
python3 tsukushi/tools/mkdic2.py --out out/TSUKUSHI.DIC

# probes を含む測定用FDイメージの組み立て
node common/build-disk.mjs
```

各 probe(`probes/*.ASM`)も同様に `nasm -f bin` で個別にアセンブルできます。

## リリース物の作り方

配布用の FD イメージ(`.xdf`)と zip をまとめて作るには次を実行します
(SKK-JISYO.L と SKK-JISYO.ML が `tsukushi/dic/upstream/` にある場合):

```sh
python3 common/make_release.py --version 0.1.0
```

`out/release/` に `tsukushi-v<version>.xdf` と `tsukushi-v<version>.zip` が
生成されます。`TSUKUSHI.COM`・`TSUKUSHI.DIC` は既存の成果物を使い回さず、
毎回ソース(`TSUKUSHI.ASM`・`mkdic2.py`)から作り直します。収録するのは
本体・辞書・`READ.ME`・`LICENSE.TXT`・`GPL2.TXT` の5ファイルのみで、
probes は含みません。

同梱する `TSUKUSHI.DIC` は FD 版と zip 版で元にする上流辞書が異なります。
FD 版は容量制限(1232KB)に収めるため語彙の少ない SKK-JISYO.ML から
作った版(41,573語)、zip 版には容量制限が無いため SKK-JISYO.L から
作った全語彙版(131,835語)を収録します。どちらも読みの長さでは
絞っていません。

## 受け取りポリシー

既存の FEP や市販ソフトを逆アセンブルした結果・内部仕様の提供は
**受け取りません**。仕様は公開資料(書籍・規格)と自分たちの実測だけで
組み立てています。提供いただいても取り込めませんので、送らないで
ください。

バグ報告・実測データ・公開資料に基づく指摘は歓迎します。

## ライセンス

つくし本体・probes・ビルドスクリプト等、このリポジトリのコードは
[MIT License](LICENSE) です。

辞書データ(`out/TSUKUSHI.DIC` 等の生成物)は SKK-JISYO.L の派生物であり、
**GPL v2 以降**が適用されます。詳細は
[`tsukushi/dic/README.md`](tsukushi/dic/README.md) を参照してください。
