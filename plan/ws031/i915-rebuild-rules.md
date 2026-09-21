# i915 再構築 — 移植作業の共通規則

[実施計画](i915-rebuild-plan.md) の §3 を具体化した、1 関数ずつの移植で守る規則。
対象ツリー: centris `~/zedBSD-gpu`。旧ソース: `src/drivers/gpu/i915-old/`（読むだけ。変更しない）。
新ソース: `src/drivers/gpu/i915/`。

## 1. 書き方

- `plan/coding-style.md` に全面準拠する。特に: ANSI C の宣言位置、static 関数の 1 行前方宣言、
  関数定義の引数 1 行 1 個、公開関数の複数行コメント（動詞＋目的語）、static 関数の 1 行コメント、
  段落ごとのコメントと空行、条件の中で関数を呼ばない、`return error;` で終わらない（失敗と成功を分ける）、
  最後の文は成功 return、型とファイルスコープ変数に役割コメント、多行コメントの区切りは単独行、
  `for` の初期化で宣言しない、条件演算子は短い対称な選択だけ、`goto` は単一の cleanup ラベルへの前進のみ。
- 見本: `src/drivers/gpu/i915/mmio.c`、`trace.c`、`device.c`。
- 新ファイルの先頭は著作権ヘッダ（Zlib）＋ファイルの説明。Linux／Mesa から写した表・定義は `intel/` の系統別 header
  （GT は `intel/gt-regs.h`・`commands.h`・`lrc-offsets.h`・`pci-ids.h`・`gt-power.h`・`workarounds.h`・`mocs.h`、Mesa の 3D state は
  `intel/genxml.h`、表示は `intel/<系統>.h`、DisplayPort は `intel/dp.h`）に置く。配列の初期化子の中で include する行の並びだけは
  `intel/*.inc`。先頭は zedBSD の著作権行（licence 行なし）、出典の licence block（その header が写した出典の Copyright 行だけを統合）、
  出典（版、ファイル、sha256）を書いた説明、include guard、定義の順（既存の `intel/` のファイルと同じ扱い）。header は使う header
  （`intel/bits.h`、`<stdint.h>` など）を自分で include し、各 .c は使う header だけを include する（まとめ役の header は作らない）。
- コメントに設計資料の番号（E-xxx、R1 など）や「parity」「osdep」「旧ファイル名」を書かない。
  Linux の対応関数名（例: `intel_gt_init_mmio()`）は、処理の意味を説明する場合に限り書いてよい。
- `kern_logf` の文言は、移植前と同じ情報を出す（実機ログの比較に使う）。接頭辞は `i915: ` に統一し、
  `parity ` や `P6c` のような段階名は、ログの意味に必要な場合だけ残す。

## 2. 名前

| 旧 | 新 |
| --- | --- |
| 公開関数 `osdep_xxx` / `parity_xxx` / `drv_i915_xxx` | `drv_i915_xxx`（`parity_intel_` は `drv_i915_` に、Linux 名の意味は保つ） |
| static 関数 | `i915_xxx`（ファイル内で意味の分かる名前） |
| `struct osdep_xxx` / `struct parity_xxx` | `struct i915_xxx` |
| 定数・マクロ `OSDEP_XXX` / `PARITY_XXX` | `I915_XXX` |
| forcewake `OSDEP_FW_*` | `I915_FORCEWAKE_*`（`mmio.h`） |
| `osdep_mmio_read32` / `write32` | `drv_i915_read32` / `drv_i915_write32`（held） |
| `osdep_mmio_raw_read32` / `raw_write32` | `drv_i915_raw_read32` / `drv_i915_raw_write32` |
| `osdep_mmio_read32_auto` / `write32_auto` | `drv_i915_read32_auto` / `drv_i915_write32_auto` |
| `osdep_mmio_posting_read32` | `drv_i915_posting_read32` |
| `osdep_mmio_write32_masked`（RMW） | `drv_i915_rmw32` |
| `osdep_mmio_write32_mask_enable` | `drv_i915_write32_masked` |
| `osdep_fw_get` / `put` / `is_held` | `drv_i915_forcewake_get` / `put` / `held` |
| `osdep_mcr_lock` / `unlock` / `is_locked` | `drv_i915_mcr_lock` / `unlock` / `locked` |
| `osdep_trace_emit(t, stage, op, ...)` | `drv_i915_trace_record(trace, stage, op, ...)`、`OSDEP_TR_X` → `I915_TRACE_X` |
| `struct osdep_mmio` / `osdep_trace` | `struct i915_mmio` / `i915_trace` |

変数名は規約 §4（保持する値の名前）に合わせて付け直してよい。

**構造体のフィールド名は変えない**（並行して移植する他の担当が旧フィールド名で参照するため）。
フィールドの改名は統合時に一括で行う。構造体・関数の名前は上の機械的な規則だけで決める。

## 3. エラー

- 戻り値の errno は**正の値**（zedBSD の規約）。旧 osdep/parity の `-errno` は正に直し、呼出し側の
  `< 0` 判定も `!= 0` に直す。Linux の負 errno を返す関数の意味（例: -ENODEV を許容）は、
  呼出し側で同じ判断になるよう直す。
- 未実装の経路は `XXX:` コメントと `kern_logf("i915: XXX ...")` で名乗り、成功を装わない。

## 4. 挙動

- 移動は挙動を変えない。レジスタ書込みの順序、値、posting read、待ち時間、forcewake の取り方、
  ログに出す値を保つ。バグに見えても直さず、`XXX:` で指摘して報告する。
- 試験専用の分岐（`PARITY_*_TEST`、`PARITY_RESIDENT_SERVE_S` など）は本番へ持ち込まない（S5 で tests/ へ）。
  `PARITY_RESIDENT` / `PARITY_RESIDENT_DISPLAY` は「常に真」として扱う。
- `static` なローカル変数に置いていた長寿命状態は、所有者（device など）の構造体へ移す。

## 5. 確認

- 1 ファイルごとに `plan/ws031/tests/i915-cc.sh <file.c>`（カーネルと同じフラグで compile のみ）。
- build list（`platform/amd64/vmunix.mk`）への追加は統合担当が行う。
- 移植した関数は [実施計画](i915-rebuild-plan.md) §5 の移行表に「旧ファイル:旧関数 → 新ファイル:新関数」で追記する
  （統合担当がまとめる。作業者は報告に一覧を含める）。

## 6. 表示（S4）の追加規則

- Linux 由来の型名・マクロ名（`struct intel_crtc_state`、`struct drm_display_mode`、`REG_BIT` 由来の定義など）は
  移動中は名前を変えない（別段階で一括改名する）。`parity_` の付く名前は §2 の規則に従う。
- Linux 名の static 関数は接頭辞を `i915_` に置き換える（`intel_ddi_foo` → `i915_ddi_foo`、`skl_x` → `i915_skl_x`、
  `drm_x` → `i915_drm_x`）。公開関数は `parity_intel_x` → `drv_i915_x`、それ以外の `parity_x` → `drv_i915_x`。
- 旧 `*_glue.inc` の関数本体は対応する `.c` へ普通の関数として統合する（データとして隠さない）。
- 同名の iterator マクロや glue の大域変数が翻訳単位ごとに別の意味を持つ場合（`for_each_intel_crtc_in_pipe_mask` など、
  レビュー A09/A14）、意味ごとの明示ループ・明示引数へ展開してから統合する。一つのマクロへ統合しない。
- 表示の状態は `display/internal.h` の `struct i915_display`（device が所有）と、その下の pipe/scanout 等の構造体に置く。
  旧 probe.c の static 局所変数（power_domains, cdclk, dcore, dstate, nogem, edp_dev, dmc_dev, dprobe, vbt_state, pch, drm_dev, vga_client, pmdemand, pwc, bw_state, dram_info）と glue の大域状態をここへ移す。
