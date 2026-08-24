# 辞書データのライセンスについて

`tools/mkdic.py` / `tools/mkdic2.py` が生成する辞書ファイル(例:
`out/FEP.DIC`)は、[SKK-JISYO.L](https://skk-dev.github.io/dict/) の
派生物です。SKK-JISYO.L 自体は **GPL v2 以降**で配布されているため、
この辞書ファイルおよびそれを含む配布物も同様に **GPL v2 以降**が
適用されます。

## 配布するときに一緒に含めるもの

辞書ファイル(`FEP.DIC` 等)を配布する場合は、以下を一緒に配ってください:

- SKK-JISYO.L の著作権表示
- GPL のライセンス全文
- 変換に使ったスクリプト(`tools/mkdic2.py`、およびその依存元である
  `tools/dic2_ref.py` 等)

## 入手元

SKK-JISYO.L は SKK 開発チームの辞書配布ページから入手できます:
<https://skk-dev.github.io/dict/>

（本リポジトリの `dic/upstream/SKK-JISYO.L` は取得済みファイルの置き場所
ですが、`.gitignore` により**リポジトリには含まれません**。数MBあり、
かつ上記URLから誰でも再取得できるためです。生成物の `out/FEP.DIC` 等も
同様にリポジトリには含めません。）

## つくし本体との関係

つくし本体(`TSUKUSHI.ASM` 等のソースコード)は [MIT License](../../LICENSE)
です。辞書データ(GPL)とは**別物**であり、同じフロッピーディスクや
配布物に同梱しても、それは単なる寄せ集め(mere aggregation)であって
つくし本体のライセンスが GPL に変わるものではありません。ただし、
GPL の辞書データを含んだ配布物全体としては、上記の GPL の条件
(著作権表示・ライセンス全文・変換元スクリプトの同梱)を満たす必要が
あります。
