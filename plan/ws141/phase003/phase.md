<!-- awesome-plan project=zedbsd record=ws141-p003 -->

# ws141-p003: display（N0 → N1 → N2 → P1 → P2 → P3 → P5、P4 は後）

Status: planned（Queue は Q1 が割り当てる）
Disposition: normal
Parent: [WS141](../ws.md)
Queue: none
依存: [p002](../phase002/phase.md)（骨格・段の印・P0。p002 の QEMU の回帰と実機の P0 の写真が先にあると安全）
実行者: phase-runner（high）

## 範囲（[design](../rpi4-gpu-design.md) §3.1・§10 の p003）

段ごとに実機の写真で確かめながら進める。各段は `rpi4gpu.stop=<段>` で止められ、危ない書き込みの前に begin の行と 3 秒の待ちを出す（p002 の helper）。register の名前は `plan/ws141/temp/rename/rename-map.tsv` の独自の名前で書く（GPL の名前を写さない）。

1. **N0**（読むだけ）: mailbox の get の tag だけで firmware の framebuffer（物理・pitch・幅・高さ）と clock（core・HDMI の 2 つ）。HDMI の clock が 0 なら HVS より先は読まず「display 無し」で抜ける。0 でなければ HVS の全体の enable、出力の切り替え、channel の enable・幅・高さ・状態・次と今の display list の位置、display list の解読（plane の数・format・位置・大きさ・pointer・pitch・end）、pv2・pv4 の enable と timing を読み、80 桁の複数の行に出す。
2. **N1**（画面を消さない引き継ぎ）: firmware の display list の word をそのまま写した list を、firmware の list と filter の係数を避けた領域に書き、読み返して一致を確かめ、channel の「次の list」に入れる。「今の list」が自分の位置になるのを時限付きで poll。
3. **N2**（N1 の直後）: firmware に display の終了を通知（mailbox、判断の項目 17 で値は事実として使う）。前後で HVS・pv2/4・display list・clock・framebuffer の memory と mailbox の答えを読み比べる。変わったら止まって記録する（設計を見直す条件、判断の項目 16）。
4. **P1**: pixelvalve の vblank と HVS の underrun の割り込み（p002 で登録した masked の handler を本物にして unmask）。1 秒あたりの vblank の数。
5. **P2**: 同期の page flip（driver が持つ 1 GiB より下の連続の 2 buffer、cache の clean、display list の切り替え、vblank で完了）。
6. **P3**: plane の合成（console の plane の上に CPU で埋めた plane、console の領域の外、背景の fill）。core clock の underrun の対処（判断の項目 15）。
7. **P5**: resident display（`drv_gpu_display_ops`）の統合と display の device の登録（`drv_gpu_register`、display の役、companion は V3D の device）。cap の bit は gpu.c の検査に従う（design §4）。

範囲の外: P4（HDMI の mode set、H5〜H10）、EDID（判断の項目 14、`rpi4-firmware.c` の拡張が要るときは最小で、既存の呼び手の挙動を変えない）、V3D（p004）、`include/hal/hal.h`（判断の項目 16 で要るなら差分を plan に置いて止める）。

## 受け入れ

- 各段の build（rpi4、warning 0）と、変えた所の host の試験（display list の組み立て・解読、timing の読み取りの計算）。
- QEMU（Q1 経由で T1）: raspi4b で boot が壊れない（HVS の register は QEMU で 0 を読むので N0 で「display 無し」に抜ける見込み、未観測）。
- 実機（ユーザー、判断の項目 6）: 段ごとの写真で印と画面（N1・N2 で画面が変わらない、P1 の vblank の数が 60±1、P2 の flip、P3 の重なり）。
- `rename-map.tsv` の旧名で driver の source に一致 0。

## 実機の手順

（p003 の実装のときに書く。p002 と同じく HDMI0 の画面の写真。serial は画面が消える段だけ。）

## 結果

（未着手）
