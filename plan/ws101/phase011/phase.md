<!-- awesome-plan project=zedbsd record=ws101p011 -->

# ws101-p011: L1 Noct の toolchain の差分と G3 の見本・測り方

Phase ID: `ws101-p011`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。L1: 5330 の passthrough で G3（N = 4,000,000）が CPU と全要素一致、GPU の dispatch 8 回、S13 の script が通る。Venus でも一致。**時間は GPU が CPU の 26 倍遅い**（CPU 10 ms、GPU 260 ms）: L2 の課題）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。D1・D2・D3・D5 はユーザーが決定）

## 範囲（[design.md](../design.md) §4、main の指示）

1. **toolchain の差分**（WS101 は変えず build もしない。main が当てる）: Noct の patch 0004、`userland/base/noct/Makefile`・`version.mk`・
   `zedbsd.cmake`、構成（demo と CI の amd64）の `ZEDBSD_NOCT_ACCEL := y`。共有の host の Noct（`build/NoctLang`）を作り直さない形。
2. **WS101 の分**: G3 の見本 `mix.nct`（N = 4,000,000、整数）、CPU と GPU の時間の測り方、offload の確認の方法、試験の script。
3. 1 ができたら main に報告（済み）。main が当てて build が通ったら、2 と p014（実機の G3）へ。

## 1. toolchain の差分（済み）

[`plan/ws101/p011-toolchain/`](../p011-toolchain/README.md): `0004-accel-opengles-on-zedbsd.patch`、`toolchain.diff`、`config.diff`、README（形、
当てる手順、当てた後の確認、作った側の確認）。要点:

- target だけの patch の一覧（`ZEDBSD_NOCT_TARGET_PATCHES` = 0001〜0004）と level（`ZEDBSD_NOCT_TARGET_PATCH_LEVEL` = `zedbsd12-t1`）。
  host の stamp・build は `zedbsd12` のまま（使い捨ての worktree で `make -n -p` を見て確かめた）。
- target の tree は同じ release の別の level なら展開し直す（他の worktree が merge 後に手で消さなくてよい）。host の tree は置き換えない。
- `ZEDBSD_NOCT_ACCEL := y` の amd64 だけ `-DNOCT_ENABLE_ACCEL=ON`、stamp に `-accel`、libEGL・libGLESv2 への依存と link。
- 確認: 0001〜0004 が検証済みの tarball に `--fuzz=0` で当たる、差分が `git apply --check` で当たる、使い捨ての worktree で target の tree の
  展開と level の置き換えが動く、accel の source が zedBSD の clang で `-fsyntax-only` を通る。link と実行は未確認（main の build）。

## 2. WS101 の分

main が差分を当て（commit 189c3bf1）、demo の構成の build（`build/demo-lcd9`）で accel の `/bin/noct`（NEEDED: libc・libEGL・libGLESv2）を
作った。WS101 の試験はその `/bin/noct` を image に写して使う（WS101 は Noct を build しない）。

| file | 内容 |
| --- | --- |
| `userland/desktop/gpudemo/`（新、package `gpudemo`、data、既定 n） | `mix.nct`（G3 の見本）と `s13.sh`（S13 の手順）を `/usr/share/gpudemo/` に置く。noct を require しない（image に noct を別に入れる構成で Noct の build を引き起こさないため。demo の構成は noct を選ぶ） |
| `mix.nct` | 整数の hash の DOALL（8 回の「乗算・加算」と「xor・論理 shift」、要素あたり 32 演算）。`noct -O2 -j --gc-tenure-size=100000000 [--gpu] mix.nct [N] [ROUNDS] [time\|notime]`。出力: 各 call の ms（zedBSD の `BeUI.getMilliseconds`）、最初と残りの中央値・最良、出力の checksum（重み付きの和と xor）、1001 要素の CPU の scalar の関数との照合 |
| `s13.sh` | CPU の run と GPU の run（各 6 call）を続けて走らせ、中央値の時間を並べ、速い方と倍率、結果の一致を出す。GPU の run は `KEI_GLES_COMPUTE_TRACE=1` で dispatch の数も出す |
| `userland/desktop/libglesv2/compute.c` | `KEI_GLES_COMPUTE_TRACE` を環境に置くと、dispatch ごとに stderr に 1 行（offload の証拠。既定では何も出さない） |
| `plan/ws101/tests/noct/g3-venus.sh` | Venus の guest に accel の noct と mix.nct を写し、`--gpu-list`・CPU の run・GPU の run（trace）を比べる |
| `plan/ws101/tests/hw/g3-hw.sh`・`g3/` | 5330 の passthrough: gles の image（glescompute 入り）に gpudemo と accel の noct（`NOCT`、既定は main の demo の build）を足し、compositor の下で s13.sh、CPU の run、GPU の run を順に走らせ、guest の disk の log で判定 |

### Noct の書き方で分かったこと（見本の形を決めたもの）

- 今の Noct（fcf5759e）の `__accel func` は**普通の名前で呼ぶ**（`Accel.call` は古い形）。buffer の引数は `rpackeduint32`（`_in`・`_out` は無い）。
  `--gpu` と最適化の水準 1 以上（`-O2`）で書き換えが試みられ、**断られると黙って CPU で走る**（`docs/accel.md`）。
- **loop の中の局所変数（`var`・`let`）があると GPU の書き換えは断られる**（Venus で小さな kernel を順に試して確かめた: 乗算・加算、xor、
  論理 shift はそれぞれ offload され、`var x = ...` を入れると dispatch が 0）。見本は `output[i]` を途中の値の置き場にした（CPU と一致）。
- Noct の整数は 32 bit で wrap し、`>>` は論理 shift（host の noct で確かめた）。GPU の結果も bit で一致した。
- zedBSD の Noct は `NOCT_MEMORY_SMALL` で GC の tenure が 1 MiB なので、16 MB の配列 2 つには `--gc-tenure-size` が要る（無いと
  「Out of memory」）。
- `--gpu-list` は `opengles:Kei OpenGL ES on Vulkan` を出す（Venus、i915 とも renderer の名は同じ）。

## 確認

| 確認 | 結果 |
| --- | --- |
| `plan/ws101/tests/noct/g3-venus.sh build/ws101-p011-venus`（Venus、N = 4,000,000、8 call） | **PASS**: CPU・GPU とも check 1001 wrong 0、checksum 一致（sum=-1291579143 xor=757251536）、GPU の dispatch 8（62,500 group ずつ）。時間は CPU 20 ms、GPU 826 ms（host の lavapipe、参考にならない） |
| `plan/ws101/tests/hw/g3-hw.sh build/ws101-p011-g3-hw`（**5330 の QEMU passthrough、i915**。lock 09:20:01〜09:26:13） | **PASS**: s13.sh が「results are the same」、mix.nct の CPU・GPU とも check 1001 wrong 0、checksum 一致（Venus と同じ値）、GPU の dispatch 8、compositor は各 run の後も生きていた |
| └ 時間（5330 の passthrough） | **CPU: 中央値 10 ms（9〜10）、GPU: 中央値 260 ms（255〜271、最初の call 365 ms）。GPU は CPU の約 26 倍遅い** |
| s13.sh の倍率の表示 | 実機の run の表示は「The GPU is 0.0 times as fast」だった（遅い側を考えていなかった）。後で「The CPU is 26.1 times as fast as the GPU.」と出す形に直し、awk の式を host で確かめた（実機では未再実行） |
| libglesv2 の build（trace の追加） | image の build で PASS（`-Werror`）。style-check 違反 0 |

## 分かった課題（L2 へ）

- D3 の 3 倍（GPU ≤ CPU / 3）には、GPU の 1 call を 260 ms から約 3 ms へ（約 80 分の 1 に）する必要がある。CPU の 10 ms は予想より速い
  （Noct の JIT と SIMD。4,000,000 要素 × 32 演算）。
- GPU の 260 ms の内訳は未測定（p016）。見込み: 1 call ごとの 16 MB の upload（CPU の写し → device の写し）と 16 MB の読み戻し。i915 の実行器は
  buffer を uncached（MOCS）で、host の写像も cache されない memory なら、CPU の 16 MB の読み（`gles_buffer_fetch` の memcpy）だけで
  100 ms を超えうる。GPU の計算そのもの（80 EU）は数 ms の見込み。
- **D3 の見直しが要るかもしれない**（design は「CPU の時間を測ってから決め直す」）。p016 で内訳を測ってから、main とユーザーに出す。

## 未実施

- 素の 5330（L2・L3）。
- demo の image への gpudemo の追加（main の構成: `plan/ws075/demo/config-demo-hdmi.mk` の `ZEDBSD_USER_PROGRAMS` に `gpudemo`）と、Terminal
  からの S13 の手の操作。
- s13.sh の直した表示の実機での再実行。

## 注意（toolchain の規則）

- 最初の g3 の image の build で、`gpudemo` の require に `base/noct` を書いていたため、**この worktree の target の Noct（`userland/base/noct/noct`、
  checkout の中）が展開と build をされた**（accel なし、`zedbsd12-t1`）。共有の `build/NoctLang`・`build/host-noct-state` は触れていない
  （stamp の時刻が 2026-09-23 のまま）。require から `base/noct` を外し、試験の `/bin/noct` は main の build のものだけを使う形に直した。
