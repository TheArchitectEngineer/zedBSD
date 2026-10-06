<!-- awesome-plan project=zedbsd record=ws102-p025 -->
# ws102-p025: 設計 — 画面 keyboard の残り（App Home で消える・引き出しの形・full keyboard の IME）

Parent: [WS102](../ws.md)
Status: test-wait（T1 依頼中。q789-i01、P1、2026-10-06 実装済み）
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

## q789-i01（P1、2026-10-06）: 実装

この Phase の Queue（q789）で、設計の 3 点を実装した（設計にある p026 の実装を兼ねる）。commit 4b956b9d。

| Bug | 変更（`userland/desktop/wayland/keyboard.c`） |
| --- | --- |
| BUG-229 | App Home・Wiseview は panel を閉じずに **put away**（`keyboard_put_away`: 閉じて、panel の種類と focus の窓を覚える、log `ZWL OSK put-away kind=K reason=home|wiseview`）。両方が消え、同じ窓（まだ窓である物）が focus を持っていれば同じ panel を開き直す（`keyboard_restore`、log `ZWL OSK restore kind=K`）。別の窓が focus を持つ、または login・lock の画面なら諦める（`ZWL OSK restore-dropped reason=focus|greeter|lock`）。panel を開くと待ちは消える |
| BUG-230 | 引き出しの hint を、faint な panel（白い四角）から Notes の角（corner.c）と同じ glass の四分円に: 下の角（flick は右下、QWERTY は左下）から contact まで伸びる、影・glass・縁（離せば開く時は青）、半径 96 から対角線の上に `kl_tr("Keyboard")`（日本語の catalog に「キーボード」を追加、`userland/desktop/locale/ja/wayland.tr`） |
| BUG-231 | 画面 keyboard が送る key（`keyboard_send_key`）を `keyboard_key_event` に通す: **QWERTY の panel の key** は物理の keyboard と同じく IME の `zwl_ime_key_early`・`zwl_ime_key_grab` を先に通る（IME が field を受け持ち direct input でない時、a は preedit の「あ」、Space で変換。log `ZWL OSK send via=ime code=N`）。IME が取らない key と flick の panel の key は今どおり focus の app へ。候補は IME の popup（OSK の候補の列への表示は今回は入れていない） |

確認: wayland（zedBSD）の build warning 0、keiland-linux.mk の host build exit 0、`plan/ws102/tests/host-keyboard.sh` PASS（layout の試験、keyboard.c 自体は host の試験が無い）、
`tools/i18n/tr.py check` で ja の wayland.tr 117 件 0 problems。QEMU は T1 に依頼（下）。実機は UAT。

残り: BUG-231 の候補を OSK の候補の列に出す（今は IME の popup）。AAT の `desktop.osk.restore-after-home`・`desktop.osk.full-ime` の追加は未実施。
