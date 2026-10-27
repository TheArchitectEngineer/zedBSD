<!-- awesome-plan project=zedbsd record=ws290 -->

# WS290 zedBSD の GHCI の driver を GHCI 1.0（ghci.md cb937e6）に書き直す

- Master: [plan/master.md](../master.md)（2026-10-26 作成）。Primary Milestone: MG006。Related: MG008。前身: [WS288](../ws288/ws.md)（closed、zedBSD の driver は ws288-p006・p010・p018・p019・p035・p040・p043・p044 などで作った）。
- Status: incomplete（2026-10-28 P3（q1638-i01）: p001 cleared、design 版 4.5（ghci.md cb937e6）の §18 で Phase p002〜p015 を確定。p002 から実装中。仕様担当との第 14 版のやり取りは並行。下の「2026-10-28 Phase の確定と実装」）
- 出典: 2026-10-26 ユーザー「amaterrace-gpuは他のエージェントにまかせることにしました。あなたにはzedBSD側に集中してもらいます。よって、ws288をクローズしてください。新しいwsを作り、現状のghci.mdを読んで、zedBSDのGHCI driverを書き直してください。amaterrace-gpuのディレクトリは読み取り専用とします。amaterrace-gpuのplan/は読んでもかまいません。am-ws001のような書き方をしてくれれば私は理解できます。」
- 決定（2026-10-26、Q1 の問いへのユーザーの回答）:
  - （G01 の回答で置き換え: 仕様は単一の GHCI 1.0。下の行は当時の記録）対象の版は **GHCI 2.0 だけ**（ghci.md の TO-BE: 19 の register、in-memory の PSO・descriptor、`CMD_*` の構造化 command、CP の自律 MMU）。1.1（§3.1.1 の 128 KiB の register、class 2 の gpu-op の中継、STATE、KICK、APERTURE）は捨てる。2.0 のハードウェアは am-ws001-p007 で作られる予定。
  - 試験は **zedBSD の host 試験だけ**（register・ring・memory の model）。amaterrace-gpu の Verilator の model・firmware の image は使わない。（2026-10-28 の決定で、実機ができた後は VF2＋KC705 の実機の試験を足す。下の「2026-10-28 の再開」）

- 判断 U1〜U7 への回答（2026-10-26 23:50 ユーザー）「* U1: 消してよいです。* U2: 写してよいです。* U3: G28は決まったのでは。* U4: OKです。* U5: PROT は実装しませんが、WSを立てて、あとで実装します。* U6: 消してOKです。* U7: UMDの記述はGHCI側でUMD/KMDに変更しました。」 → U1 `GPU_OP_GHCI_*` と `CONFIG_GHCI_NATIVE` を消す、U2 amaterrace の `tools/gsi1*.py` を `plan/ws290/tests/gsi1-tools/` に写す、U4 riscv64 は build だけ、U5 PROT は WS290 では実装せず [WS292](../ws292/ws.md) で後で、U6 ghci-blit と ghcictl の diag・paint・fwload を消す、U7 ghci.md は amaterrace-gpu 3af62ca で §6.0 を「UMD or KMD」に直した（driver は KMD が ring を書く形で整合）。U3（G28 の表示）は Q1 がユーザーに確認中。
- ghci.md の版: 4d12da1 の後に 3af62ca（2026-10-26 23:49、§6.0 の UMD/KMD だけの差）。
- 正本（2026-10-27 18:08 ユーザー「仕様がアップデートされたので、ghci.mdが正本となります。」）: 仕様は amaterrace-gpu の docs/ghci.md だけに従う。一次〜K の回答（ghci-answers-hw-*.md・ghci-questions-round*.md）は経緯であって正本ではない。再開の時は、その時の ghci.md の SHA を読んで記録し、L01〜L06 などの残る点も ghci.md の本文で確かめる。
- 2026-10-27 19:12 GHCI の担当が M01〜M58（[ghci-review-8035b4a.md](ghci-review-8035b4a.md)）に答え、docs/ghci.md を **eb296d1** に改訂（回答の要約はユーザーが伝えた。正本は ghci.md）。要約で触れていない M: M26・M33〜M36・M42・M45・M46・M50〜M53・M56・M57。要約の M39〜M49 の番号は質問の番号とずれている（例: 要約の M39 は mesh、質問の M39 は render pass の記述）。次は eb296d1 の本文で M01〜M58 を照合し、design 版 3 を仕上げる。
- DONE の取り決め（2026-10-27 ユーザー「GHCI側は ~/amaterrace-gpu/docs/ghci-review-reply.md に返信を書いています。末尾にDONEが書かれたら完了です。これからはDONEのプロトコルを使います。」）: こちらの疑問の file（plan/ws290/ghci-review-<SHA>.md）は書き終えたら末尾に `DONE` の行を書く。GHCI の側の返信は ~/amaterrace-gpu/docs/ghci-review-reply.md で、末尾に `DONE` がある時だけ完了として読む（読み取り専用）。
- やり取りの file（2026-10-27 ユーザー「返信のメッセージは plan/ws290/ghci-review.md に上書きしていってください。DONEで通知するプロトコルと、ファイル先頭を前のバージョンとは違うものにすることで変更を示す、というルールで運用します。以後、納得できる回答が得られるまで、 plan/ws290/ghci-review.md にレビューを置きます。相手側は、~/amaterrace-gpu/docs/ghci-review-reply.md にレビュー回答を置きます。」）: こちらのレビューは毎回 [ghci-review.md](ghci-review.md) に上書きし、1 行目を前の版と違う物（「# GHCI レビュー 第 n 版（対象の ghci.md の SHA、日時）」）にし、末尾に `DONE`。相手の回答は ~/amaterrace-gpu/docs/ghci-review-reply.md で、末尾が `DONE` かつ 1 行目が前に読んだ版と違う時に新しい回答として読む。第 1 版は 2026-10-27 19:43（eb296d1 の N01〜N29、ghci-review-eb296d1.md と同じ中身）。
- 2026-10-27 20:26 ユーザー「疑問の解消のみを行います。」→ 今は GHCI の疑問のやり取りだけを続ける。driver の実装（KMD の土台を含む）は始めない。
- 2026-10-28 02:50 レビュー 第 9 版（[ghci-review.md](ghci-review.md)）: ghci.md **89529de** に対し N01〜N36 が全部解決し、疑問は残っていない。次は design 版 3 の仕上げ（89529de を正本に）と p002 以降。始めるのはユーザーの指示の後。
- 2026-10-28 ユーザー「ありがとうございます。では、GHCIチームとのやりとりを終了します。」→ GHCI の担当とのレビューのやり取り（ghci-review.md・ghci-review-reply.md の DONE の取り決め）は終わり。回答の file の見張りはしない。
- ghci.md の更新（2026-10-27 13:15 の f718918 まで、q1621 で P3 が読む）についてのユーザーの説明（2026-10-27）「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」 → design を command の通信の形に作り直し、capability の flag ごとに Vulkan の extension・feature などへの対応表を作る（flag の有無の両方を host 試験）。指示: 2026-10-27 13:55「P3で、~/amaterrace-gpu/docs/ghci.md の更新を読んで、GHCIドライバを更新してください。」

- 2026-10-27 ユーザーの補足（Q1 経由の原文）: 「コマンドベースの通信に変化して、レジスタ群がばっさりなくなりました。また、capabilityが導入され、capフラグがあるときだけ使える機能がたくさんできました。Vulkanのextなどとして見せるかどうかにマッピングが必要です。」
- 2026-10-27 ユーザーの G01 の回答（原文）: 「G01: GHCIはまだ策定中のドラフトで、誤ってAIハルシネーションによって2.0にされてしまいました。1.0が正しく、それ以外のバージョンは存在しません。1.0以外は予定すらされていません。2.0は誤りなので、ドライバでは2.0を考慮してはいけません。すべて単一のGHCI 1.0です。GHCIは1.0のあとはcapabilityが追加されていく予定で、決められた1.1の必須機能とかはない予定です。1.1で追加されたCAPがあって、そのCAPに対応するかはハードウェア実装次第、という形になっていきます。」「そのほかは仕様を調整中です。いったん作業を終了していいです。」 → 解釈（Q1）: driver は単一の GHCI 1.0 だけを扱う。「2.0」「1.1 legacy」の区別・版による分岐を設計から外し、機能の有無は版でなく capability の flag だけで判断する（将来の minor で足される CAP も flag）。この文書の「2.0」は今後「GHCI 1.0」と読み替える（WS の題の扱いは再開の時に Q1 と決める）。
- 2026-10-27 hardware の担当の G02〜G39 の一次回答: Q1 が main の [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)（main ecd4407e8、この branch には無い）に原文で置いた。ユーザー「このあと資料全般を修正するので、それまで待ってください。回答は修正されるかもしれません。」なので design には未反映。
- U3（G28）: ユーザー「G28は決まったのでは」= ghci.md §13.2 に従う（design 版 3 §13、残りの欠けは G49）。

## 単一の到達目標

zedBSD の `src/drivers/gpu/ghci/` を、amaterrace-gpu の `docs/ghci.md`（89529de、単一の GHCI 1.0）だけに従う driver に書き直す。attach（識別・DEVICE_CAPS・EXTCAP）、ring と doorbell、fence と割り込み、fault・watchdog・reset、GPU の仮想 memory（ring の VM の操作、2 段の page table）、PSO と descriptor、`CMD_*` の command、表示（DISP2）、Vulkan の経路（`vk/`）と GSI1 の compiler の接続までを、host 試験で確かめる。

## 制約

- amaterrace-gpu（`~/amaterrace-gpu`、`~/amaterrace-worktrees`）は読み取り専用。読んでよい物: `docs/ghci.md`、`include/`、`plan/`、その他の source。ghci.md の誤り・食い違い・未定義は driver の側で推測で埋めず、一覧にしてユーザーに示す（amaterrace-gpu の側の担当へはユーザーが伝える）。amaterrace-gpu の計画は am-ws001-p007 のように書く。
- ghci.md は読んだ commit の SHA を記録する（2026-10-26 22:48 の時点で main 4d12da1）。
- HAL の API（`include/hal/hal.h`）は変えない（変えるなら差分ごとの事前承認）。
- 規約は [coding-style.md](../coding-style.md) の全文。試験の方針は AGENTS.md（細かい修正ごとの回帰はしない、build の warning 0 と変えた所の host 試験）。
- 試験はまず host 試験。実機（VF2 に KC705 をつないだ物）はユーザーが用意した後（下の「2026-10-28 の再開」）。QEMU の試験は無い。実機で確かめていない事は「未実施」と書く。

## 受け入れ

- driver に、GHCI 1.0（89529de）より前の形（以前の 1.1 の register・command・STATE、版の分岐）の code が残っていない。
- amd64 の vmunix が CONFIG_DRIVER_PCI_GHCI で build でき warning 0。WS288 で入れた他の platform（VF2 の riscv64 など）の扱いは p001 の設計で決める。
- host 試験が、p001 の設計で決めた範囲（上の到達目標の各項目）を覆い PASS。
- 規約の全文の見直しの Phase が cleared。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計: design 版 4.5（ghci.md cb937e6、U5・U8〜U11 の反映、design-reviewer（Opus 5.5）3 回の反映、§18 の Phase の確定）。code は書かない | — | cleared（2026-10-28 P3 q1638-i01。仕様の残りの疑問 N62〜N72 は ghci-review 第 14 版でやり取り中、driver は design §19 の仮の扱いで進める（ユーザー 2026-10-28「はい、P3は仮の扱いで実装を行ってよいです。」）） |
| [p002](phase002/phase.md) | 語の層: `ghci-regs.h`・`ghci-cmd.h`・`cmd.c`（§4.4 の全部）・`caps.c`（decode と CP_HEAP の勘定）・ring の計画と fence64・EXTCAP の走査（`check.c` に新しい名前で足す）。build に入る今の file は変えない（新しい file は build に入れない）。`ghci-ref-host-test.c`（§14、R23）（受け入れ: words と ref の試験 PASS、新しい file を amd64 と riscv64 の kernel の flag で compile して warning 0（R23）、`CONFIG_DRIVER_PCI_GHCI=y` の amd64 が今のまま build、14 LW） | p001 | planned |
| [p003](phase003/phase.md) | model・stub・script（§14）。WS288 の 1.1 の試験の削除の list を Q1 へ（受け入れ: model の自己試験 PASS、16 LW） | p002 | planned |
| [p004](phase004/phase.md) | core: pci-attach（match・Bus Master の順）、identify・caps・CP_HEAP（flat は 16 KiB）・ring・fence・IRQ・poll・watchdog、fault（FAULTED に依らず reset、犯人の記録、U5）・reset・LOST、detach。1.1 の file（firmware・native・state・blit・frame・packets・hcr）と vk/・display・compiler を build から外し、呼ぶ所を消す。capability は p004 で callback を全部実装した物だけを出す（`gpu.h` の「Optional capabilities require their complete callback pair」。resource・blob・map は p005 で足す。R17）（受け入れ: amd64・riscv64 の build の warning 0、p004 の場面 PASS、build に入る file に 1.1 の名前が無い（grep）、22 LW） | p003 | planned |
| [p005](phase005/phase.md) | session・VM（chunk・勘定・map・unmap・close・再生、flat の恒等の VA）、memory（host、4 GiB の上限）、resource・blob・map・read・write、share・import。local は ENOTSUP。+ `SHARE | ALLOCATION_SHARE`（受け入れ: p005 の場面 PASS、build の warning 0、26 LW） | p004 | planned |
| [p006](phase006/phase.md) | compiler: header・interface 表を捨て §12 の ABI（`gsi1-abi.h`）、資源の表、options、`compiler/gsi1_isa.h` を bea1ef9 の値（`GSI1_COLOR_ATTACHMENTS` 8、`GSI1_EXP_COLOR0_SRC1`、`GSI1_VS_REG_*`、lda の word 0〜3）に直す（R18）、FS の key に primitive の種類（R08）、compiler を build に戻す、gsi1 の試験と tools を移す（受け入れ: gsi1 の試験 PASS、build の warning 0。graphics の I/O は cf8be08 §12.10.3 で、22 LW） | p002・p004 | planned |
| [p007](phase007/phase.md) | executor の土台: vk.h・stream・node・object・reply・vk-caps・device・format（FMT）・memory（heap）・command・queue（空の submit、fence・semaphore・event）。libvulkan が送る op の全部（`userland/desktop/libvulkan/` の `GPU_OP_*`、1.1 の `*2`・`EXTERNAL_*`・`GET_DEVICE_QUEUE2`・`RECORD_QUEUE_TIMELINE` を含む）の表を作り、op ごとに答える・記録する・断るを決める（R21）。session の番号は `GPU_OP_CREATE_DEVICE` で取る（R16）。+ `CAPSET | COMMAND | NOTIFICATION | FENCE | JOB | JOB_CAPACITY` と recovery（受け入れ: p007 の場面 PASS、30 LW） | p005・p006 | planned |
| [p008](phase008/phase.md) | pipeline と descriptor: vk-pipeline・vk-layout・layout.h（受け入れ: p008 の場面 PASS、20 LW） | p007 | planned |
| [p009](phase009/phase.md) | 記録と実行: vk-pass・vk-record・vk-execute、draw・indirect・dispatch・barrier・dynamic state・転送・query・event・clear（受け入れ: p009 の場面 PASS、amd64・riscv64 の build の warning 0、36 LW） | p008 | planned |
| [p010](phase010/phase.md) | 転送の shader による代替（§11.6、U10）: `vk-meta.c`（内部の shader を IR で組む）、compute の copy・fill・update・query・buffer と image の写し・resolve、描画の blit、転送の後の bind の置き直し。gsi1model.py での試験（受け入れ: p010 の場面 PASS、build の warning 0、20 LW） | p009 | planned |
| [p011](phase011/phase.md) | discrete: local memory、heap 2 つ、window と boot framebuffer（受け入れ: p011 の場面 PASS、12 LW） | p005・p009・p010 | planned |
| [p012](phase012/phase.md) | 表示: DISP_SET・FLIP、DISP_IRQ、copied・cursor の写し、reset の数え直し、panic、boot framebuffer、adv7511。+ `DISPLAY | DISPLAY_EVENTS`（受け入れ: p012 の場面 PASS、22 LW） | p007・p009・p011 | planned |
| [p013](phase013/phase.md) | userland と UAPI: `GPU_OP_GHCI_*`・`CONFIG_GHCI_NATIVE` を外す、ghcictl、ghci-blit を削除の list に（受け入れ: ghcictl の build、amd64 の build の warning 0、4 LW） | p004 | planned |
| [p014](phase014/phase.md) | 実機の確かめ（VF2 + KC705、§16.2）。段ごとに Q1 の割当で（受け入れ: 段ごとの要約。KC705 の側の未実装は記録して切り分ける、12 LW） | p004（段 1）、p009（段 2）、p012（段 3）、ユーザーの hardware の用意 | planned |
| [p015](phase015/phase.md) | 規約の全文の見直し: WS が変えた C の全部、全 host 試験、amd64・riscv64 の warning 0、tree に 1.1 の code が無い grep、stub の `drv_gpu_register` を `gpu.c` と照合、試験を `plan/tools.md` に登録を Q1 に依頼（受け入れ: 規約の違反 0、全 PASS、warning 0、10 LW） | p002〜p014 | planned |

## 現在の状態・再開点

- 2026-10-26: 新設。次は p001（設計）。
- 2026-10-26 P1: p001 の [design.md](design.md) 版 2（Phase p002〜p013、196 LW、ghci.md の疑問 G01〜G39、判断 U1〜U7）。次: Q1 が G・U をユーザーに示し、design §18 から p002 以降を立てる。p002・p003 は回答なしで始められ、p004 は G04 の回答が要る。
- 2026-10-27 P3（q1621-i01、worktree `/home/awe/zedBSD-worktrees/p3-ws290`、branch agent/p3-ws290）: ghci.md ce9725a（docs/・include/ の最後の差 f718918）を読み design 版 3 を書きかけ（§0.1 改訂の要旨、§24 capability と Vulkan への対応、G の状態と G40〜G49、U8、Phase の表 212 LW）。design-reviewer 25 件（[design-review-v3.md](design-review-v3.md)）は V20 以外未反映。ユーザーの G01 の回答と「いったん作業を終了していいです」で中断。
- **再開の条件**: ユーザーが ghci.md（と資料全般）の修正の終わりを伝えた時。**再開の手順**: (1) その時の amaterrace-gpu main の `docs/ghci.md`・`include/` を `git show` で読み、SHA を記録（ce9725a からの差を読む）。(2) main の [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)（修正されていればその版）を読む。(3) design 版 3 を仕上げる: G01 の回答（単一の GHCI 1.0、版で分岐しない、機能は capability の flag だけ）を全体に、hardware の回答と修正後の ghci.md で G02〜G49 の状態を直し、design-review-v3 の各指摘が残るかを確かめて反映。(4) 改訂部分を design-reviewer に通して反映。(5) design §18 から p002 以降の phase.md を立て、この表を直す。(6) p002 から順に実装（host 試験だけ）。

- **再開点（2026-10-28 ハンドオーバー、Q1）**: GHCI の担当との疑問のやり取りは終了（ユーザー「では、GHCIチームとのやりとりを終了します。」）。正本は amaterrace-gpu の docs/ghci.md **89529de**（2026-10-28 02:48）。次の一手は p001 の仕上げ:
  1. `git -C /home/awe/amaterrace-gpu show 89529de:docs/ghci.md` を読み（読み取り専用）、[design.md](design.md) を版 4 に直す。版 2・3 の register 前提、2.0・1.1 の版の分岐、G・H・J・K・L・M・N の経緯の記述は捨てるか短くし、89529de の規則（3 つの番地の空間、session の fault、64 bit の fence、PSO・render pass・descriptor の配置、capability の bit）で組み直す。
  2. §24（capability → Vulkan）と [design-review-v3.md](design-review-v3.md) の V01〜V25 を 89529de に照らして反映。U8 などユーザーの判断が要る点は推しを添えて Q1 へ。
  3. design-reviewer を通し、§18 で Phase を確定、上の表と p002 以降の phase.md を立てる。
  4. その後 p002 から実装（試験は host だけ。ユーザーの決定）。
- 経緯の file（正本ではない）: ghci-questions*.md、ghci-answers-hw-*.md、ghci-review-<SHA>.md、[ghci-review.md](ghci-review.md)（第 9 版で「疑問は残っていない」）。
- Queue: 実装の Queue はまだ無い。p001 の仕上げは新しい Queue でユーザーの承認の後（q1621 は 2026-10-27 に paused のまま）。

## 2026-10-28 の再開（q1636、P3）

- ユーザー（2026-10-28）「P3はGHCIドライバを作成します。まずはホストで開発しますが、私が今からVF2ボードを準備して、新しいSwitchbotで制御できるようにします。VF2はKC705 FPGAボードに接続されており、プログラミングは私が行いますので、そのあと実機検証が可能です。Fableのレビューは不要です。」
- 解釈（Q1）: p001 の版 4 → p002 以降の実装を host 試験で進める。VF2（riscv64）＋ KC705 の GHCI の実機は、ユーザーが VF2 と新しい SwitchBot・KC705 の書き込みを済ませて伝えた後に Q1 が割り当てる（それまで実機の試験は無し）。U4（riscv64 は build だけ）は、実機で動かす事に変わる（riscv64 の attach の経路を設計に含める）。KC705 の bitstream の build・書き込みはユーザーがする（担当は `plan/tools/kc705/` を使わない）。設計のレビューは design-reviewer だけで、Fable のレビューはしない。

## 再開点（2026-10-28 待機、P3）

- 2026-10-28 ユーザー（Q1 経由）「P3はいったん待機してください。GHCI仕様をアップデートします。」→ q1636-i01 を通常のラップアップで止めた（worktree `/home/awe/zedBSD-worktrees/p3-ws290b`、branch agent/p3-ws290b、基点 main 6e6ebf830）。
- 読んだ仕様: amaterrace-gpu `docs/ghci.md` **89529de**（2026-10-28 02:48、2677 行）と同じ SHA の `include/`（20 file）、参考に `fw/cp/ghci_cmd.c`・`cmd.c`。読んだ時点の main は **4549f18** で、89529de..4549f18 に `docs/ghci.md`・`include/` の差は無い（`git log 89529de..HEAD -- docs/ghci.md include/` が空）。amaterrace-gpu には書いていない。
- 版 4 の進み具合: [design.md](design.md) を 89529de で全面に書き直した（§0〜§23: 要件の表、file の行き先、型と番地の 3 空間、attach・submit・IRQ・session の fault と装置の fault・reset と再生・session と VM（2 MiB の chunk、CP_HEAP 8.25 MiB の上限の勘定）・unified/discrete・detach、executor と descriptor／constant bank の取り決め、compiler の作り直し、DISP2、試験の profile と Phase ごとの場面、capability → Vulkan の表、riscv64 の実機、Phase p002〜p014（234 LW。p013 = 実機、p014 = 規約の全文）、S01・S02、U5・U8〜U11、版 3 の G と V01〜V25 の扱い）。**未実施**: design-reviewer、§18 の Phase の確定に合わせたこの表と p002 以降の phase.md の作成、source の変更（何もしていない）、build・試験。
- 見つけた仕様の欠け（design §19）: **S01** — §12.10（2264 行）が interface 表（kind 1〜14）を廃止したが、それが担っていた vertex shader の入力（属性が届く register、VertexIndex・InstanceIndex）、position・point size・varying の export の番号、fragment の `lda` の即値の意味が本文に無い（`include/gsi1_isa.h` 89529de は 1.1 の番号と interface 表のまま）。compute と fragment の入出力は本文で足りる。推しは design §19。**S02**（低）— MAX_MRT は 8 まで、§12.12 の export は k = 0〜3。他に Q1・ユーザーへの判断: U5 の読み直し（CAP_PROT = 1 の session の fault の回収は WS290、protected context は WS292）、U8〜U11（design §20）。
- 再開の手順: (1) 更新後の ghci.md・include の SHA を `git show` で読み、89529de との差を読む（S01・S02 が解けたかを含む）。(2) design 版 4 を差に合わせて直す。(3) design-reviewer に通して反映（Fable のレビューはしない）。(4) §18 で Phase を確定し、この表と p002 以降の phase.md を立てる。(5) p002 から実装（host 試験と amd64・riscv64 の build）。

## 2026-10-28 仕様担当とのやり取りの再開（q1637、P3）

- 2026-10-28 03:33 ユーザー「GHCI仕様担当とのメッセージングについてです。P3は、下記のプロトコルでGHCIドライバ担当とやりとりします。…受信は60秒おきにファイルをチェックします。以後、仕様担当とP3がお互いに納得できる回答が得られるまで、 このプロトコルで通信します。お互いに納得が得られた後は、一次停止しますが、指示があれば、送信や受信ポーリングを再開します。」
- 取り決め（ユーザーの説明の要約）: P3 は main の checkout の `plan/ws290/ghci-review.md` に上書き（1 行目を前の版と変え、末尾 `DONE`）。仕様担当は ghci.md を直し、`~/amaterrace-gpu/docs/ghci-review-reply.md` に返信（1 行目を変え、末尾 `DONE`）。双方 60 秒ごとに見る。納得したら一時停止、指示があれば再開。ghci-review.md の commit は Q1。

## 再開点（2026-10-28 レビュー再開、P3）

- 承認（ユーザーの原文、2026-10-28、Q1 経由）:「GHCI仕様担当とのメッセージングについてです。P3は、下記のプロトコルでGHCIドライバ担当とやりとりします。・P3は~/zedBSD-claude1/plan/ws290/ghci-review.md にレビューコメントを上書きします。先頭行が変更されており、末尾行がDONEになっていると、仕様担当はメッセージを受信します。・仕様担当はレビュー指摘を受けて、~/amaterrace-gpu/docs/ghci.md を更新します。さらに、P3（ドライバ担当）への返信のメッセージを ~/amaterrace-gpu/docs/ghci-review-reply.md に書いてDONEを末尾に追加します。先頭行が変更されており、末尾行がDONEになっていると、P3はメッセージを受信します。・すると、P3でレビューが走ります。レビューコメントは ~/zedBSD-claude1/plan/ws290/ghci-review.md に書き込まれます。先頭行が変更されており、末尾行がDONEになっていると、仕様担当は更新完了と認識して、レビューコメントを受信します。・受信は60秒おきにファイルをチェックします。・以後、仕様担当とP3がお互いに納得できる回答が得られるまで、 このプロトコルで通信します。お互いに納得が得られた後は、一次停止しますが、指示があれば、送信や受信ポーリングを再開します。」→ 上の「GHCIチームとのやりとりを終了します」による停止はこれで解け、q1637 で再開した（worktree `/home/awe/zedBSD-worktrees/p3-ws290c`、branch agent/p3-ws290c）。ghci-review.md は main の checkout に直接上書きし、commit は Q1。
- 2026-10-28 03:34 仕様は **bc7df9e**（§12.10.3「Shader Stage Register & Export Conventions (N37)」と ghci_cmd.h の `GHCI_VS_REG_*`・`GHCI_FS_REG_*`・`GHCI_EXP_*`）に更新済みだった。reply は 03:34 に一時 Version 10.0 の 1 行目が見えたが 03:34:41 に 9.0 へ戻ったので未読扱い。
- 2026-10-28 03:38 レビュー **第 10 版**（[ghci-review.md](ghci-review.md)）: N37（S01）は一部解決、残り N38〜N45（H 1・M 5・L 2）。design 版 4 の §0・§12・§15.2・§19 を bc7df9e に追従させた。
- 2026-10-28 03:41 回答 **第 10.0 版**（reply e4355be、仕様 **1a1fa85**）: N38〜N44 は推しどおり本文に入った（属性は有効な slot の成分の数で詰める・合計 60、r60/r61 は `used_regs_vs` ≥ 62、`lda` の word 0〜3 は clip の POS、vertex 0 が provoking、Vulkan の clip/viewport、point size 1、MRT k = 0〜7、SRC1 0x108〜0x10B）。
- 2026-10-28 03:45 レビュー **第 11 版**: 残り L 2 件（N45 の gsi1_isa.h の header・interface 表・旧 CAPS の残り、N46 の `VK_ERROR_INITIALIZATION_FAILED`）。driver と compiler の待ちは無い。design 版 4 を 1a1fa85 に追従（§0・§12 の 3・§15.2・§19・§20 の U8・§22・§23）。
- 2026-10-28 03:44 回答 **第 11.0 版**（reply 4188a4c、仕様 **cf8be08**）: N45（gsi1_isa.h の header・interface 表に pre-1.0 と明記、CAPS の名前）・N46（2298 行から Vulkan の戻り値の名前を除く）が解決。
- 2026-10-28 03:47 レビュー **第 12 版**「疑問は残っていない」（N01〜N46 全部解決、参考に gsi1_isa.h の旧名 1 か所を返信不要で）。03:46 に回答 **第 12.0 版**「Final Signoff: N01〜N46 Fully Resolved」（reply 0018f88、gsi1_isa.h bea1ef9 で参考の 1 か所も直した）。双方が合意したので、ユーザーの取り決めどおり送信と受信の polling を止めた（指示があれば再開）。
- 次（p001 の残り、新しい Queue で）: design 版 4（cf8be08 に追従済み、§19 の仕様の欠けは無し）を design-reviewer に通す（Fable は使わない）→ §18 で Phase を確定し上の表と p002 以降の phase.md を立てる → p002 から実装（host 試験と amd64・riscv64 の build）。ユーザーの判断 U5・U8〜U11（design §20）は Q1 へ。双方が納得したら polling を止める。その後 design-reviewer（Fable は使わない）→ §18 で Phase を確定 → p002 以降の phase.md。ユーザーの判断 U5・U8〜U11（design §20）は仕様担当に送らず Q1 へ。

## 2026-10-28 p001 の review と実装へ（q1638、P3）

- 2026-10-28 ユーザー「P3はOpus 5.5で設計レビューして、先に進めて下さい。GHCI仕様担当との通信が必要なときは、プロトコル通りファイルを用意して、私に言ってください。レビューでGHCI仕様に不備が見つからず、ドライバ設計をapproveできた場合、Phaseを確定し、実装に進んでOKです。」
- 解釈（Q1）: design-reviewer は Opus 5.5 で走らせる。review で仕様の不備が見つかれば ghci-review.md を取り決めの形（1 行目を変え、末尾 DONE）で用意し、Q1 がユーザーに知らせる（送る・見張るはユーザーの指示の後）。不備が無く design を approve できたら p001 を cleared にして Phase を確定し、p002 から実装。U5・U8〜U11 は推しで進め、ユーザーが変える時は直す。
- U の回答（2026-10-28、Q1 の問いへのユーザーのクリック、選んだ選択肢の文のまま）:
  - U5「全部 WS292」: CAP_PROT=1 に関わる物は session の fault の回収も含めて WS292 で扱う。WS290 では PROT=1 の device も PROT=0 と同じに扱う。
  - U8「1.0 を名乗り、断って log」: Vulkan 1.0 の device として出し、出来ない操作は呼ばれた時に断って log に残す。
  - U9「どちらも出す」: flat の device（HW_WALKER=0）にも PROT=0 の device にも Vulkan を出す（process 間の memory の保護は無くなる）。
  - U10「shader で代える」: CAP_BLT が無い時、copy・blit・resolve を compute か描画の shader で代える。
  - U11「hardware の上限の 15 個」: client に使える 15 個まで、超えたら vkCreateDevice を断る。
- 2026-10-28 ユーザー「はい、P3は仮の扱いで実装を行ってよいです。」→ 仕様の回答を待つ点は design の仮の扱いで実装する（仮の所は phase.md に一覧）。仕様担当とのやり取りは並べて続ける。


## 再開点（2026-10-28 05:50、P3 q1638）

- design 版 4.4（3ba1b7b6d）: U5・U8〜U11 の反映、design-reviewer（Opus 5.5）3 回（H 6・M 18・L 10 → H 4・M 12・L 9 → §5〜§8 の H 4・M 8・L 4）を全部反映（design §24）。3 回目の判定は「T01〜T04・T09・T11 を直せば p004・p005 は approve 可」で、直した版はまだ再確認していない。
- 仕様の不備: [ghci-review.md](ghci-review.md) 第 13 版（N47〜N60、main 1b88e6d24 で Q1 が commit）。2026-10-28 ユーザー「GHCI仕様担当が、レビューを受け取って、仕様をアップデートしています。プロトコルに従って相互アップデートを開始します。P3は自律的に仕様アップデートを受け取ってレビューを返し、相互に解決するまで継続してください。」→ 05:39 から reply（既読 Version 12.0）を 60 秒ごとに見張る。第 14 版に入れる新しい疑問の下書き: N61（DISP_SET の大きさ）・N62（texture slot が draw 全体で 16）・N63（8 byte の color attachment・A2B10G10R10）・N64（swizzle・unnormalized の field）・N65（attribute 60 語）・N66（viewport・subpixel の limit）・N67（文書の中の食い違い 4 件）。材料は main の plan/ws290/vk10-fixed-gaps.md（Q1）。
- 次: reply を受信 → 仕様の差を読む → 第 14 版を main の checkout の ghci-review.md に上書き（1 行目を変え末尾 DONE、commit は Q1）→ design を追従して WIP commit → Q1 に知らせる。双方が納得したら polling を止める。

## 2026-10-28 Phase の確定と実装（q1638、P3）

- ユーザー（2026-10-28、Q1 経由）「はい、P3は仮の扱いで実装を行ってよいです。」→ 仕様の残りの疑問（ghci-review 第 14 版 N62〜N72）を待たず、design §19 の仮の扱いで実装する。仮の所は各 Phase の phase.md に一覧で残し、仕様が変われば直す。
- ユーザー（2026-10-28、Q1 経由）「GHCI仕様担当が、レビューを受け取って、仕様をアップデートしています。プロトコルに従って相互アップデートを開始します。P3は自律的に仕様アップデートを受け取ってレビューを返し、相互に解決するまで継続してください。」→ 第 13 版（05:08）→ 回答 第 13.0 版（cb937e6、05:57）→ 第 14 版（06:00）。回答の見張りを続ける。
- p001 cleared。Phase の表は上（design §18 の p002〜p015、266 LW）。

