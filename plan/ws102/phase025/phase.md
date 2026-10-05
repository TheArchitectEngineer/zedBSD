<!-- awesome-plan project=zedbsd record=ws102-p025 -->
# ws102-p025: 設計 — 画面 keyboard の残り（App Home で消える・引き出しの形・full keyboard の IME）

Parent: [WS102](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる）
Disposition: normal
Related: [BUG-229](../../bugs/BUG-229.md)・[BUG-230](../../bugs/BUG-230.md)・[BUG-231](../../bugs/BUG-231.md)

## 由来（ユーザー、2026-10-06 UAT）

BUG-229「この表示状態で、アプリ一覧を出して戻ると、オンスクリーンキーボードが消えてしまいました。」BUG-230「引き出されたエリアがスクエアで真っ白です。これを、Notesの引き出しと同じ、扇型＋テキストに変更したいです。」BUG-231「オンスクリーンフルキーボードで、IMEの有効状態を反映して、aと入力したら『あ』になって、漢字変換もできるようにしてください。」

## 設計

- **BUG-229**: OSK の表示の状態（出ている・mode）を focus の text-input に結び付けて保つ。App Home・WiseView の間は OSK を隠すだけにし（状態は保つ）、閉じて同じ text-input に focus が戻ったら出し直す。log `ZWL OSK restore`。
- **BUG-230**: 引き出しの描画を Notes の引き出し（扇形と文字）と共通の描き方に。今の白い四角の代わりに、引き出す距離に合わせて扇形の glass が開き、中央に「Keyboard」（翻訳の catalog）と keyboard の記号。
- **BUG-231**: full keyboard（英字の配列）の key を、IME が有効（ime_status が日本語の mode）の時は**文字の commit でなく key の event として IME に通す**（今は文字の commit で IME を迂回している見込み）。IME が preedit（あ）と変換（Space で候補）を行う。候補は OSK の候補の列（WS166 の予測の列と同じ場所）に出す。IME が無効の時は今どおり文字を commit。
- 共通: OSK の状態の変化は 1 つの関数に集め log。

## 試験と Phase

- AAT: `desktop.osk.restore-after-home`、`desktop.osk.full-ime`（a → あ、Space で変換、確定）。実機はユーザー。
- 実装: 1 Phase（p026、1 LW）。
