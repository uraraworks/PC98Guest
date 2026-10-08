# CONHOOK (試作)

コンソール出力の流れ(INT 29h)を常駐メモリのリングバッファへ記録する TSR。
WebNP2 の `np2.onOutput(cb, { source: 'stream' })` が `readMemoryBase64` で読む。

    nasm -f bin CONHOOK.ASM -o CONHOOK.COM

- ヘッダはセグメント内 0103h(`CONH`、版、バッファサイズ 32768、書き込み総数 32bit)。
  ホストは割り込みベクタ 29h の指す先のセグメントからたどる。
- 常駐サイズは `mem /c` で 33,376 バイト(バッファ 32KB + PSP + コード)。
- 二重常駐は何もせず終了する。元の INT 29h へは jmp far でチェーンする。

## 測定(PROBE.ASM)

FreeDOS(98) で dir / type / mem / cls / FreeCOM のエコーが INT 29h と INT 21h(02/06/09/40)の
どれを通るかをバイト数で数えた(2026-10-08)。INT 21h AH=02/06/09 は 0 バイト、AH=40h(BX=1/2)
は INT 29h の部分集合(dir: 29h 1462 / 40h 1377、type COPYING: 29h 18772 / 40h 18333)。
つまり出力は全部 INT 29h を通る。FreeCOM の入力行エコーは AH=40h を通らず、
INT 29h に ESC[行;桁H つきで直接流れる(INT 21h フックだけでは取れない)。
