<!-- awesome-plan project=zedbsd record=queue -->

# Queue

今の Queue と直近の Queue（全担当）。承認された Queue の範囲だけが実行の境界（AGENTS.md §2）。
担当ごとの詳細は `plan/agents/P<n>/queue.md`、担当の台帳は [registry](agents/registry.md)、次の番号は [master.md](master.md) の「次の番号」。

- 1 行は 1 attempt: `| qNNN / qNNN-iNN | 担当（定義・worktree・branch） | Phase | 範囲と承認（ユーザーの原文） | 結果 |`。
- 結果の語は `pending`・`in-progress`・`cleared`・`uncleared`（取り下げは理由を）。終わった節は Q1 がまとめて
  [history](history/index.md) へ移す。q1593 までの Board は [history/queue-board-through-q1593.md](history/queue-board-through-q1593.md)
  （2026-10-27 に P3 の精査 q1608 が古い未決の行を閉じた後に移した）。

## 2026-10-26 22:57: WS290（q1594〜）

承認: 2026-10-26 22:57 ユーザー「…新しいwsを作り、現状のghci.mdを読んで、zedBSDのGHCI driverを書き直してください。…」と、同日の決定（GHCI 2.0 だけ、host 試験だけ）。

| Queue | 担当 | Phase | 範囲 | 結果 |
| --- | --- | --- | --- | --- |
| q1594 / q1594-i01 | P1（kernel-and-driver-designer、worktree `/home/awe/zedBSD-worktrees/p1-ws290`・branch `agent/p1-ws290`、基点 main） | [ws290-p001](ws290/phase001/phase.md) | 設計（ghci.md 2.0 と今の driver の照合、構成、host 試験の model、Phase の分割、ghci.md の疑問の一覧）、design-reviewer。code は書かない | 担当の作業は終了（design.md 版 2、review 28 件反映、9050ae0fd、merge 593c6e5be、P1 は返却）。p001 の clearance は G01〜G39・U1〜U7 のユーザーへの提示の後 |
| q1595 / q1595-i01 | P2（phase-runner、worktree `/home/awe/zedBSD-worktrees/p2-ws291`・branch `agent/p2-ws291`、基点 main） | [ws291-p001](ws291/phase001/phase.md) | desktop の計測と 60 fps、実機 10.0.10.3（BootNext で USB の zedBSD）。結果: cleared（全段 60 fps、d29a59f6b、merge bad29b52f）。承認: 2026-10-26 23:05 ユーザー「P2も立てます。…amdrdnaドライバの改善を行ってほしいです。…」 | cleared（d29a59f6b、merge bad29b52f。範囲の欄の結果のとおり） |
| q1596 / q1596-i01 | P2（同じ worktree p2-ws291） | ws291-p002 | Linux で約 50 fps の 3D の scene を決め、zedBSD との差を測る（予約、p001 の後） | cleared（2026-10-27 00:43: scene-6500 zedBSD 20.1 fps・Linux 50.1 fps、merge 4cdbf1b7a） |

## 2026-10-26 23:17: i915 の残り（q1597〜）

承認: 2026-10-26 23:17 ユーザー「P4を立てて、WS244, WS245, WS252の実装を終わらせます。今から5330を起動するので、それにアクセスしてテストしてほしいです。」

| Queue | 担当 | Phase | 範囲 | 結果 |
| --- | --- | --- | --- | --- |
| q1597 / q1597-i01 | P4（phase-runner、worktree `/home/awe/zedBSD-worktrees/p4-i915`・branch `agent/p4-i915`、基点 main） | [WS245](ws245/ws.md) の残り（p041〜p045・p050d4c ほか） | main の tip で 5330 の実機の回帰、FAIL は二分探索で原因の段を特定して直す、残りの実装、p044・p045 の受け入れの読み替えはユーザーに示す | cleared（2026-10-26 23:40: 5330 の回帰 PASS（main 684a5243f）、5dd23a62e を merge 1c7c926d3。p041〜p045・p050d4c cleared） |
| q1598 / q1598-i01 | P4 | [WS244](ws244/ws.md) の残り | Gen9 の display・GT の出典の直し（ws.md の再開点）、5330 で回帰 | cleared（5330 の分 PASS、7dc936a23、merge 1c7c926d3。SV7 は試さない、2026-10-26 23:45 ユーザー） |
| q1599 / q1599-i01 | P4 | [WS252](ws252/ws.md) p002〜p008 | RKL・ADL-S/RPL-S・ICL の GT。実機が無い物は host 試験まで（D5「無いまま」）、5330 で退行が無いこと | finished（2026-10-27 P3 の精査 q1608: WS252 の p002〜p006 は host 試験・5330 の回帰まで実装済み（ws252/ws.md）、WS252 は completed）。旧: pending |

| q1600 / q1600-i01 | P2（同じ worktree p2-ws291） | ws291-p002a | amdrdna の executor に depth・index draw・mip（mview が動く 3D）。承認: ユーザー「機能を足してから比較（推し）」 | cleared（2026-10-27 00:43: 1 体で Linux と pixel 一致、b20e5545e、merge 5b654104f） |
| q1601 / q1601-i01 | P4 | WS252（firmware） | driver が名指す DMC の全部（rkl・adls・icl を足す）を zedBSD-firmware の i915/（SHA256SUMS・LICENSE）と userland/firmware/i915（Makefile の照合・manifest・WS233 の firmware-inventory）に。zedBSD-firmware は local の commit まで、push はユーザーの指示の時。承認: 2026-10-26 23:58 ユーザー「zedBSD-firmware/ツリーに、i915の依存としてすべて入れられませんか？」 | 済み（2026-10-27 01:01: 5ebc72e07、merge 7f9e423b6。zedBSD-firmware は local の fcdfccf、push はユーザーの指示待ち） |
| q1602 / q1602-i01 | P4 | [ws293-p001](ws293/ws.md) | i915 の render/ を vk/ に改名（動作は不変）、build・host 試験・5330 の回帰。承認: 2026-10-27 00:06 ユーザー「vk/に揃えたいです。」 | cleared（2026-10-27 01:39: c766c36ee、merge 3eef78c3e、5330 PASS） |
| q1603 / q1603-i01 | P2（phase-runner、worktree p2-ws291） | ws291-p003 | 投入の経路の改善（非同期の submit、record・submit の費用）、measure.sh bench で Linux と比べる。結果: cleared（2026-10-27 04:31、big-100 6.06→62.0 fps、subs-100 4.35→32.5、scene-6500 20.1→21.9）。承認: 2026-10-27 00:44 ユーザー「投入の経路から（推し）」 | cleared（2026-10-27 04:31、範囲の欄の結果のとおり） |
| q1604 / q1604-i01 | P4 | WS233 の試験の追従 | plan/ws031/tests/run-i915-firmware-package-test.sh を DMC 6 file・新しい cache の名前に直し、host の rm -rf を fresh_out に替え、完了した WS031 から plan/ws233/tests/ へ移す（WS233 p001 が参照、試験の整理の基準で残す物）。q1602 の後 | cleared（2026-10-27 01:44: 2f56e7a66、PASS） |
| q1605 / q1605-i01 | P4 | ws031 の試験（WS075 が使う） | plan/ws031/tests の lower・compile の期待値を今の mview の shader に合わせる（試験の側だけ）。q1604 の後 | cleared（2026-10-27 01:51: 2c8ae752c、run-vk-host-tests.sh 全部 PASS） |
| q1606 / q1606-i01 | P4 | WS245・WS244 の完了の後片付け | ws.md を完了の形に（a1d8b7613、merge 0121eb6dc）、Phase の dir 等は Q1 が削除（2a6fa1306）。回帰に使う道具を plan/tools/i915-display/・plan/tools/hw5330/ へ移す（2026-10-27 01:54） | cleared（2026-10-27 02:01: 2d4d09c63・456e6ff46、merge 7ba7f784a、run-xd-host-tests all passed、5330-run PASS） |
| q1607 / q1607-i01 | P1（bug-fixer、worktree `/home/awe/zedBSD-worktrees/p1-bug346`・branch `agent/p1-bug346`） | [BUG-346](bugs/BUG-346.md) | VF2（10.0.30.2）で sshd の子の回収漏れを再現・原因・修正・確認。承認: 2026-10-27 04:10 ユーザー「VF2を使ってSSHの問題を修正してください。P1が空いていますか？AMD RDNAマシンでは、SSHの問題を回避しつつ、元の作業を継続してください。」 | in-progress |
| q1608 / q1608-i01 | P3（phase-runner、worktree `/home/awe/zedBSD-worktrees/p3-wsaudit`・branch `agent/p3-wsaudit`） | WS の精査 | Master の registry の未完の WS（incomplete・planning・planned）を ws.md・decisions-log・他の WS と照らし、completed・canceled・保留（blocked）に直す。証拠と出典を一覧に。承認: 2026-10-27 04:24 ユーザー「すみません、残っているとされたWSの大半が、すでに別なWSで完了していたり、私がブロックしたりキャンセルしたりしたものでした。サブエージェントP3を立てて、残っているWSを精査してください。masterとWSを修正して、completedやcanceledにしてください。」 | cleared（P3 の 655604714 を main に merge。結果は agents/P3/ws-audit.md、ユーザーへの問い 9 件は Q1） |
| q1609 / q1609-i01 | P2（phase-runner、worktree p2-ws291） | ws291-p004 | GPU 側の最初の安い試し: PA の small primitive filter・back-face cull・VGT の vertex reuse を RADV の値に（Mesa は値の参照、block ごとに出典）、scene-6500 の GPU の ms を測る。足りなければ NGG の設計の案を Q1 に。承認: 2026-10-26 ユーザーの WS291 の指示（Linux よりよくなるまでプロファイルベースで改善） | cleared（2026-10-27 04:43: 設定は RADV と同じ、差は NGG の culling、bfb05bb24） |
| q1610 / q1610-i01 | P4（phase-runner、worktree `/home/awe/zedBSD-worktrees/p4-plan`・branch `agent/p4-plan`） | plan の形式の最適化 | master.md を中心に plan/ 一式を Opus 5.5 が読みやすい形へ。P3 の ws-audit.md の結果を取り込む。承認: 2026-10-27 04:34 ユーザー「master.mdの内容が整理されていないことが、あなたの混乱につながっているように感じます。そこで、Opus 5.5であるあなたにとって理解しやすい形式に、master.mdなどのplan/以下一式を最適化してほしいです。これはP4に今すぐやってほしいです。」 | in-progress |
| q1611 / q1611-i01 | P2（phase-runner、worktree p2-ws291） | ws291-p005 → p006 → p007 | NGG の A（passthrough）→ B（culling）→ C（詰め直し・属性の後回し）。承認: 2026-10-27 04:44 ユーザー「A〜C を今進める（推し）」 | in-progress |
