<!-- awesome-plan project=zedbsd record=ws290 -->

# WS290 zedBSD の GHCI の driver を GHCI 2.0 に書き直す

- Master: [plan/master.md](../master.md)（2026-10-26 作成）。Primary Milestone: MG006。Related: MG008。前身: [WS288](../ws288/ws.md)（closed、zedBSD の driver は ws288-p006・p010・p018・p019・p035・p040・p043・p044 などで作った）。
- Status: incomplete（2026-10-26 新設。p001 の設計を実行、ghci.md の疑問 G01〜G39 の回答待ち。前: planning）
- 出典: 2026-10-26 ユーザー「amaterrace-gpuは他のエージェントにまかせることにしました。あなたにはzedBSD側に集中してもらいます。よって、ws288をクローズしてください。新しいwsを作り、現状のghci.mdを読んで、zedBSDのGHCI driverを書き直してください。amaterrace-gpuのディレクトリは読み取り専用とします。amaterrace-gpuのplan/は読んでもかまいません。am-ws001のような書き方をしてくれれば私は理解できます。」
- 決定（2026-10-26、Q1 の問いへのユーザーの回答）:
  - 対象の版は **GHCI 2.0 だけ**（ghci.md の TO-BE: 19 の register、in-memory の PSO・descriptor、`CMD_*` の構造化 command、CP の自律 MMU）。1.1（§3.1.1 の 128 KiB の register、class 2 の gpu-op の中継、STATE、KICK、APERTURE）は捨てる。2.0 のハードウェアは am-ws001-p007 で作られる予定。
  - 試験は **zedBSD の host 試験だけ**（register・ring・memory の model）。amaterrace-gpu の Verilator の model・firmware の image は使わない。

- 判断 U1〜U7 への回答（2026-10-26 23:50 ユーザー）「* U1: 消してよいです。* U2: 写してよいです。* U3: G28は決まったのでは。* U4: OKです。* U5: PROT は実装しませんが、WSを立てて、あとで実装します。* U6: 消してOKです。* U7: UMDの記述はGHCI側でUMD/KMDに変更しました。」 → U1 `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を消す、U2 amaterrace の `tools/gsi1*.py` を `plan/ws290/tests/gsi1-tools/` に写す、U4 riscv64 は build だけ、U5 PROT は WS290 では実装せず [WS292](../ws292/ws.md) で後で、U6 ghci-blit と ghcictl の diag・paint・fwload を消す、U7 ghci.md は amaterrace-gpu 3af62ca で §6.0 を「UMD or KMD」に直した（driver は KMD が ring を書く形で整合）。U3（G28 の表示）は Q1 がユーザーに確認中。
- ghci.md の版: 4d12da1 の後に 3af62ca（2026-10-26 23:49、§6.0 の UMD/KMD だけの差）。

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
| [p001](phase001/phase.md) | 設計: ghci.md（2.0）と今の driver（33k 行）の照合、残す物（GSI1 の compiler など）・捨てる物・書き直す物、新しい構成、host 試験の model、Phase の分割、ghci.md の疑問の一覧。design-reviewer を通す。code は書かない | — | in-progress（2026-10-26 [design.md](design.md) 版 2、review 28 件を反映。疑問 G01〜G39 のユーザーへの提示待ち） |
| p002〜 | 実装（p001 の設計で立てる） | p001 | — |
| 最後 | 規約の全文の見直し | 実装の全部 | — |

## 現在の状態・再開点

- 2026-10-26: 新設。次は p001（設計）。
- 2026-10-26 P1: p001 の [design.md](design.md) 版 2（Phase p002〜p013、196 LW、ghci.md の疑問 G01〜G39、判断 U1〜U7）。次: Q1 が G・U をユーザーに示し、design §18 から p002 以降を立てる。p002・p003 は回答なしで始められ、p004 は G04 の回答が要る。
