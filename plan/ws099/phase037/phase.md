<!-- awesome-plan project=zedbsd record=ws099-p037 -->
# ws099-p037: 設計 — App Home の Power Off と、暗くする確認の dialog

Parent: [WS099](../ws.md)
Status: test-wait（T1 依頼中、2026-10-06 q783-i01 P2: 実装・build・host 試験まで。以前: planned（Q1 の設計の第 1 版））
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

## 判断（2026-10-06 ユーザー、Q1 経由）

- Power Off・Restart の経路: 「sessiond に口を足す（別の Phase）」→ p037 は session の backend の actions に無い時は薄く・押せない形（今の zedBSD の session は 0、Linux は logind で動く）。sessiond の session の POWER は [ws131-p027](../../ws131/phase027/phase.md)（q793）。
- 電源 button: dialog は `source=home|button` で開ける形にし、今は App Home からだけ（電源 button は ws132-p008 のまま log だけ）。

## 実装（2026-10-06 q783-i01 P2）

| 所 | 内容 |
| --- | --- |
| `userland/desktop/wayland/power-layout.c`・`.h`（新、純粋） | 4 つの choice（Power Off・Restart・Log Out・Cancel）、card と button の配置（中央、幅 360、button 52 の高さ）、`zwl_power_hit`（button・card の中・外）、`zwl_power_focus_step`（押せない物を飛ばして回る、Cancel は常に押せる）、`zwl_power_choice_name`、dialog の状態の struct |
| `power-dialog.c`（新） | 開く（`kl_backend_power_get_state` の actions で Power Off・Restart の可否、keys は Cancel から、log `ZWL POWER dialog open source= poweroff= restart=`）、200 ms で黒 0.55 に暗く・150 ms で戻す、frosted の card と button（Power Off は赤、押せない物は 0.35 の薄さ、hover・押下・keys の choice は明るく）、button（押した button で離すと選ぶ、card の外の press で Cancel）、key（Esc で Cancel、Tab・矢印で動かし Enter・Space で選ぶ、開いている間は全部の key を取る）、motion（取る）、2 本指の下 swipe（pad の scroll、上端からの TOP2）で Cancel、Power Off・Restart は `kl_backend_power_action`、Log Out は App Home の前の Log Out と同じ経路（`ZWL SESSION logout`、handoff）、log `ZWL POWER choice= via= error=`、login・lock の画面では閉じる |
| `home.c` | 「Log Out」の tile を「Power Off」（`@power`、keywords に power off・shut down・restart・log out）に。押すと dialog（Home は閉じる）。前の Log Out の処理は dialog へ |
| `icons.c`・`icons.h` | Log Out の絵（扉と矢印）を電源の記号（上の開いた輪と縦の棒）に、`GLASS_ICON_APP_POWER`・名前 `power` |
| `shell.c` | button・key・motion・pad の scroll・gesture で dialog を先に、描画は最後（全画面の窓の上でも）、tick |
| `zwl.h`・`glass.h`・`Makefile*` | `power_dialog`、宣言、`power-layout.c`・`power-dialog.c` |
| `userland/desktop/locale/ja/wayland.tr` | Power Off（電源を切る）・Cancel（キャンセル）。Restart・Log Out は既存 |
| `tests/scenarios/desktop/home/power-off-dialog.md`（新）・`plan/tools/aat/scenarios/helpers_desktop.py` | AAT: Home で power → Power Off の icon → dialog（session は終わらない）、Esc で cancel、外の click で cancel（Log Out は by-agent） |

## 確認

| 確認 | 結果 |
| --- | --- |
| zedBSD の compositor・Linux の Keiland の build | 成功、warning 0 |
| `sh plan/ws099/tests/host-power-layout.sh`（ASan・UBSan でも） | 21 checks ok |
| tile-dump 72（icons.c、Power Off の絵） | PASS（18 tiles）、`build/ws099-p037/power.png` を目視（電源の記号） |
| `tr.py check` | 118 translated, 0 problems |
| style-check（変えた C の全部） | 指摘 0 |
| AAT の `run-host.sh`・`check-scenarios.py` | PASS（85） |
| keiland-os-boundary | 既存の FAIL だけ |
| QEMU | 未実施。T1 に依頼（AAT の desktop.home.power-off-dialog） |
| 実機 | 未実施 |
