<!-- awesome-plan project=zedbsd record=ws290 -->

# WS290 zedBSD の GHCI の driver を GHCI 2.0 に書き直す

- Master: [plan/master.md](../master.md)（2026-10-26 作成）。Primary Milestone: MG006。Related: MG008。前身: [WS288](../ws288/ws.md)（closed、zedBSD の driver は ws288-p006・p010・p018・p019・p035・p040・p043・p044 などで作った）。
- Status: incomplete（2026-10-27 中断: GHCI の仕様（ghci.md）の調整中。p001 の design 版 3 は書きかけ。再開の条件は下の「現在の状態・再開点」。前: 2026-10-26 新設、p001 を実行、G01〜G39 の回答待ち）
- 出典: 2026-10-26 ユーザー「amaterrace-gpuは他のエージェントにまかせることにしました。あなたにはzedBSD側に集中してもらいます。よって、ws288をクローズしてください。新しいwsを作り、現状のghci.mdを読んで、zedBSDのGHCI driverを書き直してください。amaterrace-gpuのディレクトリは読み取り専用とします。amaterrace-gpuのplan/は読んでもかまいません。am-ws001のような書き方をしてくれれば私は理解できます。」
- 決定（2026-10-26、Q1 の問いへのユーザーの回答）:
  - 対象の版は **GHCI 2.0 だけ**（ghci.md の TO-BE: 19 の register、in-memory の PSO・descriptor、`CMD_*` の構造化 command、CP の自律 MMU）。1.1（§3.1.1 の 128 KiB の register、class 2 の gpu-op の中継、STATE、KICK、APERTURE）は捨てる。2.0 のハードウェアは am-ws001-p007 で作られる予定。
  - 試験は **zedBSD の host 試験だけ**（register・ring・memory の model）。amaterrace-gpu の Verilator の model・firmware の image は使わない。

- 判断 U1〜U7 への回答（2026-10-26 23:50 ユーザー）「* U1: 消してよいです。* U2: 写してよいです。* U3: G28は決まったのでは。* U4: OKです。* U5: PROT は実装しませんが、WSを立てて、あとで実装します。* U6: 消してOKです。* U7: UMDの記述はGHCI側でUMD/KMDに変更しました。」 → U1 `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を消す、U2 amaterrace の `tools/gsi1*.py` を `plan/ws290/tests/gsi1-tools/` に写す、U4 riscv64 は build だけ、U5 PROT は WS290 では実装せず [WS292](../ws292/ws.md) で後で、U6 ghci-blit と ghcictl の diag・paint・fwload を消す、U7 ghci.md は amaterrace-gpu 3af62ca で §6.0 を「UMD or KMD」に直した（driver は KMD が ring を書く形で整合）。U3（G28 の表示）は Q1 がユーザーに確認中。
- ghci.md の版: 4d12da1 の後に 3af62ca（2026-10-26 23:49、§6.0 の UMD/KMD だけの差）。
- 正本（2026-10-27 18:08 ユーザー「仕様がアップデートされたので、ghci.mdが正本となります。」）: 仕様は amaterrace-gpu の docs/ghci.md だけに従う。一次〜K の回答（ghci-answers-hw-*.md・ghci-questions-round*.md）は経緯であって正本ではない。再開の時は、その時の ghci.md の SHA を読んで記録し、L01〜L06 などの残る点も ghci.md の本文で確かめる。
- ghci.md の更新（2026-10-27 13:15 の f718918 まで、q1621 で P3 が読む）についてのユーザーの説明（2026-10-27）「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」 → design を command の通信の形に作り直し、capability の flag ごとに Vulkan の extension・feature などへの対応表を作る（flag の有無の両方を host 試験）。指示: 2026-10-27 13:55「P3で、~/amaterrace-gpu/docs/ghci.md の更新を読んで、GHCIドライバを更新してください。」

- 2026-10-27 ユーザーの補足（Q1 経由の原文）: 「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」
- 2026-10-27 ユーザーの G01 の回答（原文）: 「G01: GHCIはまだ策定中のドラフトで、誤ってAIハルシネーションによって2.0にされてしまいました。1.0が正しく、それ以外のバージョンは存在しません。1.0以外は予定すらされていません。2.0は誤りなので、ドライバでは2.0を考慮してはいけません。すべて単一のGHCI 1.0です。GHCIは1.0のあとはcapabilityが追加されていく予定で、決められた1.1の必須機能とかはない予定です。1.1で追加されたCAPがあって、そのCAPに対応するかはハードウェア実装次第、という形になっていきます。」「そのほかは仕様を調整中です。いったん作業を終了していいです。」 → 解釈（Q1）: driver は単一の GHCI 1.0 だけを扱う。「2.0」「1.1 legacy」の区別・版による分岐を設計から外し、機能の有無は版でなく capability の flag だけで判断する（将来の minor で足される CAP も flag）。この文書の「2.0」は今後「GHCI 1.0」と読み替える（WS の題の扱いは再開の時に Q1 と決める）。
- 2026-10-27 hardware の担当の G02〜G39 の一次回答: Q1 が main の [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)（main ecd4407e8、この branch には無い）に原文で置いた。ユーザー「このあと資料全般を修正するので、それまで待ってください。回答は修正されるかもしれません。」なので design には未反映。
- U3（G28）: ユーザー「G28は決まったのでは」= ghci.md §13.2 に従う（design 版 3 §13、残りの欠けは G49）。

## 単一の到達目標

zedBSD の `src/drivers/gpu/ghci/` を、amaterrace-gpu の `docs/ghci.md`（GHCI 2.0）だけに従う driver に書き直す。attach（識別・DEVICE_CAPS・EXTCAP）、ring と doorbell、fence と割り込み、fault・watchdog・reset、GPU の仮想 memory（ring の VM の操作、2 段の page table）、PSO と descriptor、`CMD_*` の command、表示（DISP2）、Vulkan の経路（`vk/`）と GSI1 の compiler の接続までを、host 試験で確かめる。

## 制約

- amaterrace-gpu（`~/amaterrace-gpu`、`~/amaterrace-worktrees`）は読み取り専用。読んでよい物: `docs/ghci.md`、`include/`、`plan/`、その他の source。ghci.md の誤り・食い違い・未定義は driver の側で推測で埋めず、一覧にしてユーザーに示す（amaterrace-gpu の側の担当へはユーザーが伝える）。amaterrace-gpu の計画は am-ws001-p007 のように書く。
- ghci.md は読んだ commit の SHA を記録する（2026-10-26 22:48 の時点で main 4d12da1）。
- HAL の API（`include/hal/hal.h`）は変えない（変えるなら差分ごとの事前承認）。
- 規約は [coding-style.md](../coding-style.md) の全文。試験の方針は AGENTS.md（細かい修正ごとの回帰はしない、build の warning 0 と変えた所の host 試験）。
- 実機・QEMU の試験は無い（2.0 のハードウェアが無い）。実機の証拠は「未実施」と書く。

## 受け入れ

- driver に GHCI 1.1 の register・command・STATE の code が残っていない。
- amd64 の vmunix が CONFIG_DRIVER_PCI_GHCI で build でき warning 0。WS288 で入れた他の platform（VF2 の riscv64 など）の扱いは p001 の設計で決める。
- host 試験が、p001 の設計で決めた範囲（上の到達目標の各項目）を覆い PASS。
- 規約の全文の見直しの Phase が cleared。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: ghci.md（2.0）と今の driver（33k 行）の照合、残す物（GSI1 の compiler など）・捨てる物・書き直す物、新しい構成、host 試験の model、Phase の分割、ghci.md の疑問の一覧。design-reviewer を通す。code は書かない | — | in-progress（2026-10-27 中断: 仕様の調整中。design 版 3 は書きかけ、[design-review-v3.md](design-review-v3.md) の V01〜V25 は未反映。再開の条件は下） |
| p002〜 | 実装（p001 の設計で立てる） | p001 | — |
| 最後 | 規約の全文の見直し | 実装の全部 | — |

## 現在の状態・再開点

- 2026-10-26: 新設。次は p001（設計）。
- 2026-10-26 P1: p001 の [design.md](design.md) 版 2（Phase p002〜p013、196 LW、ghci.md の疑問 G01〜G39、判断 U1〜U7）。次: Q1 が G・U をユーザーに示し、design §18 から p002 以降を立てる。p002・p003 は回答なしで始められ、p004 は G04 の回答が要る。
- 2026-10-27 P3（q1621-i01、worktree `/home/awe/zedBSD-worktrees/p3-ws290`、branch agent/p3-ws290）: ghci.md ce9725a（docs/・include/ の最後の差 f718918）を読み design 版 3 を書きかけ（§0.1 改訂の要旨、§24 capability と Vulkan への対応、G の状態と G40〜G49、U8、Phase の表 212 LW）。design-reviewer 25 件（[design-review-v3.md](design-review-v3.md)）は V20 以外未反映。ユーザーの G01 の回答と「いったん作業を終了していいです」で中断。
- **再開の条件**: ユーザーが ghci.md（と資料全般）の修正の終わりを伝えた時。**再開の手順**: (1) その時の amaterrace-gpu main の `docs/ghci.md`・`include/` を `git show` で読み、SHA を記録（ce9725a からの差を読む）。(2) main の [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)（修正されていればその版）を読む。(3) design 版 3 を仕上げる: G01 の回答（単一の GHCI 1.0、版で分岐しない、機能は capability の flag だけ）を全体に、hardware の回答と修正後の ghci.md で G02〜G49 の状態を直し、design-review-v3 の各指摘が残るかを確かめて反映。(4) 改訂部分を design-reviewer に通して反映。(5) design §18 から p002 以降の phase.md を立て、この表を直す。(6) p002 から順に実装（host 試験だけ）。
