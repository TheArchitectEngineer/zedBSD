# WS031 i915 再構築 — 実施計画と進捗

作成日: 2026-09-21。設計: [i915-refactoring-design.md](i915-refactoring-design.md)、
[関数台帳](i915-refactoring-functions.md)、[レビュー](i915-refactoring-review.md)、
[保全台帳](i915-refactoring-assets.md)。規約: [plan/coding-style.md](../coding-style.md)。
出発点: commit `7e7ff337`（E-130、共有 route で LCD 表示 PASS）。

## 1. 方針（ユーザー決定 2026-09-21）

- `src/drivers/gpu/i915/` を `src/drivers/gpu/i915-old/` へ待避（build から外す）。新しい `i915/` を
  設計の構成で作り、旧ツリーから**関数ごとにコピーしながら**書き直す。
- 書き直しは移動と同時に `coding-style.md` へ全面準拠させる。Linux 移植コード（parity/lcd、dp）も同じ。
- 試験コード（ktest、fake HW、`PARITY_*_TEST` シナリオ）は本番経路の後（S5）に `tests/` へ移す。
- 移動は**挙動を変えない**。レビューの機能変更（session 単位 object 表 A05、serving loop の解体 R2、
  recovery R7/R9 など）は、移動を終えた後の別段階とする。
- 本番は一つの構成だけにする。旧 resident build（`PARITY_RESIDENT=1` + `PARITY_RESIDENT_DISPLAY=1`）の
  経路が既定。試験用の期限（`PARITY_RESIDENT_SERVE_S`、`STOP_ON_CLOSE`）は本番に持ち込まない。
- 旧ツリーは専門家のコードレビューの参照用に残し、レビュー対応の後に削除する（ユーザー決定 2026-09-22）。削除前に保全台帳の全行の行き先を確認する（i915-rebuild-coverage.md）。

## 2. 段階と受入

| 段階 | 内容 | 受入 |
| --- | --- | --- |
| S0 | 待避、新 skeleton（i915.c 登録、device.c readiness）、build list、`main.c`/公開 header の追従 | CONFIG_DRIVER_PCI_I915=y で kernel が link。実機で attach と readiness のログ |
| S1 | 共通部: device 起動列（P0–P2、P6 GT、P7 の GT 部）、mmio/forcewake、irq、power、firmware、workarounds、memory・GGTT・PPGTT（legacy と parity の統合）、context・engine・request、reset（未実装拒否のまま）、session/resource/command/job の ops | 実機で GT 初期化完走、node 公開、既存の gpu-i915-test 相当で batch 1 本 |
| S2 | compiler（spirv/compile/eu） | host の gentool 試験 PASS |
| S3 | render（instance/transport/dispatch/codec/object/memory/image/descriptor/pipeline/render-pass/command/state/draw/blit/batch/math/sync） | vkdemo offscreen、独立オラクル PASS（E-128 と同一 hash） |
| S4 | display（power、clock、modeset、pipe、plane、scanout、present、vblank、ddi、phy、dp、aux、panel、edid、vbt、opregion、dmc、takeover、hotplug、watermark、color、diagnostics） | E-130 の再現: 共有 route、GPU copy→flip、40 s のアニメーション、写真 |
| S5 | tests（host fixture、ktest、fake HW、シナリオ）、試験 build | host fixture 全 PASS、主要シナリオの実機再実行 |
| 以後 | 機能変更（A05、R2、R7/R9 ほか）、旧ツリー削除 | 個別に定める |

S1 は表示初期化（P3–P5、P7 の表示部）を持たない。表示は S4 で device 起動列へ戻す。
S1〜S3 の間は panel を触らない（firmware の表示をそのまま残す）。

## 3. 作業規則

- 1 関数ずつ: 旧関数を読み、台帳の行（移動先・区分）を確認し、新ファイルへ規約どおりに書く。
  台帳と違う判断をしたら、その行に追記する。
- 旧名 → 新名の対応は各新ファイル冒頭ではなく、本書 §5 の移行表に記録する（コメントに設計番号を書かない規約のため）。
- static な関数は所有ファイル内に閉じる。境界を越える helper は設計 §5.6 の解決案に従う。
- Linux 由来のレジスタ定義・表は `intel/`（第三者由来の置き場）の系統別 header に移し、出典（Linux の版、ファイル、sha256）を残す。
- 各段階の終わりに build、host 試験、実機試験を行い、台帳（results-ws031.md）へ E 番号で記録する。

## 4. 進捗

| 段階 | 状態 | 記録 |
| --- | --- | --- |
| S0 | 完了（2026-09-22） | 実機: attach → readiness → start worker が動く |
| S1 | 完了（2026-09-22） | 実機: GT 初期化完走（record_defaults rc=0、verify_workarounds rc=0 mismatch 0）、node 公開、session context 作成。BCS0 は旧 resident と同じく record のみ（gpu-i915-test は旧でも不可）。RCS0 batch は S3 の vkdemo で確認。暫定: 表示割込みの summary を off にする hook（S4 で置換） |
| S2 | 完了（2026-09-22） | host: gentool PASS、compiler fixture PASS、kernel md5 と 42 万行の出力が旧と一致 |
| S3 | 完了（2026-09-22） | 実機: vkdemo offscreen 14 frame、frame 1 rgb_sha256=7523debe…05ff（E-127/E-128 のオラクル通過フレームと同一）。旧 vk/sync.c のうち fence（opcode 35–38）だけを render/fence.c へ移植（他の旧 module は vkdemo から到達しない）。独立オラクル PASS（tests/render/readback.c が weak checkpoint drv_i915_gfx_draw_checkpoint を実装、test build は I915_TESTS=y、vkloop-hw.sh oracle）。参照 kernel build は S5 |
| S4 | 完了（2026-09-22） | 実機: 共有 route の静止フレーム rgb_sha256=94615464…19b1（E-130 と同一）、LCD-B レジスタ Linux dump 一致 17/17、ended PASS。40 s アニメーション 695 frame、ended PASS（stop confirmed、buffer 解放）、写真 handover/vk-e127/vkdemo-5330-lcd-rebuild-s4.jpg。offscreen hash 7523debe…05ff も不変。Linux 由来ファイルの MIT notice を復元。報告 i915-rebuild-s4-reports.md |
| S5 | 完了（2026-09-22） | host: vk fixture 全一覧、gentool、analyzer、表示 5 script（件数・出力とも旧と同一）、contract 6 本、ws029 ppgtt/stream、build-selection、check_generated すべて PASS。実機（I915_TESTS=y、vkloop-hw.sh test <scenario>）: ktest 382/0（13 skip）、EU/draw/R1/tex/T3/bilinear が旧台帳の hash・件数と一致、display_ktest 182/0（1 skip）、LCD-B（17/17）/LCD-C/LCD-D PASS。本番は weak な drv_i915_test_after_start だけで、offscreen/表示とも不変。報告 i915-rebuild-s5-reports.md、対応表 i915-rebuild-coverage.md |
| 次 | 専門家コードレビュー | i915-old は参照用に残す。レビュー対応後に削除 |

## 5. 移行表（旧 → 新）

段階ごとに追記する。S1 の詳細な対応表は各担当の報告（i915-rebuild-s1.md と作業ログ）にある。要点:

- osdep mmio/trace/pci/dma/runtime_pm/firmware/sync + backend_*.c + wait.c → mmio, trace, pci, dma, runtime-pm, firmware, sync, workqueue
- gt_mmio.c → device-info.c、reset.c → reset.c、pcode.c → power.c
- gt_init_base.c + gt_wa_adlp.c → workarounds.c、gt-power.c（RC6/RPS）、gt_verify_wa.c → verify-workarounds.c
- gt_mem.c + pte.c + legacy gem.c/ppgtt.c → memory.c, ggtt.c, ppgtt.c、gt_tlb.c → tlb.c
- gt_engine/gt_lrc/gt_request/gt_submit/gt_resume/gt_defaults/gt_migrate/pxp → engine, context, request, submit, defaults, migrate, pxp
- parity/irq.c（GT 半分）→ irq.c（表示半分は S4）
- legacy i915.c ops → session, resource, command, job, reset（recovery）、legacy request.c → request-queue.c、legacy_shim.c（非表示）→ worker.c
- probe.c（P0–P2、P4 の GT、P6、P7 pxp）+ runner.c → device.c、gt.h
- 廃止（本番から到達しない）: legacy uncore.c, ggtt.c, engine.c, lrc.c, irq.c
- firmware_*.c → data/firmware/*.c、linux/*.inc → data/*.inc（gen-inc.py の出力先も変更）。2026-09-22 に `data/` を `external/` に改めた:
  GT の定義は `external/i915.h`、表示は `external/display/<系統>.h`（DisplayPort は `external/display/dp.h`）、配列の本体は `external/i915-*.inc`、
  firmware は `external/firmware/`、manifest は `external/provenance/`、`vulkan-codec.inc` は `render/`。gen-inc.py は廃止（`handover/tools/retired/`）。
  同日 `external/` を `intel/` に改めた: 表示の header は `intel/` 直下（`intel/dp.h` など）、配列の本体は `intel/engine-table.inc`・`forcewake-ranges.inc`・`mocs-table.inc`・`mcr-ranges.inc`、firmware は `intel/firmware/`（一時）、manifest は `intel/provenance/`（いずれも下記 §5.1 のとおり 2026-09-22 に削除）。
  `external/i915.h` は内容ごとに `intel/bits.h`・`gt-regs.h`・`commands.h`・`lrc-offsets.h`・`pci-ids.h`・`gt-power.h`・`workarounds.h`・`mocs.h`・`genxml.h` に分割して削除した（各 .c は使う header だけを include、vmunix はバイト一致）。
  表示の定義の DRM 接頭辞を外した（`drm_dp_*`→`dp_*`、`DRM_FORMAT_*`→`FORMAT_*` ほか、[licence 棚卸し](license-inventory.md) の冒頭）

### 5.1 廃止・未移植の記録（監査 i915-rebuild-coverage.md の指摘による）

- legacy request.c の drv_i915_request_kick、drv_i915_request_retire、i915_request_emit、i915_request_emit_prologue、
  i915_request_emit_breadcrumb: 呼出元は廃止した legacy engine.c と、PARITY_SHIM_REDIRECT で serving thread
  （現 worker.c の drv_i915_worker_kick）へ置換済みの i915.c だけ。本番から到達しないため廃止。
- 旧 vk の res.c、pipe.c、cmdbuf.c、wsi.c、display.c、sync.c（fence の opcode 35–38 を除く）、surface-state-gen12.inc:
  E-127 以前の設計。libvulkan（vkdemo の offscreen・direct display）から到達しない（S3a の調査、i915-rebuild-s3-reports.md）。
  dispatch はこれらの route を XXX で拒否する。移植しない。
- parity/legacy_shim.h の名前差替えマクロ（PARITY_SHIM_REDIRECT）: 通常の関数呼出し（worker.c）に置換済み。廃止。
- 生成器 port_lcd_calc.py、port_dp_aux_pps.py、port_intel_bios.py: S4 §9-6 で廃止（表示コードは手で管理）。
  manifest は一時 intel/provenance/ に保存したが（当初は data/provenance/、次に external/provenance/）、2026-09-22 にユーザー決定で
  intel/provenance/ ごと削除した。
- intel/firmware/（2026-09-22、ユーザー決定）: DMC の byte 配列 adlp-dmc.c・tgl-dmc.c は kernel から削除し、firmware.c は
  /lib/firmware/<name> を VFS から読む（package i915-firmware、無ければ ENOENT で DMC なし）。対象機 VBT は
  vendor/intel-vbt/dell-latitude-5330-1028-0b02.inc へ移し、試験 build（I915_TEST_VBT=y）だけが display/vbt.c に include する
  （XXX: QEMU passthrough の guest に OpRegion が無いための支え。実機で GPU 試験が走るようになったら削除）。使われていなかった
  Dell Latitude 5320 の VBT と、その pin 行・ktest の skip 行は削除した。intel/firmware/ は削除。

