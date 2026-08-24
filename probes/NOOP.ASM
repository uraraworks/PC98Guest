; NOOP.COM - 何もせず回り続けるだけのプログラム
;
; 目的:
;   キーを1つも消費しない状態を作る。DOS のプロンプトのままだと COMMAND.COM が
;   キーを読んでしまい、キーバッファの中身を観測できない。
;   このプログラムを走らせておいて、ホスト側から
;   np2.readMemoryBase64() でメモリの差分を取る。
;
; 割り込みは動き続けるので、キーボードIRQによるバッファへの蓄積は起きる。
;
; 終了できない(page reload でリセットする)。
;
; ビルド: nasm -f bin NOOP.ASM -o NOOP.COM
        cpu     8086
        org     0x100
start:
        jmp     start
