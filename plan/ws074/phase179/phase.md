<!-- awesome-plan project=zedbsd record=ws074-p179 -->

# ws074-p179: Acid3 参照ページの背景画像を表示する

Status: planned（2026-10-13 q1000/p178 の全画面差を受けて追加。Queue 未選定）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Prerequisites: p172 cleared、[p178 q1000 の baseline と証拠](../phase178/phase.md)。本 Phase の verified 出力後に p178 の1領域試行を再開する。
Queue: なし

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
