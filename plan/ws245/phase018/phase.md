<!-- awesome-plan project=zedbsd record=ws245-p018 -->

# ws245-p018: 旧 VBT parser の本体の削除（殻 `vbt-shell.c`）

Status: in-progress（q1223-i01 P1: 実装・host 試験・build・5330 の素の起動の回帰に合格（2026-10-16 16:06）。merge の依頼を Q1 に送った（消す file 4 つは Q1））
Parent: [WS245](../ws.md)
Queue: q1223 / q1223-i01（Q1 の技術判断で範囲を広げた: 「DP の環境が使う vbt.c の allocator・log の関数は殻に残す。execution の試験の組の ktest-display.c・ktest-display-probe.c は … 参照されていなければ削除の対象 … LW の見積りを phase018 に書き直し、段が大きくなるなら 2 段に分けてよい。5330 の回帰に合格してから main。」）
設計: [design.md](../design.md) §6.2 の p018 の行（改訂 6）

## 段の分け方と LW（2026-10-16 書き直し）

design の p018（3 LW）は「EDID の decode の置き換え」と「VBT parser の本体の削除」の 2 つの機能を持っていた。R1（1 段 1 機能）に合わせて 2 段に分けた:

| 段 | 中身 | LW |
| --- | --- | --- |
| **p018**（この段） | 旧 VBT parser（`vbt.c` 5492 行）を消し、本番の呼び手の 10 関数を xd-vbt を呼ぶ殻 `vbt-shell.c` に。`ktest-display.c`・`ktest-display-probe.c` の削除 | 3 |
| **p053**（新、次の段） | EDID の decode（`edid.c` の `drv_i915_edid_preferred_mode` と hotplug の EDID の要約）を xd-edid に。`edid.c` の `drv_i915_drm_mode_set_crtcinfo`・`_get_hv_timing` は殻へ | 2 |

合計は 159 → 161 LW。

調べで分かった事（phase018 の最初の調べ、2026-10-16）のうち「DP の環境が vbt.c の allocator・log の関数を使う」は、実際に数えると**定義への依存は無かった**: DP の環境（`dp-internal.h` を include する `dp-sink.c`・`aux.c`・`panel.c`・`edid-read.c`・`dp-ext-kern.c`）は自分の log（`drv_i915_dp_log_enabled`・`_notef`）を使い、`vbt.h` からは定数（`I915_VBT_LOG_*`）・macro（`I915_VBT_CLAMP`）・`static inline` の helper（`i915_vbt_intel_port_to_phy`・`_phy_is_tc`・`_display_ver`・`_has_ddi`）だけを使う。`vbt.h` が宣言する allocator 等（`drv_i915_vbt_zalloc` …）を呼ぶのは `vbt.h` の中の使われない `static inline` だけで、link は要らない。`vbt.h` は DP の環境の土台として `dp-internal.h` を消す段（p050）まで残す（parser の説明の comment だけ直した）。

## 変えた物

| file | 中身 |
| --- | --- |
| `display/vbt-shell.c`（新、Zlib） | `drv_i915_vbt_world_create/_destroy`（display の VBT の状態）、`drv_i915_bios_set_opregion_vbt`、`drv_i915_bios_init_ex`（VBT の bytes を選ぶ: OpRegion の写し → PCI の expansion ROM。xd-vbt で parse し `struct i915_vbt`・`i915_vbt_state` に写す。xd の description が無い device（Gen9）は出力 0）、`_driver_remove`、`drv_i915_sha256`（xd）、`drv_i915_vbt_validate`（xd の check）、`_encoder_for_port`、`_init_panel`（xd の panel を旧の型に）、`drv_i915_opregion_locate_vbt`（N0 が p036 まで使うので旧の本体をそのまま移した）。log は `i915: VBT: source=… vbt_found=… version=… child_devices=…` と `i915: VBT child[n]: …`（child の行は旧と同じ書式） |
| `display/internal.h` | `struct i915_vbt` に xd の parse への pointer `xd` |
| `display/vbt-parse.h` | 消えた関数の宣言（parser の内部・試験用の 11 個）を外し、説明を直した |
| `display/vbt.h`・`takeover.c` | comment の vbt.c への言及を直した（takeover.c の DVO port の表は `Values referenced:` の形に）。vbt.h の開発の管理情報 1 箇所 |
| `platform/amd64/vmunix.mk` | i915 の行の `vbt.c` を `vbt-shell.c` に、`I915_TEST_SET=execution` から `ktest-display.c`・`ktest-display-probe.c` を外した（`ktest.c` は weak で呼ぶので組は build できる） |
| `tests/execution/README.md` | 消す 2 file の説明を外した |
| `tests/display/host-xd-vbt-test.c` | 旧 parser と比べる部分（`test_vbt_old`、defaults の比べ、damaged の旧 check）を外し、段の殻の試験を「旧の型に xd の値が写る」形に（Gen9 は出力 0、port の無い所は ENODEV、remove の後は何も無い） |

**消す file（Q1 に依頼）**: `src/drivers/gpu/i915/display/vbt.c`、`src/drivers/gpu/i915/intel/vbt-defs.h`（include するのは vbt.c だけ）、`src/drivers/gpu/i915/tests/execution/ktest-display.c`、`src/drivers/gpu/i915/tests/execution/ktest-display-probe.c`（master.md の試験の一覧・未完の Phase から参照されていない: `plan/ws051/phase002`（cleared）・`plan/ws118/phase006`（WS は closed）・`plan/ws233/phase001`（過去の変更の記録）の言及だけ。design は p043 で ktest-xd-display に置き換える予定だった）。vbt.c が残っている間は display の host の lib が vbt.c と vbt-shell.c の両方を compile して link が重なるので、host 試験は消す file を除いた `git archive` の写しで流した。

段の phase.md の必須の項目: 消す関数と残る呼び手 — vbt.c の 111 関数のうち、外から本番で呼ばれる 10 個は殻に、残り（parser の内部と試験だけが呼ぶ物: `drv_i915_bios_is_valid_vbt_header`・`_process_vbt`・`_init_vbt_missing_defaults`・`drv_i915_vbt_init`・`_fini`・`_set_log_level`・`drv_i915_bios_is_valid_vbt`・`_hdmi_level_shift`・`drv_i915_vbt_emit`・`_fmtcheck`・`drv_i915_bios_init`）は本番の呼び手 0（grep）。殻が更新する旧の写し: `vbt_state`（source・vbt_found・version・missing_defaults_used・display_devices・parsed）。表 R・hook: 無し。R11: vbt-shell.c・vbt-parse.h・host 試験 0、vbt.h 1 → 0。

## 確かめた事

- host（消す 4 file を除いた写し）: regs 495・io 48・platform 89・vbt 134・survey 57、失敗 0。native-decide 14、失敗 0。
- build: 既定の vmunix warning 0。`I915_TESTS=y` の vke2・display・display_ktest・display_ktest2・execution の 5 組も warning 0。

## 5330 の素の起動の回帰（2026-10-16、素の起動、AC 接続）

image は p017 と同じ（e4c1b4295 の disk-image、USB の Boot0007）。ed9621e9b → b650c327c の kernel・uapi の差は AMD の driver だけで、kernel だけを差し替えた。基準の kernel は main b650c327c の source から（sha256 8c531060…）、段の kernel は 0aa834646（e6237eb9…。comment だけ直した後の build も同じ sha256）。

| 見る物 | 基準（b650c327c） | 段（p018） |
| --- | --- | --- |
| zedBSD の ssh まで | 31 s | 46 s |
| graphical / msgbuf_dropped / session | graphical / 0 / ready=1 | graphical / 0 / ready=1 |
| lease 1 / picture up / device lost | 1 / 2 / 0 | 1 / 2 / 0 |
| i915 の error・warn の行 | 3 | 2（旧 parser の `intel_bios_init: … parser_errors=0` の行が無くなった分。文言の差だけ） |
| backlight-probe | 100 / 100 / 100 | 100 / 100 / 100 |
| survey | lit | lit（frames +122、CRC 0x5c1be2a5 の 2 回、underrun 0） |
| VBT を読む物の log（`VBT child[0..3]`、`edp init_connector`、`LCD-B input`、`DP-ext TC1/TC2`） | — | 基準と同じ（diff で 0） |
| 書き込みの digest | order 0xe098061e、set 0x62c7059b | order 0xdc839fe9、set 0x6e910c28（同じ kernel でも起動ごとに行き来する 2 つの値の組。p017 の語ありの起動で 0x162100 の bit 1 だけの差と確かめた組） |
| `5330-survey-diff.py` | — | steady の差 0、PASS |

判定: **合格**（Q1 の判定待ち）。LCD の目視: 未確認（一覧）。証拠: `plan/ws245/tests/evidence/5330-base-b650c327c-20261016.md`・`5330-p018-0aa83464-20261016.md`。
