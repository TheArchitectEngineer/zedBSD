<!-- awesome-plan project=zedbsd record=ws084-guide -->

# WS084 の作業の手引き（2026-10-01）

WS084（i915 の firmware の画面の引き継ぎ: 素の 5330 を UEFI で起動したとき GOP が点けた pipe A を N1 で止めて driver のものにし、LCD に desktop を出す）を、
追加の調査なしで続けるための手引き。正本は [ws.md](ws.md) と各 `phaseNNN/phase.md`。食い違ったら ws.md・[master](../master.md)・[AGENTS.md](../../AGENTS.md) が優先する。
2026-10-01 の survey（file・git の履歴の読み取り、`make -n`）で確かめた。実機・QEMU は動かしていない。

## 1. ゴール

| 項目 | 内容 |
| --- | --- |
| 完了の条件（ws.md） | 素の 5330 を USB の image から UEFI で起動し、firmware の画面を引き継いで greeter（または自動の login の desktop）が内蔵の LCD に出る |
| 段 | L1（済み、2026-09-29 demo-lcd3）: 素の 5330 で firmware の画面から Kei の LCD へ。**L2: demo の image（最新）の起動が 10 回中 10 回、黒・固まりなく届く** |
| デモの場面 | fg010 の **S1（起動: 電源から Kei の起動画面、greeter）**。全ての場面の前提（素の 5330 の LCD に出ること） |
| WS の完了の宣言 | L2（p003）と乖離の整理（p004）の後、ws.md を完了の形に。code を変えた場合は規約の全文との照合（Awesome Plan の conformance）を足す |

### ユーザーの判断

| 日付 | 判断 | 意味 |
| --- | --- | --- |
| 2026-09-29 | 「ではWSを立ち上げて実装してください。display takeoverは以前に実験して動いた実績があり…」 | この WS の始まり（main が実装） |
| 2026-09-29 | 「HDMIはいったんやめて、LCDのみの構成にします。」 | demo の既定 `display=edp`（`plan/ws075/demo/config-demo-hdmi.mk`）。HDMI の LCD は WS075 の範囲で、この WS の条件の外 |
| 2026-09-29 | demo-lcd3 で「完璧です。」 | p001・p002 cleared、L1 |
| 2026-09-29 | 表示の build: demo・実機は既定（logo、`kmsg=quiet`）、GPU の driver を直す Phase だけ logo を消す | §5 の image の A・B |
| 2026-09-29 夜 | 10-10 ごろ以降は bug の修正と実機の調整だけ | p004 の直しは 10-10 より前に main が決める。それ以降は失敗の修正だけ |
| master の優先順位 | WS084 は「デモ critical の残り」の先頭 | 上位の WS の後 |

## 2. 今の状態

### 済み（証拠）

| Phase | 内容 | 証拠 |
| --- | --- | --- |
| p001 | N0 が active な pipe で止まらず `takeover` の印、resident の run の開始で readout → takeover → release | `display/takeover.c:2619`（`drv_i915_native_decide`）、`display/modeset.c:3660`（`i915_resident_takeover`）、`:1734` から呼ぶ |
| p002（4 回） | well の sanitize を残す・INIT の参照を遅らせる（`display.c:2278`）・combo PHY の `icl_ddi_combo_get_config` を結ぶ（`ddi.c:519`）・`icl_set_active_port_dpll` の 2 行（`ddi.c:3949` の `i915_ddi_get_clock`）・RPS（後に ws075-p020 で busy の評価に） | **素の 5330（demo-lcd3、ZEDBSD_GRAPHICAL_BOOT=n）**: takeover rc=0、preflight 通過、picture up、24.5 present/s、ユーザー「完璧です」 |

### 開いているもの

| 物 | 状態 |
| --- | --- |
| L2（10 回の起動） | 未着手 → [p003](phase003/phase.md)（planned）。**既定の graphical boot の image の素の 5330 での takeover は未確認**（demo-lcd2・lcd3・lcd8 は logo 無効） |
| parity との乖離 1〜3 | 未整理（今は実害なし）→ [p004](phase004/phase.md)（planned） |
| host 試験 `host-native-decide-test.c` | 期待値は p001 で直したが未実行（build の道具が WS031 の片付けで消えた、`1e867fbf`）→ p004 の手順 3 |
| kernel 内の試験（`tests/display/hpd-ktest.c`、`tests/execution/ktest-gt.c`） | build の道具が無く未実行 |
| HDMI の起動時の未接続（rc=13） | この WS の外（WS075） |
| RPS（F-054） | ws075-p020 で cleared（passthrough）。素の 5330 は未実施 |

bug: この WS に固有の ticket は無い（[Bug Board](../known-bugs.md) に WS084 の行なし、2026-10-01）。関係するもの: BUG-092（sessiond が GPU の node を待つ、resolved）、
BUG-117（素の 5330 での確認が残り、WS075）。

## 3. 次の作業の順番

| 順 | Phase | 目的 | 条件 |
| --- | --- | --- | --- |
| 1 | [ws084-p003](phase003/phase.md)（planned） | L2: demo の既定の image で素の 5330 の 10 回の起動、`plan/ws084/tests/reboot-loop.sh` | ユーザーが USB の起動と最初・最後の目視をできるとき。ws075 の提案 p033（素の 5330 の S11）と同じ機会にまとめてよい |
| 2 | [ws084-p004](phase004/phase.md)（planned） | 乖離 1〜3 を調べて記録、直しの案を patch に（適用しない）、host-native-decide-test を戻す | p003 の dmesg を入力にする。source は変えない |
| 3 | 提案 ws084-p005 | p004 で main が「直す」と決めた乖離の実装、素の 5330 で p003 の 10 回をやり直す | main の判断の後、10-10 より前。GPU の driver の Phase なので image は B（§5）から始め、最後に A で 10 回 |
| 4 | 提案 ws084-p006 | WS の完了: 変えた source（`display/takeover.c`・`modeset.c`・`display.c`・`ddi.c`・`output.c`・`hotplug.c`・`gt-power.c` の WS084 の差分）を [coding-style.md](../coding-style.md) の全文と照合、build（warning 0）、boot test、ws.md を完了の形に | p003〜p005 の後 |

p003 で失敗した回があれば、その回の dmesg の行（下の §4 の印）で原因を分け、修正の Phase を main が立てる（p004 の前でもよい）。

## 4. 未知と調べ方

| 未知 | なぜ要るか | 調べ方 |
| --- | --- | --- |
| K1 graphical boot（loader の splash が GOP に描いた後）での takeover | demo の既定は logo あり。今までの素の 5330 は logo なしだけ | p003 の 1 回目の dmesg: `i915: N0 decision:`（`display/takeover.c:2826`）、`i915: takeover: readout:`（`display/modeset.c:3703`、DPLL と clock が 0 でない）、`i915: takeover: rc=`（`:3717`、`still active 0x0`）、`i915: resident display: picture up`（`:3457`） |
| K2 起動ごとのばらつき | L2 の 10/10 | p003 の `reboot-loop.sh` の各回の dmesg を同じ grep で並べる。`takeover: power_domains_enable: wells_on A -> B`（`modeset.c:1748`）の値が回ごとに同じか |
| K3 takeover の後の preflight（乖離 1） | 止まり切らない pipe で再発し得る | 拒む log `i915: LCD-B preflight: the display is not idle (TRANSCONF ...)`（`modeset.c:2011`）と `resident display: not started (preflight...)`（`:1773`）。p004 |
| K4 INIT の参照の返却（乖離 3）と DC state | 再点灯の前に DC state に入る危険 | `power well DC_off state mismatch` の有無と `wells_on`。`display/power.c:1455-1465`、DMC は `display/dmc.c`。Linux の参照 `plan/ws031/linux-parity/linux-reference/i915-src/display/intel_display_power.c`（`intel_power_domains_enable`）と `intel_modeset_setup.c` |
| K5 probe の sanitize（乖離 2） | readout の前に firmware の状態を変えていないか | `display/takeover.c:2141`（`drv_i915_modeset_sanitize_hw_state`）、`:5552`・`:5577`・`:2256`・`:5652`。parity: `git show 8022d26f:src/drivers/gpu/i915/parity/lcd/parity_modeset_setup_glue.inc` |
| K6 PLL の readout の経路 | p002 の 3・4 回目の fault（NULL の `shared_dpll`） | `display/ddi.c:519`（`drv_i915_lcd_ms_bind_readout`）、`:3949`（`i915_ddi_get_clock`）、`:3976` の注記。Linux `linux-reference/i915-src/display/intel_ddi.c` の `icl_ddi_combo_get_config`・`icl_set_active_port_dpll`。`takeover-internal.h:356` に未移植の step の名前（`I915_TAKEOVER_ICL_SET_ACTIVE_PORT_DPLL`）が残る |
| K7 reboot と firmware の起動順 | p003 の自動化 | 1 回目の reboot で ssh が戻るか。zedBSD の reboot は `userland/base/reboot`（`shutdown-control/main.c`）。戻らなければユーザーに画面を聞き、電源で 10 回に切り替える |
| K8 bare metal の遅さ（再発時） | 2026-09-29 の 1 fps の類 | `dmesg | grep 'i915: perf:'`（`perf.c:188`: presents/s、submit ごとの GPU ms）、`dmesg | grep 'i915: rps'`、入力は `plan/ws084/tests/evlat.c`（evdev の event の時刻、`evlat DEVICE SECONDS`。guest で動かすには image に入れる build が要る、その build の方法は記録に無い＝未確認） |

parity の資料: `git show 8022d26f:<path>`（`parity_lcd_kernel.c`・`parity_modeset_setup_glue.inc`・`display_nogem.c`・`driver_probe.c`・`probe.c`）、
`git show 4ab09939:plan/ws031/handover/notes/n1-implementation-state.md`（停止要因 1〜7）。Linux の参照は repo の中（`plan/ws031/linux-parity/linux-reference/`、manifest は Linux 6.8 の
固定の環境）。Linux の 5330 での display の状態の記録: `plan/ws031/display-ref/`（`dbg-i915_display_info.txt`・`dbg-i915_shared_dplls_info.txt`・`dbg-i915_power_domain_info.txt`）。

## 5. コマンド

全て repo の root から。一般の build・回帰は [plan/ws104/commands.md](../ws104/commands.md) の §0・§1・§4。**image の build は同時に 1 つ、BUILD は Phase ごとに別。**

### 5.1 kernel だけ（driver の compile の確認）

```
mkdir -p build/<W>
make -j64 BUILD=build/<W> ZEDBSD_CONFIG=plan/ws075/demo/config-demo-hdmi.mk build/<W>/vmunix > build/<W>/kernel.log 2>&1; echo "make exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/<W>/kernel.log | wc -l
```

（`make -n` で target を確かめた、2026-10-01。素の 5330 の image なので `I915_TEST_VBT` は付けない: VBT は機械の OpRegion から、`build-demo-image.sh:15-17`。）

### 5.2 素の 5330 の image（`plan/ws075/demo/build-demo-image.sh`、`passthrough` を付けない）

| 形 | コマンド | いつ |
| --- | --- | --- |
| A. demo の既定（logo、`kmsg=quiet`、`display=edp`） | `plan/ws075/demo/build-demo-image.sh build/<W>-usb` | L2・デモ |
| B. GPU の driver を直す Phase（logo なし、kernel の message を画面に） | `plan/ws075/demo/build-demo-image.sh build/<W>-usb ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"` | 直す途中 |

- **A に `login=graphical` を足さない**（graphical boot が既に入れるので重複し、kernel が boot の parameter を拒んで止まる）。B は graphical boot を切るので
  `login=graphical` が要る（無いと greeter が lease を取らず takeover が走らない、ws.md の p002 の 4 回目）。
- 出力 `build/<W>-usb/hdd-image.img`。ユーザーが USB に書く。ESP の `zedbsd.cfg` で boot の parameter を後から変えられる（`build-demo-image.sh:13-14`）。
- build の warning: commands.md §1 の grep を build の出力に。

### 5.3 QEMU の回帰（takeover の経路は通らない）

```
cp build/<W>-usb/hdd-image.img build/<W>/boot-copy.img
OUTPUT=build/<W>/boot plan/tools/boot-test.sh build/<W>/boot-copy.img; echo "exit=$?"
```

PASS は `exit=0` と `boot-test: PASS build/<W>/boot/login.png`。PNG をユーザーに見せる。QEMU には firmware の画面が無く N0 は active な pipe を見ない。
passthrough（solaris10-man の QEMU）も同じで、**takeover の経路は素の 5330 でしか確かめられない**（ws.md の p001・p002）。

### 5.4 host の試験

| 試験 | コマンド | 状態 |
| --- | --- | --- |
| 出力の選択（`display=`・EDID の再試行） | `sh plan/ws075/tests/hdmi/host-output-test.sh build/<W>/h2` | 2026-10-01 PASS（`host-output-test: 80 checks, 0 failures`、約 1 秒） |
| RPS の contract | `sh src/drivers/gpu/i915/tests/contracts/run.sh rps` | 2026-10-01 PASS（74 checks、約 3 秒） |
| N0 の判定（`host-native-decide-test.c`） | p004 の手順 3 で道具を戻してから `sh plan/ws084/tests/run-native-decide-host-test.sh` | 未実行（link の不足が既知） |

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（別の agent が作成中。機械と host・passthrough の lock・USB の単独の起動・demo の image の節）。ここは WS084 に固有の所だけ。

- **この WS の実機は素の 5330 だけ**（ユーザーが USB から UEFI で起動）。passthrough（`plan/ws075/tests/hdmi-h4-hw.sh`、`flock /tmp/i915-hw.lock`）は firmware の画面が無いので
  takeover を通らない。passthrough は「takeover と無関係な経路（入力の遅れ、GPU の時間）」を切り分けるときだけ（2026-09-29 の `h4-ctl.py latency A 10` の形、
  [WS075 の guide](../ws075/guide.md) §6）。素の 5330 で起動している間は passthrough の host（solaris10-man = 10.0.10.25）が居ないので、passthrough の lock を取る他の agent の試験と
  時間が重ならないよう main が調整する。
- 素の 5330 への ssh（image の root に `plan/tmp/guest/id_ed25519.pub`、`build-demo-image.sh:42`）:
  ```
  ssh -i plan/tmp/guest/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@<IP> dmesg > build/<W>/boot.dmesg
  grep -E 'N0 decision|takeover:|resident display|pipe_off|LCD-B preflight|DC_off|i915: perf:' build/<W>/boot.dmesg
  ```
  IP は DHCP で変わる（2026-09-29 は 10.0.30.5）。毎回ユーザーに聞く。main の端末から届かないことがある（2026-09-29 の 10.0.30.3 は No route to host）。そのときはユーザーに dmesg を頼む。
- 読み方:

  | 行 | 正常 | 異常のとき |
  | --- | --- | --- |
  | `i915: N0 decision: PROCEED -- ... (active pipes 0x1 ...)` | PROCEED、pipe A が active | STOP: 他の条件（GGTT の重なり、VT-d、pipe の読み取りの誤り）。`takeover.c:2619` |
  | `i915: takeover: readout: ... DPLL1 ... <clock> kHz` | DPLL と clock が 0 でない | 0 kHz: PLL を読めていない（p002 の 2 回目、`ddi.c`） |
  | `i915: takeover: rc=0, crtcs stopped 1, still active 0x0` | rc=0、still active 0 | `pipe_off wait timed out`・`Timeout waiting for DDI BUF to get idle`: clock か well を失った（p002 の 1・2 回目） |
  | `i915: takeover: power_domains_enable: wells_on A -> B` | 起動ごとに同じ値 | 再点灯の失敗と一緒なら乖離 3 |
  | `i915: resident display: picture up` | ある | `not started (preflight...)`: 乖離 1 |
  | `i915: perf: N ms: M presents (X/s) ...` | 操作中 20/s 前後（demo-lcd3 で 24.5/s） | 数/s: GT の周波数（`i915: rps` の行）を見る |

- 画面が GOP のまま固まり ssh も届かない（2026-09-29 の demo-lcd1）: takeover の前の fault。image B（kernel の message を画面に）で作り直し、ユーザーに画面の写真を頼む。
- 証拠は「素の 5330」と書き、QEMU（boot test）と分ける。ユーザーの目視・写真は誰が見たかを書く。

## 7. 注意

- **HAL**（`include/hal/hal.h`）・**UAPI**（`include/drivers/gpu.h`・`include/uapi/gpu*.h`）の変更は差分ごとに事前の承認。takeover の修正は `src/drivers/gpu/i915/display/` の中で済むはず。
- **toolchain** を変えない・build しない。lock（`plan/tools/toolchain-lock.sh`）を外さない。
- **実機は同時に 1 つ**。素の 5330 を使う間は passthrough（`/tmp/i915-hw.lock` の試験）も使えない。2 つの実機の試験を同時に走らせない。
- **image の build は同時に 1 つ**（commands.md §0）。
- **QEMU の console・serial の log で判定しない**。素の 5330 の判定は ssh の `dmesg`（guest に ssh で尋ねる）とユーザーの目視。
- QEMU と実機の証拠を分ける。やっていない確認は「未実施」（例: graphical boot の素の 5330 の takeover は 2026-10-01 時点で未実施）。
- 10-10 以降は bug の修正と実機の調整だけ。
- commit は `git commit -m WIP -- <自分の path>` だけ。push しない。`.internal/` を読まない。集約の `make check` を走らせない。
