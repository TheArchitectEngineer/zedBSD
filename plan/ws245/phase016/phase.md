<!-- awesome-plan project=zedbsd record=ws245-p016 -->

# ws245-p016: OpRegion の場所と map（xd-vbt の読み、`struct i915_xd` と io・platform の用意、OpRegion の runtime の削除）

Status: in-progress（q1218-i01 P1: 実装・host 試験・build・5330 の素の起動の回帰に合格（2026-10-16 15:16）。merge の依頼を Q1 に送った（消す file 6 つは Q1）。SV7（Gen9）の起動はユーザーの一覧）
Parent: [WS245](../ws.md)
Queue: q1218 / q1218-i01（承認は phase047 と同じ）
設計: [design.md](../design.md) §6.2 の p016 の行、§3.6、§6.1 R2・R8・R11（改訂 6）

## 作った物・変えた物（2026-10-16）

| file | 中身 |
| --- | --- |
| `display/xd.h`（own、新） | `struct i915_xd`（xd-io の窓、platform の choice、chosen、OpRegion の読みの結果）と段の関数 |
| `display/xd-display.c`（own、新） | `drv_i915_xd_start`（create: product と revid で description を選ぶ、PCH は platform の既定を仮定。無い device（Gen9・未知の ID）は log だけで chosen 0）、`drv_i915_xd_south`（irq_prepare の後に PCH ありで選び直す。別の種類の PCH は記述を保って log）、`drv_i915_xd_read_opregion`（HAL の read-only の device の map で OpRegion を読み log） |
| `display/xd-vbt.c/.h` | `drv_i915_xd_opregion_read`: OpRegion の 8 KiB を写し、`find_vbt` の raw VBT を先に試し（OpRegion の中に重なる物・写しより大きい物・map できない物・check を通らない物は採らない）、駄目なら mailbox 4、VBT を写して SHA-256。map は呼ぶ側の hook（host 試験は buffer を渡す）。Procedure referenced は Linux `intel_opregion_setup` |
| `display/display.c` | create で `drv_i915_xd_start`、irq_prepare で `drv_i915_xd_south`（PCH の source が REAL の時だけ ID を渡す）、`drv_i915_display_init_opregion` は xd の読みの結果を `display->opd`（vbt_valid・vbt・size・sha256）に写して旧 parser（`drv_i915_bios_set_opregion_vbt`）に渡す。`i915_display_opregion_signature` と OpRegion の world の create/destroy、`intel_opregion_register` の log を削除 |
| `display/takeover.c`・`takeover.h`・`takeover-internal.h` | `drv_i915_opregion_read_data`・`drv_i915_opregion_log`（128 行）と、sanitize の `intel_opregion_notify_encoder`（macro と宣言）を削除。N0 自身の読み（`takeover.c` の `drv_i915_opregion_locate_vbt` の呼び）は p036 まで旧（影の比較の片側） |
| `display/interrupts.c` | GSE の hook を何もしない関数に（OpRegion の runtime は無い: §7 #5）。`opregion.h` の include を外した |
| `display/internal.h` | 先頭に `I915_XD_STRICT` の `#error`、`xd.h` を include し `struct i915_display` に `struct i915_xd xd`、ACPI の notifier の型（opregion.c だけが使っていた）と `opregion_world` を削除 |
| `display/output-timing.h`（own、新）・`output.h`・`xd-edid.c` | `drv_i915_output_edid_timing` の宣言を `internal.h` を含まない header に移し（R2、レビュー 5 の M2 (a)）、xd-edid.c はそれを include |
| `platform/amd64/vmunix.mk` | i915 の行から `opregion.c` を外し `xd-display.c` を足した。`I915_TEST_SET=display` から `lcd-opregion.c`、`display_ktest2` から `opregion-ktest.c` を外した（comment も） |
| `tests/display/host-xd-vbt-test.c`・`host-xd-platform-test.c`、`plan/ws245/tests/run-xd-host-tests.sh` | host 試験（下）。run script に xd の .c を `-DI915_XD_STRICT -fsyntax-only` で compile する検査 |

**消す file（Q1 に依頼。担当は rm しない）**: `src/drivers/gpu/i915/display/opregion.c`、`display/opregion.h`、`display/opregion-internal.h`、`intel/opregion.h`、`tests/display/lcd-opregion.c`、`tests/display/opregion-ktest.c`。どれも vmunix.mk と include から外した（本番・試験の組の build は通る）。display の host の lib は `display/*.c` を全部 compile するので、消すまでは opregion.c の compile が失敗する（host 試験は、これらを除いた `git archive` の写しで流した）。

段の phase.md の必須の項目:
- 消す関数と残る呼び手: `drv_i915_opregion_read_data`・`_log`（呼び手は display.c だけ → 0）、`drv_i915_opregion_*`（opregion.c の 17 関数。本番の呼び手は display.c の world・interrupts.c の GSE・takeover-internal.h の notify → 0。残りは lcd-opregion.c だけ）。`drv_i915_opregion_locate_vbt`（vbt.c）は N0 と host 試験が使うので残る（p018 の殻へ）。
- 殻が更新する旧の写し: `display->opd`（present・mapped・vbt_valid・vbt・vbt_size・vbt_sha256。`op` の field は誰も読まないので埋めない）、`display->p2_opd`、`opregion_present`・`opregion_vbt_present`。
- 表 R: 該当なし。hook: 無し。
- R11（開発の管理情報）: `display.c` 87・`internal.h` 50・`takeover.c` 22・`takeover-internal.h` 4・`interrupts.c` 1・`output.h` 3 → 0（`grep -cE` で確かめた。`"i915: P2 opregion"` などの log の段の符号を消した。読む道具は無い: `plan/tools`・`tests`・`plan/ws*/tests` を grep して 0）。

## 確かめた事（host・build）

- `run-xd-host-tests.sh`（opregion の 6 file を除いた `git archive HEAD` の写しで）: regs 495・io 48・platform 89（+9: start の description・PCH・Gen9・未知の ID）・vbt 135（+16: OpRegion の読み）・survey 57、失敗 0。xd の .c の `I915_XD_STRICT` の compile は全部通り、`output.c` を同じ形で compile すると `#error` で止まる事を確かめた。
- build: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws245-dev vmunix` warning 0。`I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組も warning 0。

## 5330 の素の起動の回帰（2026-10-16、素の起動。QEMU ではない）

image は p047 と同じ（510b267f5 の disk-image、USB の Boot0006）。基準の kernel は親の main 95b86cba8 の source から（sha256 35192182…）、段の kernel は 17d5b333（1407ae8c…）。95b86cba8 と 510b267f5 の kernel の差は AMD の driver だけ。AC 接続。

| 見る物 | 基準（95b86cba8） | 段（p016） |
| --- | --- | --- |
| zedBSD の ssh まで | 51 s | 届かず（下の DHCP）。ユーザーが network を reset した後に届いた（15:15） |
| kern.boot.login / msgbuf_dropped | graphical / 0 | graphical / 0 |
| sessiond の SESSION start と HANDOFF | あり（ready=1） | あり（ready=1） |
| lease 1 claimed / picture up / device lost / error・warn の行 | 1 / 2 / 0 / 3 | 1 / 2 / 0 / 3 |
| perf の per submit | 4.21・6.33 ms | 2.81・5.20 ms |
| backlight-probe | 100 / 100 / 100 | 100 / 100 / 100 |
| survey | lit（frames +1278） | lit（frames +1717、CRC 0x7a7b9cfe の 2 回、underrun 0） |
| 書き込みの digest | 13613 writes、order 0xb961e20f、set 0xdcd7004e | 同じ（OpRegion は読むだけなので変わらない見込みのとおり） |
| `5330-survey-diff.py` | — | steady の register の差 0、PASS |
| OpRegion・VBT | N0 の VBT 8704 byte、sha256 3bff4a09… | `i915: xd-vbt: OpRegion at 0x614e5018 version 2.1 mailboxes 0x1d: VBT from the raw VBT, 8704 bytes, sha256 3bff4a0920d55c9a..`、N0 は `same bytes=1`・`PROCEED`、`intel_bios_init: source=1 … child_devices=4`（基準と同じ） |
| xd の platform | — | `i915: xd: platform Alder Lake-P, display 13, stepping 48 (south display ADP assumed …)` → irq_prepare の後 `south display ADP (read, PCH 0x5180)` |

判定: **合格**（Q1 の判定待ち）。LCD の目視: 未確認（ユーザーは network の reset の時に画面が点いている事を見た: 2026-10-16 ユーザー「画面はついており、ネットワークをリセットしました」）。証拠: `plan/ws245/tests/evidence/5330-base-95b86cba8-20261016.md`・`5330-p016-17d5b333-20261016.md`。

**DHCP が取れなかった原因**（p016 に依らない）: `/var/log/messages` の 06:08:43 UTC `networkd: ue0: dhcp obtained no address: dhcpc: ue0: lease obtained, but /etc/resolv.conf could not be written: File exists`。zedBSD の `/etc` に 06:06（直前の基準の起動）の `/etc/resolv.conf.tmp.38` が残っていた。`userland/base/dhcpc/main.c:1093-1100` は `/etc/resolv.conf.tmp.<pid>` を `O_CREAT | O_EXCL` で作るので、起動ごとに同じ pid（38）の dhcpc が残った temp に当たると EEXIST で失敗する。今日の失敗した 3 回（05:03・05:24・06:08 UTC）は同じ文言。i915 の kernel の差は pid の割り当てを変え得るだけで、原因は dhcpc の temp の残り（Q1 に Bug として渡した。temp は消していない）。

**ユーザーの一覧**: SV7（Gen9）の起動（create の `i915: xd: no description of the device 0x5917 …` の 1 行と、init_opregion の OpRegion の行が今と同じ事）。
