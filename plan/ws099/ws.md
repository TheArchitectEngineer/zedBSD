<!-- awesome-plan project=zedbsd record=ws099 -->

# WS099: Keiland のデモの仕上げ（5330、2026-10-10 まで）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（台本の確定）から。台本の案はユーザーの確認待ち
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

WS035 を閉じるときのユーザーの判断:「閉じて新しい WS（Keiland のデモの仕上げ、10/10 まで）」。
ユーザーの指摘:「WS035は、あまりゴールが明確でないのに延々と作業している気がします。」→ この WS はゴールを**デモの台本**に限る。

**完了の条件**: 下の台本の全ての場面が、**Dell Latitude 5330 の実機**（内蔵 LCD、`display=edp`、デモの image）で、崩れ・止まり・読めない文字・
操作できない所なく通る。各場面の受け入れの証拠は、QEMU の自動の通しの試験と、実機でのユーザーの確認（またはユーザーが撮った画面）。
台本に無いものはこの WS で作らない（見つけたら Future Work か Bug Board）。期限は 2026-10-10 ごろ（それ以降は bug の修正と実機の調整だけ、master の判断）。

## 台本（案、2026-09-30 main。ユーザーの確認待ち）

| # | 場面 | 見せること | 受け入れ |
| --- | --- | --- | --- |
| S1 | 起動 | 電源から Kei の起動画面、greeter | 黒・文字の console が出ずに greeter まで。時間を記録 |
| S2 | login | kei で login、デスクトップ | 壁紙・system bar・デスクトップの icon が出る |
| S3 | App Home | 左上から App Home、app の一覧、検索 | 開閉が滑らか、icon が崩れない |
| S4 | Files | Files を開き、folder を移動、表示の切り替え | 文字が読める、scroll が滑らか（mouse・touch） |
| S5 | 画像 | Files から png・jpg を double click → Image Viewer、拡大・pan・前後 | 開く、操作に追従する |
| S6 | text | Files から txt → Text Editor、編集・保存（Save As のファイル選択の窓） | 開く、保存できる |
| S7 | Settings | 壁紙の差し替え、窓の透明度、検索 | 変更がすぐ当たる、暗い壁紙でも文字が読める |
| S8 | Notes | 右上の角の swipe で Notes（全画面）、pen で書く、Esc で窓に | 書ける、全画面を解いて窓を動かせる |
| S9 | PDF Viewer | PDF を開き、頁送り・拡大 | 開く、操作に追従する |
| S10 | Terminal | `ls`・矢印の履歴・Tab の補完・`less` | 本体の角が直角、文字が欠けない |
| S11 | 窓の操作 | 10 個ほどの窓で移動・resize（四隅と辺）・Wiseview・最大化 | 操作が軽い（目安: 入力から表示 50 ms 以内） |
| S12 | 終わり | Log Out → greeter、Shut Down | 黒・止まりなく戻る・切れる |

ブラウザ（Amazon）と IME は、今は人間が作業中（master の判断）。戻ったときに台本に足すかをユーザーと決める。

## 既知の項目（p001 で台本の場面に割り当てる）

| 項目 | 場面 | 出所 |
| --- | --- | --- |
| 暗い壁紙の上のすりガラスの文字の contrast（旧 ws035-p135） | S7 | WS089 の p009、すりガラスを残す決定（2026-09-30） |
| [BUG-115](../bugs/BUG-115.md): zdesktop-p072 の試験の失敗（最初の client の setup errno=5） | 回帰 | ws094-p002 |
| 窓 10 個の軽さ（WS075 p023: 分岐の中の ALU を飛ぶ） | S11 | WS075（この WS の外で進む） |
| 実機の確認の結果（`build/demo-lcd8`） | 全て | ユーザー |

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws099-p001 | 台本の確定（ユーザー）と、QEMU の通しの自動試験（S1〜S12 の各場面を撮る script）。既知の項目を場面に割り当て、足りないものを Phase にする | planning | — |
