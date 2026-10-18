<!-- awesome-plan project=zedbsd record=ws245-p020 -->

# ws245-p020: GMBUS と DDC の EDID の読み（xd-gmbus）

Status: in-progress（q1228-i02 P1: 実装・host 試験・build は済み。2026-10-16 ユーザーの決定「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」（Q1 経由）で、5330 の回帰は p046 まで積んだ後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: q1228 / q1228-i02
設計: [design.md](../design.md) §6.2 の p020 の行（改訂 6）、§2.9・§2.13・§3.2 の xd-gmbus

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-gmbus.c/.h`（新、Zlib） | GMBUS の controller: `drv_i915_xd_gmbus_init`（数だけ）、`_reset`（GMBUS0 → GMBUS4 に 0）、`_pin_valid`（1〜3、9〜14）、`_transfer`（pin の選択 → message ごとに cycle（1/2 byte の offset とその次の message は 1 つの indexed cycle、2 byte の offset は GMBUS5 で後に 0）→ 各 message の終わりに wait phase → STOP → idle → GMBUS0 を 0）。read・write は 511 byte ごとの cycle、GMBUS3 は 4 byte ずつ下位から。NAK は idle を待ってから SW_CLEAR → 0 → GMBUS0 を 0、最初の message の最初の NAK だけ 1 回始め直す。ready にならない（NAK も無い）は STOP 無しで GMBUS0 を 0 にして返す。待ちの間は request の `interrupts` で GMBUS4 の該当 bit を立て、待ちの後に 0。結果は `I915_XD_GMBUS_DONE/NO_ACK/HUNG/NOT_READY/LINGERED/BAD_PIN` と失敗した message の番号・始めた回数・最後の GMBUS2。途中の出来事（NAK の後に bus が active のまま・NAK・始め直し）は request の `noted` に起きた順で知らせる。register には xd-io だけで触る。値と手順は block ごとに `Values referenced:`（50 ms・10 ms）・`Procedure referenced:`（Linux v6.8.12 intel_gmbus.c の do_gmbus_xfer・gmbus_is_index_xfer・gmbus_index_xfer・gmbus_xfer_read(_chunk)・gmbus_xfer_write(_chunk)・gmbus_wait・gmbus_wait_idle） |
| `display/gmbus.c`（殻） | wait・wait_idle・max_xfer_size・read/write の chunk・index の判定と転送・message の列・NAK の片付け・`i915_has_gmbus_irq`・WA #0868 の clock gating の呼びを削除（1029 → 約 500 行）。残る物: adapter（pin ごとの bus、名前、初回に lock と xd-io の窓と xd-gmbus を用意）、controller の lock、GMBUS の power domain、force_bit と bit-bang の fallback（`-EAGAIN` で retry の bit、bit-bang は -EIO の記録の step）、`i2c_msg` → xd の message の写し、結果 → 旧の errno（DONE → 数、NO_ACK → -ENXIO、HUNG・LINGERED → -ETIMEDOUT、NOT_READY → -EAGAIN）と旧と同じ文の log（NAK・retry・after NAK は `noted` で起きた順に、waiting for idle と falling back は戻った後に）。model の試験（`world->hpd.fake`）の時は窓の hook が `drv_i915_hpd_read/_write` を通る。Linux の MIT の表示は殻に残す（p019 の aux.c と同じ） |
| `display/hotplug-internal.h` | `xd-gmbus.h` を include。world に `hpd_gmbus_io`（xd-io の窓）と `hpd_gmbus`（xd-gmbus）。`struct intel_gmbus` の `reg0`・`gpio_reg`・`bit_algo` を `pin` に、`struct i2c_algo_bit_data` を削除、`display.gmbus` の `mmio_base`・`wait_queue` を削除。GMBUS の文だけが使っていた `I915_HPD_WAIT_FOR`・`_WAIT_FOR_US`・`intel_de_wait_for_register_fw`・wait queue の macro・`HAS_GMBUS_IRQ/_BURST_READ`・`IS_GEMINILAKE/IS_BROXTON/HAS_PCH_SPT/_CNP`・`i915_bxt/pch_gmbus_clock_gating` を削除。先頭の comment の Linux の file の一覧から intel_gmbus.c を外した。R11: 2 → 0 |
| `display/xd-io.c/.h` | `drv_i915_xd_io_poll_any`（どれかの bit が立つまで待つ。GMBUS2 の ready か NAK）。poll と共通の static `i915_xd_io_wait` に |
| `intel/display-regs-xd-south.h` | GMBUS4 の 3 bit（IDLE・WAIT_PHASE・HW_READY）を足し、`Values referenced:` に名前を足した |
| `tests/display/xd-regs-map.h` | 上の 3 つの対 |
| `tests/display/xd-fake-regs.c/.h` | 読みの規則（`i915_xd_fake_rule_read`、register の読みを試験の関数が答える）。GMBUS の DDC の model（`struct i915_xd_fake_gmbus`、`i915_xd_fake_gmbus_attach/_script`）: GMBUS1 の SW_CLEAR・STOP・data cycle（ACK・NAK・STUCK の script、0x30 の segment pointer、index cycle・1 byte の write・GMBUS5 の offset、read の byte、write の byte の受け取り）、GMBUS2（stuck は ACTIVE だけ、NAK（`stays_active` で ACTIVE も）、HW_READY・WAIT_PHASE と残りがある間の ACTIVE、`lingers`）、GMBUS3 の 4 byte ずつ。data cycle の command の語を記録 |
| `tests/display/host-xd-gmbus-test.c`（新） | 24 の check: reset の 2 書き込み、pin の表、pin 4 は書き込み 0 で BAD_PIN、base block（旧と同じ command の語 0x468000a1・STOP 0x48000000、byte の順）、GMBUS4 の 68 書き込み（data 32・wait phase・idle）、interrupts 無しは全部 0、block 2（GMBUS3=1・0x42010060 → 0x468000a1、2 つ目の segment）、2 byte の offset（GMBUS5 0x80000120 → 0x420600a1 → 0）、600 byte の read（511 と 89 の 2 cycle）、6 byte の write（0xa3a2a1a0・0x420600a0・0x0000a5a4）、offset の後の 0 byte の read は 2 cycle、NAK 1 回（始め直して読める、SW_CLEAR・0・GMBUS0 0・pin の選び直し、REFUSED と RETRY の通知）、NAK 2 回（NO_ACK、始めは 2 回）、誰もいない address、後の message の NAK（始め直さない、番号 1）、NAK の後の active（10 ms、HUNG、通知の順）、STOP の後の active（10 ms、LINGERED、GMBUS0 は 0 に）、ready にならない（50 ms、STOP 無し、NOT_READY） |
| `tests/display/host-xd-io-test.c` | poll_any の 2 check（2 つ目の bit で 40 µs に終わる、どれも立たないと全部の時間で timeout と記録） |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-gmbus-test` を足した |
| `platform/amd64/vmunix.mk` | i915 の行に `xd-gmbus.c` |

段の phase.md の必須の項目:
- 消す file: 無し（`gmbus.c` は殻として p039・p051 まで、`edid-read.c` は下の理由で残す）。
- 消す関数と残る呼び手: gmbus.c の static 12 個（`i915_has_gmbus_irq`・`i915_gmbus_wait`・`_wait_idle`・`_max_xfer_size`・`_xfer_read_chunk`・`_xfer_read`・`_xfer_write_chunk`・`_xfer_write`・`_is_index_xfer`・`_index_xfer`・`_xfer_messages`・`_clear_error`。呼び手は gmbus.c の中だけ → 0）、hotplug-internal.h の static inline 2 個（`i915_bxt_gmbus_clock_gating`・`i915_pch_gmbus_clock_gating`、呼び手は gmbus.c だけ → 0）と上の macro（`grep` で display/・tests/ に hotplug の環境の使い手 0）。
- 殻が更新する旧の写し: `bus->force_bit` の retry の bit（`-EAGAIN` で立て、bit-bang の失敗で落とす。旧のまま殻に）。world の数 `gmbus_irqs`（割り込みの数、旧のまま）。
- 表 R: GMBUS0 の行（p020〜p035）。`takeover.c:5352-5354` の nogem front の reset は旧のまま（起動の 1 回、xd-gmbus の transfer の前）、p035 で xd に。transfer の書き手は段の後 xd-gmbus だけ。
- hook と lock: 殻が controller の mutex（`display.gmbus.mutex`）と GMBUS の power の参照を transfer の間持つ。xd-gmbus は lock を取らない（xd-io の lock の順の外）。
- build の組: 既定の vmunix と `I915_TESTS=y` の 5 組（下）。
- R11: hotplug-internal.h 2 → 0。他の触った C の file（gmbus.c・xd-io.c/.h・display-regs-xd-south.h・xd-regs-map.h・xd-fake-regs.c/.h・host-xd-io-test.c）は前から 0、新しい file も 0。

## 設計との差

1. **`edid-read.c` は消さない**（design §6.2 の p020 の「消す旧 code」の列に `edid-read.c（366 行）` がある）。`edid-read.c` は DDC の block の読み（block ごとに 4 回・checksum・header・segment・5 回の transfer）で、HDMI（GMBUS）だけでなく eDP の I2C-over-AUX（`dp-sink.c:752`）と外部 DP（`dp-ext-kern.c:818`・`:839`）も同じ関数で EDID を読む。hardware の列は無い（`i915_i2c_transfer` は adapter の algorithm を呼ぶだけの継ぎ目）。消すのは AUX の EDID の呼び手（dp-sink・dp-ext）と HDMI の probe（hotplug の p039）が xd に移った後（p048・p039・p052 のどれか最後の段か p043）。
2. **burst read（display 10 以降の 512〜767 byte の 1 cycle）は持たない**。読みは 511 byte ごとの cycle（design §2.13「read は 511 byte まで」）。呼び手の読みは EDID の 128 byte だけなので実機の振る舞いは変わらない見込み。
3. GMBUS4 の割り込みの enable は旧と同じに残した（poll で待つので割り込みは数えるだけ。`gmbus_irqs` の数を保つため）。

## 旧と同じに保った物と確かめ方

- 確かめ方（段の前後の比べ）: scratch の harness（commit しない。hotplug の world を model で作り、adapter の口 `drv_i915_hpd_gmbus_adapter` と `master_xfer` と `drv_i915_drm_edid_read` だけを呼んで、全部の register の書き込み・戻り値・読んだ byte の和・force_bit・log を出す）を段の前（2f34bc94c の gmbus.c）と後で同じ source から build して diff した。場面: pin 2・9 の adapter（reset の書き込み）、pin 4・15 は NULL、base block、128 からの 1 byte、segment の block 2、NAK 1 回、NAK 2 回、0x51 に誰もいない、2 つ目の message の NAK、NAK の後に active のまま、STOP の後に active、controller が止まる（-EAGAIN）→ 次は bit-bang（-EIO、retry の bit が落ちる）→ その次は GMBUS で読める、2 byte の offset、6 byte の write、offset の後の 0 byte の read、pin 9 の 5 byte の read、`drv_i915_drm_edid_read` で 3 block（block 2 は segment）。**書き込みの列（GMBUS4 の 1 語ずつを含む）・戻り値・byte・force_bit・log の文は全部一致**。違いは log の 2 行（waiting for idle、falling back）が GMBUS0 の 0 の書き込みの前から後に移っただけ（host の出力の中の並び。実機の書き込みの列は同じ）。
- 旧の host 試験: `host-dp-test`（72）・`host-lcd-modeset-test`（123）を段の前（2f34bc94c を `git archive` した tree）と後で `plan/ws031/display-ref -v` で流し、出力（821 行・392 行）が pointer の値を除いて一致、失敗 0。GMBUS を通る旧の host 試験は無い（hotplug の model の試験 `hdmi-hotplug.c`・`hpd-model.c` は kernel の I915_TESTS の物で build だけ）。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p020/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24、失敗 0。xd の .c（xd-gmbus.c を含む）の `I915_XD_STRICT` の検査は通る。
- build: 下の「build」の行。
- 5330: 未実施（p046 まで積んだ後に 1 回。起動に HDMI が無いので GMBUS の transfer は通らない。通るのは hotplug の start の adapter の reset の 2 書き込み（GMBUS0・GMBUS4 に 0）で、書き込みの列は旧と同じ見込み）。
- 実機で確かめる事（節目、ユーザーの一覧）: HDMI の plug で EDID が読めて 2 画面になる事（GMBUS の transfer の初めての実機の道）。USB-C DP の plug（p019 の AUX の TBT IO）。

## build

- 2026-10-16: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p020/k-default build/p020/k-default/vmunix` warning 0（vmunix の sha256 87b9f1c9…）。`I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組も warning 0（hotplug の model の kernel 試験 `hpd-model.c`・`hdmi-hotplug.c` を含む）。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- GMBUS: 起動の GMBUS の書き込みは start の reset（c5100・c5110 に 0）。基準だけの 2 回目の reset は H5（無害）。他の差なし

満たしていない・未確認の基準と残る物:

- HDMI の plug で EDID・2 画面（GMBUS の transfer の初めての実機の道）、USB-C DP の plug: ユーザーの一覧（beta2.md の U19〜U23）の項目で、未実施

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい（5330 で通る道は満たした。transfer の実機の道はユーザーの一覧）。
