<!-- awesome-plan project=zedbsd record=ws035p102 -->

# ws035-p102: 画面の lock（g5）

Phase ID: `ws035-p102`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「g5 lock screen (session lock, e.g. Super+L / idle, unlocking through zsessiond auth)」。
[login-manager-design.md](../login-manager-design.md) §7-7・§9 の g5）

## 実装（2026-09-28）

- **zdesktop**（session、zsessiond が起こしたとき＝`--control-fd` があるときだけ）:
  - `zwl_lock`（greeter.c）: lock の画面は login の画面と同じ絵（ぼかした wallpaper・時計・card）で、session の user 1 人だけを出す。
    電源の button は出さない。lock の間は desktop とその window を描かない（fullscreen の直接表示もしない）。
  - 入力: key・button・wheel・pointer は全部 lock の画面が取る（seat.c の入口）。client には届かない。
  - lock する方法: **Super+L**、App Home の **Lock Screen**（`@lock`、Log Out の前）、**入力の無い時間**（`--lock-idle=秒`、
    session の既定は 10 分、0 で無し）。
  - 解除: password を zsessiond に `UNLOCK password`（session の socket）で送る。答え（`OK`・`FAIL`）は handoff.c が読み、
    lock の画面に渡す（`zwl_lock_answer`）。password は送ったらすぐ消す。
  - zsessiond の無い zdesktop（試験・手で起こす）は lock しない（解除する手段が無いため。Super+L も client へ）。
- **zsessiond**（session.c）: session の socket の `UNLOCK password` を login と同じ照合（`login_verify`）で、session の user
  だけについて調べ `OK`。違えば 2 秒（3 回続くごとに倍、最大 16 秒）待って `FAIL`。成功・失敗は syslog と zsessiond の log へ
  （password は書かない）。

## 検証（amd64、Venus の guest、graphical の image、2026-09-28）

- `plan/ws035/tests/zdesktop-p102.sh`（新）PASS: root で login、terminal を起こして Super+L で lock（`locked.png`）、違う
  password は 2 秒後に FAIL（`wrong.png`、「Wrong password. Try again.」）、正しい（空の）password で解除（`unlocked.png`:
  lock の間に打った「nope」は terminal に届いていない）、App Home の Lock Screen で lock・解除、`--lock-idle=20` で入力が
  無いと自分で lock（`idle.png`）・解除。解除は 3 回。
  画面: `build/ws035-shots/p102-20260928-{locked,wrong,unlocked,idle}.png`。
- 回帰 PASS: zdesktop-p101（login・Log Out の引き継ぎ）、zdesktop-p100（zsessiond の無い zdesktop）。
- 規約: 変えた file の style-check は増えていない（handoff.c・zsessiond の session.c は 0）。build warning 0。
- 実機: 未実施。

## 制限・残り

- lock は session の zdesktop（user の uid）の中にある。同じ user の process は zdesktop を止められる（desktop の compositor の
  lock と同じ強さ）。zdesktop が落ちると session が終わり greeter に戻る（画面は開かない）。
- 入力の無い時間は zdesktop が受けた入力（key・button・pointer・wheel）で測る。動画などの「起きている」要求（idle inhibit）は無い。
- lock の間の通知・時計以外の表示（誰が lock したか、別 user への切り替え）は無い。
- VT の切り替えが無いので、lock の間も serial・ssh の login は使える（今までどおり）。
