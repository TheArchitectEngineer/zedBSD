<!-- awesome-plan project=zedbsd record=ws099-p037 -->
# ws099-p037: 設計 — App Home の Power Off と、暗くする確認の dialog

Parent: [WS099](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる）
Disposition: normal
Related: [BUG-235](../../bugs/BUG-235.md)・[ws132-p008](../../ws132/phase008/phase.md)（電源 button の dialog）

## 由来（ユーザー、2026-10-06）

- BUG-235「ログアウトアイコンで、確認なくセッションが終了してしまいます。デスクトップを暗くする演出で、終了するか選ばせてください。」
- 「Logoutの確認は、電源オフのボタンもあるといいですね。また、タブレットで使うのが目的なので、Logoffというアイコンより、Power Offみたいなアイコンとテキストがいいかもしれないです。」

## 設計

- App Home の「Log Out」の tile を **「Power Off」**（電源の記号の tile、montage-4 の形、灰色の帯）に替える。Lock Screen の tile は残す。
- 押すと（と、電源 button の短押しと同じ dialog）: desktop 全体を 200 ms で暗くし（黒 0.55）、中央に card:
  - **Power Off**（主、赤みの強調）・**Restart**・**Log Out**・**Cancel**
  - Esc・外の click・2 本指の下 swipe で Cancel。
- 電源 button の dialog（ws132-p008、compositor の電源の dialog）と同じ code の dialog にまとめる（中身の 4 つの button と暗くする演出を共通に）。
- 動作: Power Off・Restart は sessiond を経る今の経路（ws131 D12 の改訂、session から poweroff・reboot を頼む）。Log Out は今の log out の経路。
- 文は WS158 の翻訳の catalog に（「電源を切る」「再起動」「ログアウト」「キャンセル」）。
- log: `ZWL POWER dialog open source=home|button`、`choice=poweroff|restart|logout|cancel`。

## 試験と Phase

- AAT のシナリオ `desktop.home.power-off-dialog`（Cancel で戻る、Log Out で greeter）。Power Off・Restart は QEMU で guest が止まる・再起動するのを確かめる。
- 実装は 1 Phase（0.5 LW、ベータ1 の残りとして）。
