<!-- awesome-plan project=zedbsd record=ws187 -->

# WS187: ロック画面（大きな時計、スワイプ・ホイールでの解除、認証の方式の選択）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」。旧: incomplete）
Primary Milestone: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-08）「ロック画面は縦長のディスプレイでもきれいに見えるよう、時計を中央より上に大きく表示してほしいです。」と「画面の下部（下端でなくてよい）から上にスワイプするとロック解除でき、手動ロックされた場合を除き、ロックから一定時間ならスワイプのみで認証不要、認証する場合はスワイプのあとパスワード、PIN、ハードウェアパスキーが選択できる入力画面。タッチパッドもタッチスクリーンもない場合のために、マウスホイールを上方向に回転でロック解除も実装。」

## 結果（2026-10-13 完了）

- p001: lock の画面に大きな時計を中央より上に（縦長・横長、寸法は論理 px）。
- p002: 下部から上へのスワイプ（touchscreen・touchpad）と mouse の wheel の上で解除の card を出す（`lock-swipe.c` の状態機械）。猶予は自動の lock の後 5 分（2026-10-08 ユーザーの決定）、手動の lock（利用者の Sleep を含む）は常に認証。
- p003: 認証の方式の選択（Password・PIN・Hardware Key）。登録の無い方式は出さず、Hardware Key は sessiond が返した時だけ。
- p004: 規約の全文の見直し（2026-10-09、host 試験 lock-clock 74・lock-swipe 45 ok（ASan・UBSan）、zedBSD・Linux の build warning 0、style-check 0）。
- 確認: QEMU の T1-379 PASS（縦長・横長の PNG）。実機の UAT（時計・スワイプ・wheel・PIN・Hardware Key）は 2026-10-10 にユーザーが確認（「これで一通りUATの確認事項は確認したと思います。CloseできるものはCloseしましょう。」）。
- 回帰: `tests/scenarios/desktop/lock/`（swipe-card・wheel-card、`plan/tools/aat/scenarios/helpers_desktop.py`）。

## 制限・移管

- ロック画面の方式の button の高さは BUG-283（直しは main、T1-520 で height=40）。
- p004 で直さなかった他 WS の既存の行（sleep.c の printf の引数、greeter.c の kl_tr の引数の入れ子）は、それぞれの WS の見直しで扱う。猶予の時間を Settings で変える形は WS148。FreeBSD の build は未実施。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws187-p001 | lock の画面の時計の配置と描画（縦長・横長）、host 試験、T1 の PNG | cleared（2026-10-08、T1-379） |
| ws187-p002 | 解除の操作（下部から上へのスワイプ・wheel の上）と猶予（自動の lock の後 5 分は認証なし、手動の lock は常に認証） | cleared（2026-10-08、T1-379） |
| ws187-p003 | 認証の入力の画面: Password・PIN・Hardware Key の選択 | cleared（2026-10-08、T1-379） |
| ws187-p004 | 規約の全文の見直し | cleared（2026-10-09） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
