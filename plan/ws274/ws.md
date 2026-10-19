<!-- awesome-plan project=zedbsd record=ws274 -->

# WS274 仮想メモリの code を src/kern/vm/ に分けて整理する

- ID: ws274、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: incomplete（2026-10-19 設計の承認、着手。旧: planning（2026-10-19 15:45 Q1 作成。案の段）
- 起点（2026-10-19 ユーザー）:「vm.cが長すぎて複雑すぎるという指摘を受けました。そういえばあとで整理しようとして未着手でした。src/kern/vm/以下に分割して整理する案を作れますか？まだ案でいいです。vm.c, vmspace.cなど、仮想メモリ系統をvm/に分割して整理して入れる案です。」
- 案: [design.md](design.md)（vm.c 10350 行・vmspace.c 7481 行を、object・content・fault・sync・cache・prefetch・page・commit・metadata lock・private page・page の追跡・reclaim・pageout と、vmspace の寿命・metadata・fault・copy・fork・map・region・unmap、device、swap に分ける。中心 約 33 LW、任意を含め 約 50 LW）。
- 担当: 未割当。

## 完了の条件（案）

1. 振る舞いを変えずに src/kern/vm/ へ分け、全部の platform の build が warning 0、関数の本文の照合が一致、kd-host-test・pin-host-test が PASS、T1 の起動の回帰が PASS。
2. 判断（design.md §6 の 1〜8）は着手の前にユーザーに聞く。

- 決定（2026-10-19 17:43 ユーザー）: 設計を承認、§6 の 8 つの判断は「既定のとおり」。VF2 の後に着手（VF2 の板の前の code が済んだので着手）。

## Phase

design.md §3 の段。各段は build（amd64 の LTO・gcc の LTO 無しの platform・pcat の object の一覧）warning 0、関数の本文の照合、nm の比べ、kd-host-test・pin-host-test。最後に全部の platform の build と T1 の起動の回帰。

| Phase | 段 | LW | 状態 |
| --- | --- | --- | --- |
| p001 | S0 準備（KERN_VM_SOURCES・KERN_VM_OBJS、11 の platform、本文の照合の script）＋S1 移動（git mv で vm/vm.c・vm/vmspace.c・vm/device.c、試験の path・docs）＋S2 commit と metadata lock の切り出し | 7 | cleared（2026-10-19 Q1、T1-736） |
| p002 | S3 匿名 page の群（J・K・L・M）と reclaim-internal.h | 5 | in-progress（code と host の確かめは済み（92fd627a2）、main への merge と T1 待ち） |
| p003 | S4 object の群（A〜G）と object-internal.h（必要なら S4a・S4b） | 10 | in-progress（q1299、P1、2026-10-19） |
| p004 | S5 vmspace の群と vmspace-internal.h、pin-host-test の追従 | 8 | planned |
| [p005](phase005/phase.md) | S6 swap.c を vm/ へ移して 4 つに（判断③） | 4 | in-progress（q1298、P2、2026-10-19。code と host の確かめは済み、T1 待ち） |
| p006 | 死んだ code と重複（判断⑦、約 5 LW） | 5 | planned |
| p007 | S9 最後の確かめ（全 platform の build、T1 の起動の回帰）と規約の全文の見直し（S8 の接頭辞を含むかは p006 の後に判断） | 3〜10 | planned |

