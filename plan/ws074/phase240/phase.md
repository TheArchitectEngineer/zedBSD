<!-- awesome-plan project=zedbsd record=ws074-p240 -->

# ws074-p240: flex item の aspect-ratio と自動最小サイズ

Status: uncleared（2026-10-18 host の有限部分は cleared、target の確認と残る flex basis 条件は未実施）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: [ws074-q20261018-01](../queue.md)

## 範囲・目的・受け入れ

p234/p235 の残りから、横書き flex item の preferred aspect-ratio と main-axis の自動最小サイズ・flex basis を調査し、独立の最小例と固定 WPT で確認できる共通原因を直す。row/column、明示的な cross size、flex-basis:0、content/border-box の差を確認する。全31件の成功や WS の完了を今回の受け入れにはしない。

変更対象は `userland/desktop/libbrowser/layout/flex.c` と必要な WS074 の試験・記録。公開 ABI、shell、他 WS の実装は範囲外。実装は通常の production path を使う。C 全文 [coding-style.md](../../coding-style.md)、[Guardrail](../../guardrail.md)、[component 全文](../../standards/browser-component.md) を適用する。

依存: p172 の統合済み engine と p234 の aspect-ratio 実装。コードの存在を確認済み。p234/p235 の残りという scoped output を使い、未完の WS 全体の clearance は前提にしない。

## 手順・検証・上限

最大90分。固定 WPT `2d66b9b7998bb58c336138c178323ddee857b586` の aspect-ratio 領域と独立例の修正前出力を保存し、spec と照合して有限の修正を行う。専用 host build（warning 0）、変更箇所の短い host 試験、同じ WPT ID の前後、Acid3 差0・Acid2 非増加を確認する。変更関数を全文規約で review し、formatter と diff check を行う。QEMU・実機は別の試験担当の関門として残し、host の部分結果と区別する。

仕様: [CSS Flexbox §4.5](https://www.w3.org/TR/css-flexbox-1/#min-size-auto)、[§9.2](https://www.w3.org/TR/css-flexbox-1/#algo-main-item)。結果と残りは本記録へ追記する。

## 開始（2026-10-18）

ユーザー「では、WS074を続けてください。」を受け、既存の後続有限 Queue の事前承認と p234/p235 の残りへ適用。Codex が単独実行。p239 までの実装を確認した。WPT の旧 symlink 先は再起動で消失していたため、同じ固定 revision を再取得中。GitHub の公開保留を維持し、Master/共有 Queue の projection は Q1 に pending。

## ws074-q20261018-01-i01 結果（2026-10-18）

Event ws074-p240-host-partial-clear-20261018: 有限の host 部分を cleared とする。[証拠](evidence/20261018/README.md)と[257 IDの前後](evidence/20261018/results.json)。ratio item の自動最小サイズを line collection・flexing の前に計算するよう修正し、basis0・row/column・definite percentage・box sizing・cross制約を確認した。WPT142→149/257 PASS、改善7・後退0、独立29/29、DOM23/23、alignment5/5、host/amd64 build warning0、Acid3差0、Acid2差873を維持。変更したC関数の全文規約reviewを実施した。Chromiumとは28例が一致し、explicit heightとmax-heightが競合する1例は仕様の期待と参照の差を明記。

Phase whole は uncleared。QEMU・実機・GPU、stretchで決まるcross size、flex-basis:contentとautoの区別、画像の自動最小サイズは残る。次の有限範囲の候補はfixed009/045/046のstretch cross sizeと、021/022のbasis区別。Interop全3,805件・WS074 acceptanceの達成は主張しない。新しい製品判断は行っていない。公開ABI/Wayland境界の変更はない。Master・共有Queue/GitHubの投影はpending。

Commit receipt: 実装・試験・証拠は `53f46c61b`（WIP）。`git log` と `git show --stat` で11 fileの保存をread-back確認。pushは未実施。

Event ws074-p252-basis-plan-20261018: [p252](../phase252/phase.md)へ既存content vs autoとgrammar/computedの有限候補を具体化。非置換horizontal content baseと保存・復元/cacheを検証し、replaced/target等の本Phaseの未達は保持。q14 final outputのgate後に新Queue選定が必要、まだ実装しない。


Event ws074-p252-q21-selected-20261018: [p252](../phase252/phase.md)でcontentとautoの区別/非置換content baseを選定。本Phaseのverified ratio cross/auto minimum outputを保持し、preferred main/minmaxを測定だけ除去する。replaced/target等本Phaseの未達/historyは不変、dependent final全1491/static1561/strict48/scripted413で後退を検証。shared projection pending。


Event ws074-p252-q21-output-20261018: [p252 host partial](../phase252/evidence/20261018-q21/README.md)でnon-replaced content vs auto outputをverified。既存ratio40/40を保持、whole fixed改善7/全旧PASS後退0。本Phaseの以前のattempt/target/replaced等残件は不変、p252 whole/本Phaseをこの転記だけでclearしない。shared projection pending。
