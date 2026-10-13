<!-- awesome-plan project=zedbsd record=ws074-p179 -->

# ws074-p179: Acid3 参照ページの背景画像を表示する

Status: cleared（2026-10-13 q1001-i01。参照画像の canvas 描画を修復し、同条件で再計測）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Prerequisites: p172 cleared、[p178 q1000 の baseline と証拠](../phase178/phase.md)。本 Phase の verified 出力後に p178 の1領域試行を再開する。
Queue: [q1001-i01](../queue.md)

## 目的と受け入れ

固定 WPT `2d66b9b7998bb58c336138c178323ddee857b586` の `acid/acid3/reference.sub.html` が指定する `reference.png`（665×454、6色の箱）を、現行ブラウザが背景画像として取得・decode・描画する。テスト入力・参照画像・pixel 比較方式は変更しない。修正は実際に確認した一般的な background-image の欠陥に限る。

- `reference.png` が reference の画面と paint 診断に現れ、画像の位置・寸法・色を実ファイルと比較できる。単に画像を強制で貼る処理を入れない。
- 修正後の Acid3 を同じ固定条件で測り、得点100/100・例外0を維持し、全体の差画素が q1000 の302056を超えない。Acid2 の差1865も増やさない。改善しない場合は原因と後続判断を記録し、clearしない。
- 変更箇所の host build warning0、関連する短い host 試験、全文 C 規約の適合を確認する。必要な別 OS / QEMU の確認は T1 へ依頼し、未実施を明記する。

## 最大90分の調査・修正範囲

1. 現行 `acid3-reference.layout` と `acid3-reference.paint`、`reference.png` を照合する。最小の背景画像ページでも再現するかを確認し、画像 URL の解析、解決、HTTP 取得、PNG decode、layout の image pointer、canvas painting の順に観察する。該当 component の一般的な契約を確認する。
2. 一つの原因を特定し、直す file/関数と期待する変化を Phase に記してから最小修正する。候補は `userland/desktop/libbrowser/css/`・`page/images.c`・`layout/box.c`・`paint/list.c`。担当 source は `libbrowser/` と WS074 の試験のみ。
3. 同じ runner で前後の画像/JSON/paint を保存する。Acid3・Acid2 の変化と、変更箇所の短い host 試験を確認する。最終の p100 全画面差0は本 Phase の受け入れ条件ではない。

原因を1つに絞れない、複数 module の設計変更が必要、または90分に達した場合は attempt を uncleared にして証拠・再開条件を残す。新しい Queue の exact scope と開始は別途記録し、q1000 から自動で継続しない。

## 出所と設計変更の配達

2026-10-13 q1000-i01 / [p178](../phase178/phase.md): ユーザー指定の最初の1領域試行で、現行 Acid3 は得点100/100、差302056/480000。参照側の `reference.png` の色の箱が描かれず、画面全体が不一致になった。p178 の局所差を選ぶ前に、この Phase で参照の背景画像経路を確認する。p178/p100 の受け入れ条件は緩めない。WS074 の一覧と再開条件へ投影済み。

## q1001-i01 原因確認（修正前）

2026-10-13 22:31 JST: `reference.sub.html` の `<html>` と `<body>` はともに高さ 0、canvas は 800×600。参照の paint dump に背景画像 item はない。同じ `reference.png` を通常の `<div>` の `background` shorthand と `<img>` で描く最小ページでは表示される。一方、絶対配置の子だけを持つ body の background shorthand では、body の背景色だけが canvas に届き画像は欠ける。URL 解析・取得・PNG decode ではなく、`userland/desktop/libbrowser/paint/list.c` の `list_canvas_background()` が高さ 0 の root padding box を画像の位置決め領域に渡し、`list_background_image()` が高さ 0 を理由に描画を省くことが直接原因。[CSS Backgrounds 3 §2.11](https://www.w3.org/TR/css-backgrounds-3/#special-backgrounds) では body の背景を root に伝播し、canvas 全体に描きながら画像の位置決めは root box に従う。位置決め領域の高さ 0 でも `top left` の自然寸法画像は描画可能。`list_background_image()` の位置決め領域 0 判定のみを除き、実際の描画領域 0 の判定を維持する。通常 box の背景・`background-size` 算出を変えない。

## q1001-i01 結果 / clearance

2026-10-13 22:35 JST: `paint/list.c` の画像位置決め領域が 0 の場合の early return を外し、canvas の描画領域 0 の判定は残した。固定 WPT の参照ページの paint dump は `image 0.00 0.00 665.00 454.00 665x454`、[並列画像](evidence/20261013-q1001/acid3-side.png)に6色の箱を確認。`reference.png` は変更していない。絶対配置の子だけを持ち root 高さ 0 の[回帰ページ](../tests/pages/canvas-background.html)と[host 試験](../tests/host-canvas-background.c)で画像 2×2 が canvas 原点に描かれることを確認した。

`sh plan/ws074/tests/host-build.sh plain` は終了値0、warning/error 0。`build/ws074-host/plain/host-canvas-background` は pass、`host-image-zero` は10/10 pass。`python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1001` は終了値0。固定 WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`、Acid3 800×600・settle 15000 ms: 得点100/100、inventory 100/100、例外0、RGB一致88.1985%、差 **56647/480000**（q1000 の302056より245409減）。Acid2 400×300: RGB一致98.4458%、差1865/120000（q1000と同じ）。[report](evidence/20261013-q1001/report.json)、Acid3/Acid2 並列画像と参照 layout/paint dump を同じ証拠 directory に保存。`git diff --check` pass。変更した C と host 試験を C 全文規約・browser component 規約で確認し、新規の違反は見つからなかった。別 OS / QEMU の実行は未実施。

この Phase の条件（一般的な欠陥の最小修正、参照画像の表示、Acid3/Acid2 非増加、host 検証）は満たした。Acid3 の残り56647差、Acid2 の既存1865差は p178/p100 の残務であり、本 Phase では修正しない。p178 の以前の q1000-i01 は uncleared のまま保持し、新しい1領域の試行は新しい Queue の exact scope/承認後。p100/WS074 は未達・incomplete。GitHub Issue/Project 公開は設定上保留、local 記録は保存した。
