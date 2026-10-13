<!-- awesome-plan project=zedbsd record=ws118 -->

# WS118: Dell Latitude 5320 で Kei を動かす

<!-- awesome-plan-current:start -->
Status: closed（2026-10-13 ユーザー「WS044、WS048、WS118はクローズします。」。旧: incomplete、Target はベータ4 以降）
Primary Milestone: MG003
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的（2026-10-02 ユーザー）

「対象 platformですが、Latitude 5330のほか、Latitude 5320でも動かしたいのですが、5320ではLCDの制御がうまくいっていないので、SSHDを起動してリモート実機でログを取れるようなイメージを作成して、ユーザと一緒に進めましょう。」5320 を対象の platform に加え、内蔵 LCD で graphical login から Keiland のデスクトップを出す。network は USB の RTL8156。

## 結果（2026-10-13 close）

- TGL の表示（p006、P3、2026-10-07）: TGL の DPLL と firmware の表示の引き継ぎ（takeover）。世代に依らない fallback は Future Work の F-081 に移した。
- 5320 の touchpad（p007、P3、2026-10-07）: TGL の LPSS I2C の ID と clock の最小の修正。ユーザーの実機の目視で cleared。
- 遠隔の log 用の image（p001）と SSH から手で i915 を初期化する変種 D（p005）は実装と D の build・host 試験まで（q634-i01、2026-10-03 の host の再起動で中断）。C の build・QEMU の SSH・boot-test は未実施のまま。

## 制限・残り

- 5320 の LCD の原因の分類（p002）・i915 の修正（p003）・ベータ1 の受け入れ T3・T4（p004）は未実施。p001・p005 の残りも canceled。
- agent/p4 の 59a94c6aa（q634-i01 の kernel の既定の経路の変更、boot-test 未実施）は main に未統合のまま。
- i915 の世代に依らない表示の fallback は [F-081](../future-work.md)。設計は p006 の phase.md にあり、F-081 から link されている（消す前に F-081 の link の扱いを Q1 が決める）。
- 5320 の 2026-10-07 の log（`tests/k5320-20261007-*.log`）は WS050 の ws.md から証拠として link されている。
- 5320 の続きは、5320 の新しい WS の候補（勝手に移していない）。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws118-p001 | sshd を起動する遠隔の実機 log 用の image（3 種と変種 D）と手順書、QEMU で SSH まで | canceled（2026-10-13、ユーザーの指示で WS を close。旧: uncleared、q634-i01 中断） |
| ws118-p002 | ユーザーと実機で inventory と i915 の log、LCD の失敗の分類 | canceled（2026-10-13、同上。旧: planning） |
| ws118-p003 | p002 の分類に基づく i915 の修正 | canceled（2026-10-13、同上。旧: planning） |
| ws118-p004 | 5320 のベータ1 の受け入れ T3・T4 | canceled（2026-10-13、同上。旧: planning） |
| ws118-p005 | i915 を起動の後に SSH から手で初期化し 5320 の LCD の初期化を直す（変種 D） | canceled（2026-10-13、同上。旧: uncleared、q634-i01 中断） |
| ws118-p006 | TGL の DPLL と firmware の表示の引き継ぎ、fallback | cleared（q846、2026-10-07。fallback は F-081） |
| ws118-p007 | 5320 の touchpad（TGL の LPSS I2C） | cleared（q849、2026-10-07 ユーザーの目視） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
