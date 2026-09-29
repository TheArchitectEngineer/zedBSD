<!-- awesome-plan project=zedbsd record=ws102 -->

# WS102: スクリーンキーボード（compositor に直接、flick と QWERTY と手書き）

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（設計）から
<!-- awesome-plan-current:end -->

## 目標（2026-09-30 ユーザー）

「スクリーンキーボードを実装したいです。画面右下からスワイプで、画面右側にフリック入力パネル。画面左下からスワイプで、画面下側にQWERTY
キーボード、手書き入力。フリック入力は、アルファベット、記号、日本語。スクリーンキーボードはWaylandコンポジタに直接実装するのがいいと思います。
システムの一体感を重視します。手書き入力の認識処理はあとで実装して、スタブでいいです。」

## 達成基準（案、2026-09-30 main）

| # | 基準 | 確かめ方 |
| --- | --- | --- |
| K1 | 画面の右下の角から内へ swipe すると、画面の右側に flick の入力の panel が出る。もう一度の swipe（外へ）か閉じる key で消える | QEMU の注入の touch、画面 |
| K2 | flick の panel で、アルファベット（大文字・小文字）、数字と記号、日本語（かな）を flick で打てる。種類の切り替えの key がある | 注入の touch で打った文字が app（Text Editor・Terminal）に届く |
| K3 | 画面の左下の角から内へ swipe すると、画面の下側に QWERTY の keyboard が出る。shift・記号・数字・backspace・enter・矢印がある | 同上 |
| K4 | QWERTY の panel から手書きの入力の面に切り替えられる。書いた線を描き、認識は stub（「認識はまだ」と出すか、決まった候補を返す）でよい | 画面 |
| K5 | keyboard が出ている間、focus の app の窓が隠れないように、作業の領域を縮めるか、窓を上へ寄せる | 画面（Text Editor の caret が見える） |
| K6 | 見た目は Kei のすりガラスと部品（system bar・menu）に揃い、compositor の一部として描く（別の process の app ではない） | 画面、ユーザーの目視 |
| K7 | 既存の角と端の gesture（左上の App Home、右上の Notes、下端の上への swipe）とぶつからない | 回帰の試験（WS099 の C9） |
| K8 | 文字の送り先は focus の app。日本語は IME（WS095）があれば IME を通して変換でき、無ければかなをそのまま送る | 注入の touch |

## 設計で決めること（p001）

- 文字の届け方: compositor の内部から focus の client へ key を合成する（`zwl_seat_key_deliver` の経路）か、text-input-v3 の commit で送るか。
  日本語の flick は、かなを IME（WS095 の engine）に渡して変換するか、かなをそのまま送るか。**WS095 は今、人間が作業中**（master）なので、
  IME の file（`ime.h`・`text-input.c`・`input-method.c`）と seat.c の IME の hook を変えずに済む形を先に考える。
- 角の gesture: `corner.c`（右上の Notes）・`home.c`（左上、下端の swipe）の既存の認識器と同じ型で、右下・左下の角を足す。下端の上への swipe
  （Wiseview・Home を閉じる）との区別。
- 描画: compositor の glass の panel と文字（`glass.c`・`panel.frag`）で描く。panel の大きさ（1280x800 と 1920x1080 の画面、5330 の内蔵 LCD は 1920x1080）。
- flick の配列: 日本語は 12 key のかな（あ段の key を中心に上下左右で い・う・え・お）、濁点・小書きの切り替え。英字は 12 key の abc 配列か。記号。
- 手書き: 線の記録と描画、認識の interface（後で認識の engine を差す）。
- K5 の作業の領域の縮め方（WS099 の「作業の領域」、BUG-114 の配置の規則との関係）。
- 試験: 注入の touch（`build/main-pen` の image と touchinject）で角の swipe と flick を打つ。
- Phase の分け方。

## Phase（案）

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| ws102-p001 | 設計（上の決めること） | planning | — |
