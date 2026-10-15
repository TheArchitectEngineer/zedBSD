<!-- awesome-plan project=zedbsd record=ws245-p002 -->

# ws245-p002: 土台（register と表の header、xd-io、xd-platform、host 試験の枠、own の型の分離、Gen9 の own への移設）

Status: cleared（2026-10-15 Q1: (a)〜(e) は P1 の host 試験・build、(f) は T1-699（T2、QEMU の Venus の image、boot-test.sh で login prompt、main 6d07bee3d）PASS。(g) の実機（Gen9 の機械は §7 #22 の回答待ち、5330・5320）は未実施でユーザーの試験の一覧に。main の merge a9925a4db）
Parent: [WS245](../ws.md)
Queue: q1204 / q1204-i01（P1、worktree p1、branch agent/p1-ws245、基点 main 911b7e178）
Approval: 2026-10-15 夕 ユーザー「WS245は、ロールバックできる体制にしておいて、今から実装します。」

## 範囲

[design.md](../design.md) §6.2 の p002 の行（受け入れ条件 (a)〜(g)）。§7 の判断は既定の案。ただし #18（RKL・ADL-S/RPL-S を支えるか）と #22（Gen9 の機械）は回答待ちなので、`device.c` の PCI ID の表には RKL・ADL-S/RPL-S を**足さない**（今の振る舞いのまま。xd-platform の記述の表には持つ）。(g) の実機は一覧に載せるだけ。

ロールバックの体制（[ws.md](../ws.md)「実装の開始とロールバックの体制」）: kernel の振る舞いを変えない。xd の file は `AMD64_I915_SOURCES` に入れて compile するが呼ばれない。own の型の分離と Gen9 の own への移設は振る舞いを変えない移動。

## 結果（2026-10-15、P1）

commit（branch agent/p1-ws245、どれもメッセージは WIP）:

| commit | 内容 |
| --- | --- |
| d8134ee40388f82282ea468f61d26f3170e8dbbf | own の型の分離（`display-internal.h`）と Gen9 の own への移設（Q1 が main に merge: 9e78e62f8） |
| 5084f18520a922ff178791afa7edcc2abf063464 | register の header（north・south）、`xd-io`、own の register の model、host 試験の枠、Linux との照合の道具（Q1 merge: 3fdaf59dc） |
| 3d476ca4368da729e2d15650a2229c34c4a6854c | 表の header、`xd-platform`、platform の host 試験、PHY の DCC・display 12 の chicken の register |
| 8e62fb89d7d933fd7e0527a608d9c2191f906c04 | 表を Linux の file と比べる道具 |
| （この phase.md・ws.md の commit） | 記録 |

作った file（行数）:

- `src/drivers/gpu/i915/display/display-internal.h`（500）: own の型。`i915_display_mode`（旧 `i915_lcd_mode` と同じ layout）、PCH の enum と `i915_pch_state`・bridge の walk、`i915_scanout`、`i915_irq_vblank`、`i915_display_irq`、`i915_pw_irq_ops`、`i915_resident_display`、`i915_display_refresh`、`i915_present_window`。`internal.h` はこれを include し、旧名は `#define i915_lcd_mode i915_display_mode`（旧 code と WS051・WS075 の host 試験のため。p013 で消す）。
- `display/gen9-firmware.c/.h`（368・26）: Gen9 の N0-lite（`drv_i915_gen9_firmware_check`、log の文言は同じ）。
- `intel/display-regs-xd.h`（1154）・`display-regs-xd-south.h`（208）: §5.2 の register の群（p004〜p008 が使う物。VBT・OpRegion・DPCD の layout は除く: 下の「設計との差」）。block ごとに複数行の Values referenced:。
- `intel/display-tables-xd.h`（2636）: 表のデータ（swing 21 表と platform ごとの組、combo DPLL の DP の語 2 表、TBT PLL、CDCLK 4 表、voltage 2、DBUF 2、DBOX 2、buddy 2、procmon、stepping 8、PCH ID、PCI ID 9、well 4、port 4、PLL 4、platform の記述 9 行）。
- `display/xd-io.c/.h`（578・165）: read/write/rmw/posting read/poll/delay、書き込みと step の記録（256 の ring）、GT driver mailbox の 1 command（Procedure referenced: intel_pcode.c）、lock の順の hook、field の helper。
- `display/xd-platform.c/.h`（341・445）: 記述の型と定数、`drv_i915_xd_platform_choose`（PCI ID → 行、revid → stepping、PCH ID → 南の種類。PCH が無い・知らない ID（VM）は platform の物と仮定、知っている別の種類は EOPNOTSUPP）、表の lookup（DPLL、TBT PLL、procmon、buddy、port）。
- 試験: `tests/display/xd-fake-regs.c/.h`（516・47。own の register の model: 値・時計・mirror/call の rule・遅延の変化・lock の順の記録。Linux の型を使わない）、`xd-regs-map.h`（3029。1002 組の (xd, Linux) の対）、`host-xd-regs-test.c`（99）、`host-xd-io-test.c`（360）、`host-xd-platform-test.c`（409）。
- `plan/ws245/tests/`: `run-xd-host-tests.sh`（ASan/UBSan）、`check-xd-regs-linux.sh/.c` と `linux-stub/linux/*.h`（Linux v6.8.12 の register header と直に比べる）、`check-xd-tables-linux.py`（表を Linux の .c と比べる）。

変えた file: `internal.h`（own の型を移し include と別名、−463）、`takeover.c/h`（`drv_i915_n0_lite_gen9`・`drv_i915_n1_console_fb` と Gen9 の define を削除、−187。`drv_i915_adjust_wm_latency` は takeover と ktest が使うので残す）、`device.c`（N0-lite の呼び先と include）、`bootfb.c`（`kern_boot_handoff` を直に）、`gen9-outputs.c`（latency の調整を自前の段に、`takeover.h` を外す）、`gen9-node.c`（`takeover.h` を外す）、`gen9-mode.h`・`scanout.h`（`display-internal.h` に）、`gen9-*.c/h` の mode 型の名前、`platform/amd64/vmunix.mk`（`gen9-firmware.c`・`xd-io.c`・`xd-platform.c`）。

## 受け入れ条件ごとの結果

| 条件 | 結果 |
| --- | --- |
| (a) register の offset が Linux の値と一致 | **PASS**。`host-xd-regs-test`（旧 `intel/*.h` の define と比べる）495 検査 0 失敗。加えて `check-xd-regs-linux.sh`（Linux v6.8.12 の header そのものと）1002/1002 組一致、`check-xd-tables-linux.py`（表 53 個を Linux の .c と）全部一致 |
| (b) xd-io の poll/trace/mailbox | **PASS**。`host-xd-io-test` 48 検査 0 失敗（access、poll の即時・遅れ・timeout・0 µs、記録の順と ring の巻き、mailbox の応答・busy・拒否（EBUSY・ENXIO・EINVAL・EACCES）・timeout、lock の順の違反の検出） |
| (c) platform の選択（9 行の代表、revid、PCH、RKL・ADL-S が GT を起こさない） | **PASS**。`host-xd-platform-test` 80 検査 0 失敗。今の driver が受ける ID（`intel/pci-ids.h` の TGL と ADL-P 系）は全部 verified の行に、verified の行の ID は全部受ける ID に、RKL・ADL-S・RPL-S の ID は受ける ID に無い（= `device.c` は GT を起こさない。#18 の回答まで表は変えない）。5330 の eDP の HBR@38.4 MHz で CFGCR0 0x00e001a5・CFGCR1 0x88（dump と一致） |
| (d) build warning 0、`git diff --check` | **PASS**。`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix` rc=0 warning 0（vmunix check PASS）、`BUILD=build/ws245-p002/i915tests I915_TESTS=y` でも rc=0 warning 0。`git diff --check` 0 |
| (e) 移設した関数の既存の試験 | **PASS**。host-gen9-outputs 29/0 失敗（latency の 3 検査）、host-bootfb 20/0、host-native-decide 14/0 |
| (f) QEMU（Venus の image の起動、T1 の boot-test.sh） | **未実施**（T1 に依頼する。下の案） |
| (g) 実機（Gen9 の機械の起動と display） | **未実施**（一覧に載せるだけ。機械の名前は §7 #22 の回答待ち） |

## 設計との差（判断と理由）

1. **#18**: `device.c` の ID の表に RKL・ADL-S/RPL-S を足さない（Q1 の指示）。xd-platform の記述には `verified = 0` の行として持つ。
2. **design §4.1 (c)**: `gen9-node.c` の `drv_i915_delayed_*` は own の `../workqueue.c`（Zlib、Linux 由来の表示なし）を直に呼んでおり、`hotplug-internal.h` を経由していない。移設は不要（Q1 了承）。
3. **N0-lite の置き場所**: design は `display.c` だったが、Gen9 だけの段なので新しい `gen9-firmware.c` に置いた（`display.c` は p003〜p013 で書き換わる）。`display->n0` には書かない（Gen9 では `absent` で以後の段を飛ばすので誰も読まない。log は同じ）。
4. **mode の型**: `struct i915_display_mode` は今の `i915_lcd_mode` の layout と field の名前のまま（design §4.3 の `h[4]/v[4]` の案は採らない: 振る舞いを変えない移動にするため）。旧名は `internal.h` の別名で残す。
5. **`struct i915_display`** は Linux 由来の型を埋め込むので `internal.h` に残した。own の file（bootfb.c・gen9-node.c・scanout.c 等）は p003/p013 まで `internal.h` を include し続ける。`gen9-mode.h`・`scanout.h` だけが `display-internal.h` に移った。
6. **表の型の置き場所**: 型と定数は `display/xd-platform.h`、データは `intel/display-tables-xd.h`（`xd-platform.c` だけが include。使わない TU で static の表が未使用の警告にならないように）。他の xd の file は記述の pointer と lookup で表に届く。
7. **p002 に入れなかった表**: VBT・OpRegion・DPCD の layout（design §3.1 の `xd-vbt-layout.h` 等、p004・p005 で）、Dekel の TC PLL の算法の定数（表 T4 の TBT 以外、p006）、timeout の表 T8（使う module で）。HPD の pin（H1）は port の記述と register の define で持つ。
8. **Alder Lake-N の ID**: Linux v6.8.12 の 3 つに加えて、今の driver が受ける 0x46d3・0x46d4（tree の `intel/pci-ids.h`）を持たせた（受ける ID を減らさない）。
9. **Tiger Lake の UY 行**: Linux v6.8.12 は GT2 の ID（5320 の 0x9A49 を含む）を UY とし、swing の HBR2 表と stepping を変える。決定 4（Linux を優先）でそのまま持つ。
10. **機械で照合していない表**: well・port・PLL・platform の行は Linux の構造（関数・初期化子）から読んだ物で、`check-xd-tables-linux.py` の対象外。`host-xd-platform-test` の形の検査と代表値（PW_A は pipe A だけ、PW_2 は AUX USBC1 を持つ、USBC の AUX は 500 ms、TC2 は Dekel PHY G 等）で確かめた。p006 の受け入れ (a)（W3 と 5330 の `dbg-i915_power_domain_info.txt`）で再び照合する。

## 出典の書き方の確かめ

- 新しい file の先頭の comment に出典・派生の宣言・Linux の MIT の表示は無い（確かめた: 新しい file の先頭の comment に `Linux`・`referenced`・`derived` が無い。試験の道具の説明は「何と比べるか」を書くだけ）。
- 値の block ごとに複数行の `Values referenced: Linux v6.8.12 <path> (<名前>)`: regs 48 block、south 8、tables 24、xd-platform.c 3（Procedure referenced）、xd-io.c 2、gen9-firmware.c 2、gen9-outputs.c 1、display-internal.h 1。
- Linux の symbol の名前は使っていない（`I915_XD_*`・`i915_xd_*`・`drv_i915_xd_*`）。表の struct の field の並びは自分で決めた（combo の swing は post cursor 1 → 2、Dekel は de-emphasis → preshoot、DPLL は CFGCR1 の register の順、CDCLK は ref → cdclk → ratio → divider）。
- Gen9 の移設の段は Procedure referenced（skl_watermark.c の adjust_wm_latency）を付けた。VT-d の値は Intel の VT-d の仕様と datasheet を出典にした（Linux に無い）。

## 実行したコマンド

```
make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix                     # rc=0, warning 0（3 回: commit ごと）
make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws245-p002/i915tests I915_TESTS=y vmunix   # rc=0, warning 0
sh plan/ws245/tests/run-xd-host-tests.sh build/ws245-p002/host                 # regs 495/0, io 48/0, platform 80/0
sh plan/ws245/tests/check-xd-regs-linux.sh                                     # 1002/1002
python3 -I plan/ws245/tests/check-xd-tables-linux.py                           # 53 表 equal
sh plan/ws237/tests/run-gen9-outputs-host-test.sh build/ws245-p002/gen9-outputs  # 29/0
sh plan/ws229/tests/run-bootfb-host-test.sh build/ws245-p002/bootfb            # 20/0
sh plan/ws084/tests/run-native-decide-host-test.sh build/ws245-p002/native-decide # 14/0
git diff --check                                                               # 0
```

使い捨ての出力は worktree p1 の `build/ws245-p002/`（Q1 が掃除してよい）。

## T1 への依頼の案（(f)）

- 目的: own の型の分離と Gen9 の移設、xd の file の追加（呼ばれない）で、i915 の無い image の起動が壊れていない事。
- image: 試験用の config は作らない（kernel の振る舞いは変わらないので、T1 の標準の Venus の image の config で、この branch の main への merge 後の tree を build）。
- 確かめ: `plan/tools/boot-test.sh` で login prompt（PNG）。
- 期待: 今と同じに login prompt。QEMU の証拠として記録（実機の証拠ではない）。

## 実機の一覧（(g)、ユーザーの確かめ。今すぐは頼まない）

- Gen9 の機械（どれかは §7 #22 の回答待ち）: 起動して firmware の画面のまま desktop が出る（`i915: N0-lite (gen9): …` の log が今と同じ）、bootfb / gen9 の display（i915.gen9.display= の既定）が今と同じ。
- 5330（ADL-P）・5320（TGL）: p002 は振る舞いを変えないので必須ではないが、次に起動した時に今と同じ（display 12・13 の旧 code の道に入る）。

## 残り

- (f) の T1 の結果と Q1 の判定（cleared はその後）。
- §7 #18（RKL・ADL-S を支えるか）と #22（Gen9 の機械）の回答。
- p004・p005 の VBT・OpRegion・DPCD の layout の header、p006 の Dekel PLL の定数と timeout の表（上の「設計との差」7）。
- `plan/ws245/tests/` の試験と道具を Master の Tools に登録するのは p013 の clearance の時（design §6.3）。
