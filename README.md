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
- ノーマルモード(640x400)のみに対応。ハイレゾモードは対象外です

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

# 辞書の生成(SKK-JISYO.L が dic/upstream/ にある場合)
python3 tsukushi/tools/mkdic2.py --out out/FEP.DIC

# probes を含む測定用FDイメージの組み立て
node common/build-disk.mjs
```

各 probe(`probes/*.ASM`)も同様に `nasm -f bin` で個別にアセンブルできます。

## 受け取りポリシー

既存の FEP や市販ソフトを逆アセンブルした結果・内部仕様の提供は
**受け取りません**。仕様は公開資料(書籍・規格)と自分たちの実測だけで
組み立てています。提供いただいても取り込めませんので、送らないで
ください。

バグ報告・実測データ・公開資料に基づく指摘は歓迎します。

## ライセンス

つくし本体・probes・ビルドスクリプト等、このリポジトリのコードは
[MIT License](LICENSE) です。

辞書データ(`out/FEP.DIC` 等の生成物)は SKK-JISYO.L の派生物であり、
**GPL v2 以降**が適用されます。詳細は
[`tsukushi/dic/README.md`](tsukushi/dic/README.md) を参照してください。
