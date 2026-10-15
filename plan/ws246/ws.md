<!-- awesome-plan project=zedbsd record=ws246 -->

# WS246: nvrtx の shader compiler を sm75.h・sm75.c にまとめて refactor し、出典を定数・構造体・関数ごとに明記する

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 ユーザー「残り使用量でできそうです。今やってください。」→ Q1 が p001 を実施）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md) p008（compiler）、[WS242](../ws242/ws.md) p002（SM86・SM89）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: p001 cleared（Q1 d5d72059d: sm75.h 948 行・sm75.c 5950 行にまとめ、使う側・vmunix.mk・host 試験を追従。build warning 0、nvrtx-sm75-host-test 287/0、desktop 138/0、vk-d3 122/0、vk 60/0）。referenced: の comment を複数行の形に折り返した（ユーザー）。次は p002（定数・構造体・関数ごとに出典を探す、latency の if の連鎖を表に、NAK に沿った構造の見直し）。
<!-- awesome-plan-current:end -->

## ユーザーの提案（原文、2026-10-15）

「提案ですが、compiler/sm75-*.h はcompiler/sm75.hにまとめ、 sm75-*.cもsm75.cにまとめるのがいいと思います。全体としてzedBSD独自コードが増えれば、ソースファイルごとに引用比率が高すぎる問題が緩和されます。まとめた上でリファクタリングし、定数、構造体、関数ごとにreferenceを探して明記します。」

## 対象（2026-10-15 の main）

`src/drivers/gpu/nvrtx/compiler/`: sm75-compile.c 3428 行・sm75-encode.c 1055・sm75-schedule.c 1492・sm75-isa.h 564・sm75-compile.h 128・sm75-encode.h 192・sm75-schedule.h 96（計 6955 行）。使う側: nvrtx の compute.c・draw.c・vk-pipeline.c・vk-execute.c、platform/amd64/vmunix.mk、plan/ws037/tests/ の nvrtx-sm75-host-test・nvrtx-sm75-desktop-test・nvrtx-vk-d3-host-test・nvrtx-vk-host-test・sm75-machine・sm75-reference。

## Phase（案）

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001（cleared） | まとめ: header を sm75.h（外向きの口と内部の定義を分けるかは設計で決める）、source を sm75.c に。使う側・vmunix.mk・host 試験を追従。動作は変えない（SM75・86・89 の出力が byte 単位で同じ、host 試験 287/0・desktop 試験 138/0、build warning 0） | planned |
| p002 | refactor: 定数・構造体・関数ごとに出典を探し、「Value(s) referenced:」「Procedure referenced:」を block ごとに付ける（file の上部には書かない）。latency の if の連鎖は表に直す。Mesa NAK に沿った構造（encode の関数の分け方、scoreboard の割り当て）は自前の形に直すか、手順の出典を付ける | planned |
| p003 | 規約の全文の見直し | planned |

## 注意

- WS242 の残り（P2: driver の 4 か所で `_sm` の関数を使う、QMD 3.0）と同じ file の口を触るので、順序を Q1 が決める。
