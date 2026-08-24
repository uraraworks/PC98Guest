# 辞書データのライセンスについて

`tools/mkdic.py` / `tools/mkdic2.py` が生成する辞書ファイル(例:
`out/TSUKUSHI.DIC`)は、上流の SKK 辞書の派生物です。上流辞書は
2種類使っています(用途で使い分け):

- **SKK-JISYO.L**(Large、全語彙): zip 配布物の `TSUKUSHI.DIC` の元。
  容量制限が無いため読みの長さを絞らず全語彙を収録します
  (131,835語)。
- **SKK-JISYO.ML**(Medium-Large): FD イメージ配布物の `TSUKUSHI.DIC`
  の元。FD(1232KB)に収める必要があるため語彙の少ない辞書を選び、
  読みの長さでは絞りません(41,573語)。以前は SKK-JISYO.L を
  `--max-yomi-kana 4` で読みの長さのほうを絞って使っていましたが、
  「ありがとう」「東京」のような読みの長い頻出語が落ちる問題があった
  ため、SKK-JISYO.ML に切り替えました。

どちらも SKK 開発チームによる同一著作者の辞書で、**GPL v2 以降**で
配布されているため、これらから生成した辞書ファイルおよびそれを含む
配布物も同様に **GPL v2 以降**が適用されます。

## 配布するときに一緒に含めるもの

辞書ファイル(`TSUKUSHI.DIC` 等)を配布する場合は、以下を一緒に配ってください:

- 元にした上流辞書(SKK-JISYO.L または SKK-JISYO.ML)の著作権表示
- GPL のライセンス全文
- 変換に使ったスクリプト(`tools/mkdic2.py`、およびその依存元である
  `tools/dic2_ref.py` 等)

## 入手元

SKK-JISYO.L・SKK-JISYO.ML はいずれも SKK 開発チームの辞書配布ページ
から入手できます:
<https://skk-dev.github.io/dict/>

（本リポジトリの `dic/upstream/SKK-JISYO.L` と `dic/upstream/SKK-JISYO.ML`
は取得済みファイルの置き場所ですが、`.gitignore` により**リポジトリには
含まれません**。数MBあり、かつ上記URLから誰でも再取得できるためです。
生成物の `out/TSUKUSHI.DIC` 等も同様にリポジトリには含めません。）

## つくし本体との関係

つくし本体(`TSUKUSHI.ASM` 等のソースコード)は [MIT License](../../LICENSE)
です。辞書データ(GPL)とは**別物**であり、同じフロッピーディスクや
配布物に同梱しても、それは単なる寄せ集め(mere aggregation)であって
つくし本体のライセンスが GPL に変わるものではありません。ただし、
GPL の辞書データを含んだ配布物全体としては、上記の GPL の条件
(著作権表示・ライセンス全文・変換元スクリプトの同梱)を満たす必要が
あります。
