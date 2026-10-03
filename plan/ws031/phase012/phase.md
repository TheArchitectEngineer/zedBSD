# WS031 p012 計画: レビュー — 静的解析・規約全文確認・回帰・制限整理

実装モジュールを書かない。WS031 全体の静的レビュー、コーディング規約全文確認、回帰、ライセンス最終確認、制限整理を行う。おおまかな設計。

## Module と所有ファイル
- doc のみ: `plan/ws031/results-ws031.md`（成果）、`plan/ws031/phase012/style-review.md`、`plan/ws031/i915-vk-license-audit.md`（最終）
- fixture/analyzer: `plan/ws031/tests/run-i915-vk-analyzer.sh`（gcc -fanalyzer / clang --analyze、WS029 の analyzer と同型）

## 実装する公開インタフェース（規約・正本）
なし（レビュー）。

## 内容
- 静的解析: `vk/**` 全 source に gcc `-fanalyzer` / clang `--analyze`。0 件を目標、指摘は修正。
- 規約: `plan/coding-style.md` §14 を全新規 `.c`/`.h` に適用確認（file section、forward declaration、purpose comment、`Succeeded:` return、条件分割、for 初期化子、in-kernel の float/再入/スタック方針）。
- 転記 `.inc`: `gen-inc.py` 相当で出典・SHA・変換規則の整合、未転記 symbol 参照なし、Mesa/PRM の MIT 監査 exit 0。
- 回帰: WS031 host fixture 全 PASS（通常＋ASan/UBSan）、WS029 の回帰（i915 core）に影響なし、build 3 構成 warning 0、GPU なし build で vk symbol 0、`git diff --check` PASS。
- 制限整理: 最適化なし、機能 subset、単一 RCS0 直列、eDP 1 枚 modeset、対象 SPIR-V/命令の範囲、非 LLC 未対応等を明記し、後続を registry の planning 行へ。

## 依存
- 前段: p001–p011 全て。
- WS029 core: 回帰確認のみ、変更しない。

## 触れるファイル / 触れないファイル
- 触れる: `plan/ws031/**`、`plan/ws031/tests/run-i915-vk-analyzer.sh`。指摘修正は該当モジュール Phase へ差し戻すか、軽微なら本 Phase で最小修正（記録する）。
- 触れない: HAL、UAPI、WS029 core ロジック、libvulkan。

## 受け入れ条件と試験
- 静的解析 0 件、規約確認 OK、回帰 PASS、監査 exit 0、`git diff --check` PASS、制限が文書化。

## 見積・制限
180 分。実機描画の受け入れは p011 が正本。本 Phase はレビューと整理。

## 完了（build-passing 基準）
静的解析 gcc -fanalyzer / clang --analyze: vk 全 11 source で 0 件（`plan/ws031/phase012/analyzer-*.log`）。全 8 host fixture（cmd/spirv/res/sync/eu/compile/pipe/cmdbuf）通常＋ASan/UBSan PASS。i915 kernel build PASS（vmunix check、warning 0、`-mgeneral-regs-only`）。WS029 host fixture 全 PASS（回帰なし）。vk は CONFIG_DRIVER_PCI_I915 で gate（GPU なし build から除外）。git diff --check clean。転記 `.inc` は Mesa 出典・SHA 記録（i915-vk-license-audit.md）。
