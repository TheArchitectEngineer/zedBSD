<!-- awesome-plan project=zedbsd record=ws074-p183 -->

# ws074-p183: CSS hsl()/hsla() 色と Acid3 得点 slash

Status: cleared（q1006-i01、2026-10-13 23:13 JST）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172/p179/p180/p181/p182 cleared、[q1005](../phase182/evidence/20261013-q1005/report.json) の差画像・style/paint。
Queue: [q1006-i01](../../history/queue-q1006.md) cleared。2026-10-13、ユーザーは画素差0まで最大90分 Queue を何度でも自動実行するよう指示。下記 exact scope を記録して選定。

## 観測と目的

固定 WPT Acid3 の `#slash` は `color:red; color:hsla(0, 0%, 0%, 1.0)`、`#\ ` は `color:transparent; color:hsla(0, 0, 0, 1)`。q1005 の slash は赤で、上部の見えてはいけない文字も可視。`css/values.c` の `css_parse_color()` は rgb()/rgba() だけを受け付け、hsl()/hsla() を EINVAL とする。[CSS Color 4](https://drafts.csswg.org/css-color-4/#the-hsl-notation) に沿い、HSL/HSLA 値を一般的に RGB へ変換する。

## q1006-i01 exact scope / clearance（最大90分）

`css/values.c` の色関数解析に、HSL/HSLA の伝統的なカンマ区切り形式および現在の空白/solidus 形式を焦点を絞って追加する。色相 degree/angle、S/L の percent と現在の number、alpha を扱い、一般的な HSL→sRGB 変換をする。無効な値は既存の宣言 fallback を保つ。`#slash` と上部文字の style/paint を前後比較し、局所的 host 試験で代表色、alpha、無効入力、RGB 既存値を確認。host build warning0、変更 C と browser component 全文規約確認、固定 WPT Acid3/Acid2 再計測を行う。WPT/runner/font/参照画像は変えない。

Acid3 score100/100、inventory100/100、例外0、差 **13302/480000** の非増加、Acid2差 **1865/120000** の非増加が条件。大きな parser 設計変更、範囲外の色空間実装、差増加、または90分到達時は uncleared とし証拠と再開条件を残す。p100 の全画面差0は別条件。

## q1006-i01 結果（2026-10-13 23:11〜23:13 JST）

**cleared**。`css/values.c` に hsl()/hsla() のカンマ区切り・空白/solidus 形式を追加し、degree/turn/grad/rad、S/L percentage/modern number、alpha を sRGB の packed RGBA に変換。無効値は EINVAL のまま。`host-hsl-color` は代表色・alpha・角度・無効値・既存 RGBA の 14/14 PASS。host build warning/error0、`git diff --check` PASS。`clang-format` は環境に無く、変更 C と browser component 全文規約を手動確認。QEMU/別OS、main 最新 HEAD 全体の再buildは未実施。

固定 WPT `run-acid-tests.py --out build/ws074-acid-q1006-final`: Acid3 score100/100・inventory100/100・例外0、差 **12600/480000**（97.375%一致、q1005 の13302から702減）、Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1006/report.json)、[Acid3並列画像](evidence/20261013-q1006/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1006/acid2-side.png)、前後 style/paint を保存。`#slash` の赤は黒になった。上部の余分な文字は赤いままで、この要素の selector/hidden 処理を次原因として分離。p100全画面差0は未達。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
