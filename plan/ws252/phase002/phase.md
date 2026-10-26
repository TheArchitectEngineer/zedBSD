<!-- awesome-plan project=zedbsd record=ws252-p002 -->

# ws252-p002: Gen12 の platform の記述の行（TGL・ADL-P・RKL・ADL-S・RPL-S）と golden

Status: in-progress（2026-10-26 q1599 P4: 実装・host 試験・build・5330 の回帰 PASS。cleared は Q1）
Parent: [WS252](../ws.md)
Queue: q1599-i01（P4）
設計: [design.md](../design.md) §4・§5・§6.1

## やった事（f8b65c839）

- 新しい `src/drivers/gpu/i915/product.c/.h`: PCI ID → 行（graphics・display の版、platform、engine の mask、platform だけの WA の bit `platform_wa`、名前）の表。行は TGL・RKL・ADL-S・RPL-S・ADL-P 系・SKL・KBL・CFL・CML。RKL は VCS2 無し、Wa_1607297627（render の PSMI）は TGL・RKL・ADL-P 系だけ（Linux v6.8.12 `i915_pci.c` の `platform_engine_mask`、`intel_workarounds.c` の `rcs_engine_wa_init`）。`drv_i915_product_kind` を device.c から移した。
- device.c: 旧の表と `i915_product_listed`・`I915_START_ENGINE_MASK*`・`I915_ID_VALUE` を消し、engine の mask は行から（`gt->platform_engine_mask`）、`gt->info.platform_wa` を行から。`gt->is_alderlake_p`（読む所が無い）を消した。
- device-info.h: `I915_PLATFORM_WA_RENDER_PSMI` と `i915_gt_info.platform_wa`。workarounds.c: Wa_1607297627 の rule に条件 `i915_wa_render_psmi`、`drv_i915_wa_build_engine` は版の代わりに GT の情報を受ける（ktest-gt の呼び出しを追従、試験の GT に ADL-P の bit）。
- intel/pci-ids.h: RKL・ADL-S・RPL-S の ID の表（Linux v6.8.12 `i915_pciids.h`）。memory.h の DMA の注記（全部の platform が 39 bit）、mmio.c の forcewake の表の注記（Gen12 の全部）。
- golden: `plan/ws229/tests/gen12-golden/` を今の API に直して `plan/ws252/tests/gen12-golden/` に置いた（旧の dir は Q1 が merge の時に消す）。基準の commit は 684a5243f（WS252 の前の main）、tree の側は product の表の ADL-P の行（0x46a8）で走らせ、続けて 5 つの行（TGL 0x9a49・RKL 0x4c8a・ADL-S 0x4680・RPL-S 0xa780・ADL-P 0x46a8）を走らせる。`check-linux.py` が Linux の tree（`build/linux/linux-6.8.12`、kernel.org の tarball を autosigner の署名と sha256 で確かめた）から機械で読んだ期待（ID の表、`platform_engine_mask`、`rcs_engine_wa_init` の platform だけの条件の block の register）と golden の出力を比べる。

## 確かめた事（2026-10-26）

- `sh plan/ws252/tests/gen12-golden/run.sh`: `gen12 golden PASS: 543 lines identical to 684a5243f`、`gen9 smoke: 202 register writes, 24 steps rc=0, 0 steps failed`、`linux check: PASS`（5 行の ID と engine が Linux の info と一致、RKL・ADL-S・RPL-S の ID の表が Linux と一致、Wa_1607297627（0x02050）は tgl rkl adlp だけ、他の 6 block は 5 行全部）。検出の確かめ: ADL-S の rcs0 の list に 0x02050 を足した写しで `linux check: FAIL`（試験の側が誤りを捉える）。
- build: 既定・`I915_TESTS=y I915_TEST_SET=execution`・`vke2` の 3 組 rc=0、warning 0、amd64 vmunix check PASS。`run-xd-host-tests.sh` all passed。`git diff --check` 空。
- 5330（素の起動、2026-10-26、kernel だけ差し替え、`plan/ws245/tests/5330-run.sh --kernel`）: ssh まで 44 s、survey lit、基準（q1229-i21）と steady の差 0、書き込みの digest が基準と同じ → PASS。証拠 `plan/ws245/tests/evidence/p4-ws252-f8b65c839-20261026.md`、画面 `plan/ws245/temp/5330/p4-ws252-f8b65c839-20261026.png`（git の外）。

## 制限

- `identifiers[]` と product の表は同じ `I915_PCI_*_IDS` の macro から作る 2 つの表のまま（attach の表は PCI の driver の形が要る）。xd の表（display）との統合はしていない（design §6.1: WS245 と合わせる、つかなければ今のまま）。
- RKL・ADL-S・RPL-S の実機は無い（D5「無いまま」）: fuse・MCR の steering・WA の read-back は未確認。
