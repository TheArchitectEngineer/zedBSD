# WS031 レポート 36 — i915 再構築の完了報告とコードレビュー依頼

Date: 2026-09-22
対象ツリー: `~/zedBSD-gpu`（centris、未コミット）。旧ツリー `src/drivers/gpu/i915-old/` は比較用に残してある。
前提資料: [再構成案](../../i915-refactoring-design.md)（とその review / functions / assets）、[レポート 35](ws031-report-35.md)。

## 1. やったこと

専門家案に基づくユーザー決定（2026-09-21）どおり、`src/drivers/gpu/i915/` を `i915-old/` へ待避し、
新しい `i915/` を設計の構成で作って、旧ツリーから**関数ごとにコピーしながら** `plan/coding-style.md` へ全面準拠で書き直した。
表示の Linux 移植（約 3 万行）も移動と同時に書き直した。移動は**挙動を変えない**方針で行い、
設計・レビューが挙げた機能変更（session 単位の object 表 A05、serving loop の解体 R2、recovery R7/R9 など）は行っていない。

| 段階 | 内容 | 検証 |
| --- | --- | --- |
| S0 | 待避、skeleton（PCI 登録、kernel readiness と start worker） | 実機 attach→start |
| S1 | 共通部（mmio/forcewake、pci、dma、sync/workqueue、GT 情報・reset・PCODE、workaround/MOCS/RC6/RPS、memory/GGTT/PPGTT、engine/context/request/execlists、割込み、ops、request worker） | 実機 GT 初期化、record_defaults / verify_workarounds PASS |
| S2 | compiler | kernel md5 と 42 万行の出力が旧と一致、gentool PASS |
| S3 | render（Vulkan executor） | 実機 vkdemo offscreen frame 1 `7523debe…05ff`、独立オラクル PASS |
| S4 | display（8 並行 package＋統合） | 実機 共有 route `94615464…19b1`（E-130 と同一）、LCD-B レジスタ Linux dump 一致 17/17、40 s 695 frame ended PASS、写真 |
| S5 | 試験の移設 | host 全 PASS、実機 ktest 382/0・EU/draw/tex/T3/bilinear が旧台帳と一致、LCD-B/C/D PASS |

台帳: `plan/ws031/results-ws031.md` E-131、E-132。

## 2. 新しいツリー

```
src/drivers/gpu/i915/            69 files  25.0k 行  共通部（device、session/resource/command/job、memory、GGTT/PPGTT、engine/context/request、irq、…）
  render/                        45 files  11.1k 行  Vulkan executor（instance/transport/dispatch/codec/object/memory/image/…/draw/blit/fence）
  compiler/                       6 files   4.3k 行  SPIR-V → IR → Gen12 EU
  display/                       69 files  79.4k 行  表示（power/clock/phy/dmc、vbt/opregion、state/watermark、takeover、hotplug、dp/aux/panel、ddi、pipe/plane/vblank、modeset/scanout/present）
  data/                           Linux 由来の定数・表（出典と notice 付き）、firmware blob、provenance
  tests/                         76 files  49.3k 行  試験（contracts、display、execution、render、fixtures）。本番 build に入らない
旧 i915-old/                                113.1k 行
```

本番に残る試験との接点は weak な checkpoint だけ（`drv_i915_test_after_start`、`drv_i915_gfx_draw_checkpoint`、
`drv_i915_firmware_test_request`、hotplug model の read/write）。試験 build は `I915_TESTS=y`。

## 3. 読み方（推奨順）

1. [実施計画](../../i915-rebuild-plan.md)（方針、段階、§5 の移行要約と §5.1 の廃止記録）と [共通規則](../../i915-rebuild-rules.md)（名前・errno・挙動の規則、§6 表示）。
2. **旧→新の対応表** [i915-rebuild-coverage.md](../../i915-rebuild-coverage.md): 旧 2,988 関数・378 ファイルそれぞれの新しい場所（file:function）。旧と新を並べて読むための地図。
3. 共通部: `device.c`（起動列。旧 `probe.c` の順序どおり）、`gt.h`、`i915.h`、`worker.c`（旧 serving thread）。
4. render: `render/dispatch.c` → `render/command.c` → `render/draw.c` / `render/blit.c`。compiler: `compiler/compile.c` 冒頭の規約。
5. display: [S4 分担・設計](../../i915-rebuild-s4.md)（Linux 環境 5 種の分離、`struct i915_display`、名前表）→ `display/display.c`（起動の段）→ `display/modeset.c` / `present.c`。
6. 各段階の報告: `i915-rebuild-s1-reports.md`、`-s3-reports.md`、`-s4-reports.md`、`-s5-reports.md`（担当ごとの判断、XXX、見つけた問題）。

## 4. 設計案からの変更点（判断の記録）

| 項目 | 判断 | 理由・場所 |
| --- | --- | --- |
| legacy の uncore/ggtt/engine/lrc/irq | 廃止 | 本番（resident）から到達しない。plan §5.1、s1.md |
| legacy request.c の kick/retire/emit | 廃止 | 同上（redirect で serving thread へ置換済みだった） |
| 旧 vk の res/pipe/cmdbuf/sync/wsi/display | fence（opcode 35–38）以外は未移植 | libvulkan から到達しない（s3-reports の opcode 表） |
| 表示の Linux 環境 | 5 環境（modeset/dp/vbt/hotplug/opregion）を互いに排他の header に分離 | 同名 Linux 型の layout が環境ごとに違う。dp/dp-sink、panel/panel-backlight、edid/edid-read、hdmi/hdmi-mode に分割（s4.md §4、§9） |
| 同名 iterator・マクロ（A14） | 意味ごとに明示名 `i915_<env>_*` へ展開 | s4.md §1.2。A14 の「空走査」は実際には効いていなかった |
| Linux 型名 | 移動中は変えない | 改名は layout 統一の段階で（rules §6） |
| errno | 関数境界は zedBSD の正の errno。Linux の値が観測・比較される所（eDP の結果、modeset hook、ログ）は `-I915_<ENV>_E*` を保つ | rules §3、s4.md §1.2。zedBSD の番号は Linux と違う（EINVAL=3 等） |
| 表示が無い／N0 が STOP | display を absent にして表示 ops なしで node を公開 | 旧は probe 全体を止めていた（s4.md §7） |
| 生成器 port_lcd_calc 等 | 廃止し手で管理。manifest は `data/provenance/` | s4.md §9-6 |
| 試験の hook | weak checkpoint のみ。runner は試験 build の compile 時定数で選ぶ | coding-style §2、§12 |

## 5. レビューで見てほしい点

1. **ファイルスコープに残った状態**（A09 の未完）: `display/vbt.c:266`、`opregion.c:119`、`dp-sink.c:136`、`state.c:99`、`modeset.c:134` の bound-world ポインタ（hook が world を引数に取らないため）、`render/memory.c` の live memory list、`render/blit.c` の rect kernel cache、`sync.c:70` の time-base fault。いずれも XXX 付き。装置一台前提。
2. **`container_of` による暗黙の埋込み要件**: `power.c:2604/2618`、`dmc.c:606`、`takeover.c:2483`、`drv_i915_edp_world_of`。これらの構造体は `struct i915_display` の中にしか置けない。
3. **NULL 検査の抜け**: `takeover.c:622` `drv_i915_n1_crtc_state`（modeset 経路が takeover world の存在に依存）、`opregion.c` の各 accessor、`power.c:2174` の vga。
4. **試験で見つかった本番の問題**（未修正）:
   - `mmio.c` の auto access: forcewake の ack timeout でもアクセスし、put で偽の underflow（旧から継承）。
   - `pci.c` `setup_msi`: 二度呼ぶと vector が漏れる（読解のみ）。
   - `render/pipeline.c`: compile 失敗時に未公開 pipeline が漏れる（既知 XXX）。descriptor set が解放されない（opcode 78 未移植）。
   - reply flag 0 でも create reply を書く（到達しない）。`submit.c:577` ring size ≤ 1 で 32 bit shift（到達しない）。
   - `power.c` `i915_pcode_poll` が time-base の失敗を EIO で返すが latch しない。
5. **旧コードの食い違い（XXX で保存、未修正）**: dp/vbt 環境の表示版数 13 固定（Tiger Lake で誤り）、`DVO_PORT_*` の値、`DISABLE_DPT_CLK_GATING` の bit、HDMI scrambling の `conn_state->connector` の型取り違え、`icl_calc_wrpll` の unsigned `abs`。
6. **設計案で未導入**: device 所有の ops storage（A10）、session 単位の object 表（A05）、serving loop の解体（R2）、recovery（R7/R9）。これらは移動後の別段階として残した。
7. **容量**: `struct i915_display` は数百 KiB を `kern_calloc` で確保（実機では成功）。

## 6. ユーザーの判断が要る事項

- **GPL-2.0**: `data/display-acpi-display.inc` と `display/opregion.c` の 2 関数（`drv_i915_acpi_device_id_update`、`i915_acpi_display_type`）は Linux `intel_acpi.c`（GPL-2.0）由来。旧ツリーにも同じもの（`intel_acpi_port.c`）があった。`license-inventory.md` §3。
- **Linux 由来の notice**: 表示の 22 ファイル（.c 17、header 5）に Linux の MIT notice を付けた（旧 port file の header、header は Linux 7.2 の同ファイルの notice）。GT 側（旧 `parity/gt_*.c` 由来）は旧から notice が無く、data の `.inc` にだけある。`data/forcewake-ranges.inc` も出典名のみ。
- **governance 文書の古いパス**（編集していない）: `AGENTS.md:392`、`plan/AGENTS.md:138`、`plan/master.md:1795`、`plan/queue.md:78`（`linux/i915-regs.inc`・`linux/i915-ids.inc` → `data/…`）。WS029 の節は legacy ファイルを記述している（履歴）。
- **使われなくなった設定**: `Makefile:162` の `CONFIG_DRIVER_PCI_I915_PARITY`、`CONFIG_DRIVER_PCI_I915_SELFTEST`（`config/kernel-options.list`）。
- **旧ツリーの削除**: レビュー対応の後。削除前の残作業は coverage §6（参照の最終確認、監査の再実行）。

## 7. 未実行・既知の制限

- 実機で未実行の試験シナリオ: lcdr、lcdo、lcdg、hdmib、dual、dual_share、aux、hdmi_edid、hdmi_hpd（試験 build では link・compile 済み）。
- 廃止した試験: N1（firmware 表示の takeover は本番 switch が要る）、opregion fwtest（起動途中の checkpoint が要る）、legacy selftest、参照 kernel 比較 build（render に seam が足りない）。
- ktest の skip 13（VBT parser の driver 共通 slot 8、`kern_diag_oneshot_arm` 5）、display_ktest の skip 1（delayed work の flush が無い）。
- 旧から継承の制限（BCS0 は record のみ、同期 submit、hang 後の wedged 表示なし、表示の同期 present など）は変えていない。
