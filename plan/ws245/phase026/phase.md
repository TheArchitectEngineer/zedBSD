<!-- awesome-plan project=zedbsd record=ws245-p026 -->

# ws245-p026: DMC（xd-dmc）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない。`dmc.c` の削除は merge の時に Q1）
Parent: [WS245](../ws.md)
Queue: q1229-i02（P1、ws245-p024〜p046 を design の順に実装して branch に積む）
設計: [design.md](../design.md) §6.2 の p026 の行、§2.12、§3.4 (d)

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-dmc.c/.h`（新、Zlib） | `struct i915_xd_dmc`（版、file の拒みの理由、package の版と entry の数、DMC ごとの program（entry の有無と offset、拒みの理由、header の版、置き場所、dword の数、owner の storage の写し、register の対）、load の診断）。`drv_i915_xd_dmc_init`・`_parse`（CSS header の長さ、package v1/v2 の版と長さ、stepping の entry の選び（最初に合う物: 完全、stepping の任意の substepping、任意の stepping の substepping、全部の任意）、各 program の header v3/v1 の長さ・register の数（20/8）・範囲（v1 は DMC 全体、main は 0x8F000〜、display 13 の pipe は 0x5F000〜、display 12 の pipe は pipe ごと 0x92000/0x96000…）・program の大きさ（display 12 0x6000、13 0x20000 byte）・storage の空き、を見て写す）・`_has_main`・`_has`・`_load`（platform の WA の時に 4 pipe の PIPEDMC の clock gating を off → program のある DMC の event handler 8 個ずつを off（control と address）→ preemption を止めて program を dword ずつ → header の register（pipe DMC の handler と、Tiger Lake の main の CLK_MSEC・VBLANK_A、ADL-S の VBLANK_A の handler は off の語で）→ DC_STATE_DEBUG の 2 つの mask と posting read → pipe C/D の gating を戻す）・`_pipe`（PIPEDMC_CONTROL の enable の rmw）。file の配置（header の field の byte の位置）は block ごとに `Values referenced:`（intel_dmc.c の struct intel_css_header・intel_package_header・intel_fw_info・intel_dmc_header_base/v1/v3）、上限の値も `Values referenced:`、手順は `Procedure referenced:`（parse_dmc_fw・parse_dmc_fw_css・parse_dmc_fw_package・dmc_set_fw_offset・fw_info_matches_stepping・dmc_mmio_addr_sanity_check・parse_dmc_fw_header・intel_dmc_load_program・pipedmc_clock_gating_wa・adlp_pipedmc_clock_gating_wa・disable_all_event_handlers・disable_event_handler・dmc_mmiodata・disable_dmc_evt・gen9_set_dc_state_debugmask・intel_dmc_enable_pipe・intel_dmc_disable_pipe）、stepping の wildcard は intel_step.c の intel_step_name |
| `display/dmc-shell.c`（新、殻、Zlib） | DMC の loader（work の queue、firmware の取得と ADL-P の fallback、INIT の power 参照を取り完全な load の後に返す、resume の載せ直し、fini）と mode set の pipe DMC の switch（`drv_i915_dmc_enable/disable_pipe`、mode set の register の口で PIPEDMC_CONTROL）。parse と load は xd-dmc を呼び、旧の `struct i915_dmc`（`dmc_info[]`・版・load の診断）を写す。stepping は `display->xd.choice.step`、firmware の path は platform の `dmc_path` |
| `display/dmc.c` | 中身を消した（「何も残っていない」の comment だけ。vmunix.mk から外した。host の display の lib は display/*.c を全部 compile するので、file が残る間も重複の symbol を作らない）。**merge の時に Q1 が消す** |
| `display/dmc.h` | 殻の 4 つの公開の関数だけに（`drv_i915_dmc_prepare`・`_parse_dmc_fw`・`_parse_reset`・`_load_program` を削除: 呼び手は旧 dmc.c の中だけ） |
| `display/xd.h` | `struct i915_xd` に `struct i915_xd_dmc dmc` |
| `display/display.c` | `i915_display_dmc_init` は stepping と path の計算をやめ、`drv_i915_dmc_init(dev, wq, mmio, pd, pwc)` を呼ぶ。log は xd の step と path |
| `platform/amd64/vmunix.mk` | `dmc.c` を `dmc-shell.c`・`xd-dmc.c` に |
| `tests/display/host-xd-dmc-test.c`（新） | 32 の check（下） |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-dmc-test` を足した |

段の phase.md の必須の項目:
- **消す file（merge の時に Q1）**: `src/drivers/gpu/i915/display/dmc.c`（中身は消した。本番の呼び手 0）。`dmc.h` は殻の header として残る（呼び手の display.c・pipe.c・takeover.c の段で消す: p035・p048・p050）。
- 消す関数と残る呼び手: `drv_i915_dmc_prepare`・`drv_i915_parse_dmc_fw`・`drv_i915_dmc_parse_reset`・`drv_i915_dmc_load_program` と旧 dmc.c の static 全部（`git grep` で本番の呼び手 0）。残る殻の関数: `drv_i915_dmc_has_payload`（display.c:616）・`_init`（display.c）・`_resume`（display.c:915）・`_fini`（display.c）・`_enable_pipe`（pipe.c:1852・takeover.c:4984）・`_disable_pipe`（pipe.c:2288）。
- 殻が更新する旧の写し: `display->dmc_dev.dmc`（`dmc_info[]` の present・offset・mmio_count・start・dmc_fw_size・payload・payload_size・header_ver、版、package の版と entry の数、load の診断 payload_writes・aux_writes・evt_disable_writes・psum・asum・load_seq_completed）。読み手は display.c の stop の log、`display.c:616` の `dmc_has_payload`、`modeset.c:2467` の `dmc_fw_mask`（pipe DMC の switch が見る）。
- 表 R: DMC の program storage・event handler・CLKGATE_DIS_PSL_EXT・DC_STATE_DEBUG の書き手は xd-dmc だけ。PIPEDMC_CONTROL は殻（mode set の register の口）、xd の `_pipe` は p048 から。
- hook と lock: load は DMC の workqueue の work（旧と同じ）。INIT の power 参照を持つ間に書く。preemption を止めるのは program の dword の書き込みの間だけ（xd-io の書き込みは眠らない: forcewake の範囲の外で、記録は ring だけ）。
- R11: 触った file は全部 0（display.c・dmc.c・dmc.h・xd.h・dmc-shell.c・xd-dmc.c/.h・host-xd-dmc-test.c）。

## 旧と同じに保った物と違い

- 確かめ方 1: 旧の host 試験 `host-dp-test`（72）・`host-lcd-modeset-test`（123、pipe DMC の enable を含む）の出力は p024 の後と同じ。
- 確かめ方 2（scratch の harness、git に入れない: `plan/ws245/temp/harness/wells26.c`・`run-wells26.sh`、`FW=<firmware の path>`）: 実物の firmware（main の `build/sources/firmware/i915/dc85…/adlp_dmc.bin` v2.20 と `tgl_dmc_ver2_12.bin` v2.12、読むだけ）を段の前（92c67726b の `git archive`）と後で parse して load し、選んだ entry・program の置き場所と大きさ・register の数・全部の書き込みの列を比べた。**ADL-P は全部一致**（D0、main + pipe A〜D、13141 書き込み、和も同じ）。
- **TGL の違い（意図した物、Linux を優先: 決定 4）**: (1) PIPEDMC の clock gating の書き込みが無い（Wa_16015201720 は display 13 だけ。旧は全 platform に書いていた）、(2) pipe DMC の event handler の register が 0x92000 から（旧は ADL-P の 0x5F000 を使っていた）、(3) main の VBLANK_A（0x32）と CLK_MSEC（0xbf）の handler を off の語で書く（旧は header の値のまま）、(4) pipe A の program の handler も off で書く（旧は 0x5F000 の範囲で判定していたので外れていた）。書き込みは 4733 → 4727。
- 違い: stepping は xd の platform の表（S1）から（旧は全 platform で ADL-P の換算を使っていた）。ADL-P は同じ D0。TGL は表の行。
- 違い: parse の拒みの log は「program N not taken (reason R)」の 1 行に（旧は理由ごとの文）。file 全体の拒み（CSS・package）は旧と同じ文。
- 違い: fini の後の stop の log の program の大きさは 0（旧は大きさを残していた）。

## 実機で確かめる事（5330、全部の段の後の 1 回で）

- `i915: dmc worker: proceeding to load`、DC6 が許される（DC_STATE_EN の DC6）、pipe DMC A の enable（PIPEDMC_CONTROL A の bit 0）。stop の log の版 v2.20、MAIN/A/B/C/D の present。
- suspend → resume（DMC の載せ直し、`program loaded again after the resume`）: 節目（p027）でユーザーの一覧。
- 5320（TGL）は節目でユーザーの一覧: 上の TGL の違い（Linux と同じ形）で DC の出入りが変わらない事。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p026/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 47・power 65・dmc 32、失敗 0。`I915_XD_STRICT` の検査（xd-dmc.c を含む）は通る。
- DMC の check: 5330 は D0、package の版と entry、wildcard の main、pipe A は C0 でなく D0 の entry、pipe B は D の任意、pipe C は無い、storage に順に写す。拒み: file 無し、CSS の長さ（124 byte）、package の版 3、package の長さ、main の register が main の範囲の外（main 無しで ENOENT、pipe A は取る）、pipe の register が display 13 の pipe の範囲は取る、program が途中で切れる、entry が file の外、header の版、register 21 個、storage の空き無し。ADL-P の load: 45 の書き込みの順（gating 4 → main と pipe A の handler 32 → program 3 → register 3（main の handler は header のまま、pipe A の handler は off）→ DC_STATE_DEBUG 0x3 → pipe C/D の gating を戻す、他の bit を保つ）、pipe B の gating は off のまま、main 無しで何も書かない。TGL の load: gating 無し、pipe A の handler は 0x92034〜、main の VBLANK_A と CLK_MSEC は off、他の handler は header のまま、pipe A の handler は off。pipe の switch: A の on/off（他の bit を保つ）、program の無い B は書かない。
- 旧の host 試験と harness: 上。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。

## build

- 2026-10-16: `sh build/tmp/kbuild.sh p021` で既定の vmunix と `I915_TESTS=y` の 5 組が rc 0・warning 0。pipe の switch の書き方の直しの後に既定の vmunix を作り直して rc 0・warning 0。`git diff --check` は空。
