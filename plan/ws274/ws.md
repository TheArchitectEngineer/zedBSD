<!-- awesome-plan project=zedbsd record=ws274 -->

# WS274 仮想メモリの code を src/kern/vm/ に分けて整理する

- ID: ws274、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: planning（2026-10-19 15:45 Q1 作成。案の段）
- 起点（2026-10-19 ユーザー）:「vm.cが長すぎて複雑すぎるという指摘を受けました。そういえばあとで整理しようとして未着手でした。src/kern/vm/以下に分割して整理する案を作れますか？まだ案でいいです。vm.c, vmspace.cなど、仮想メモリ系統をvm/に分割して整理して入れる案です。」
- 案: [design.md](design.md)（vm.c 10350 行・vmspace.c 7481 行を、object・content・fault・sync・cache・prefetch・page・commit・metadata lock・private page・page の追跡・reclaim・pageout と、vmspace の寿命・metadata・fault・copy・fork・map・region・unmap、device、swap に分ける。中心 約 33 LW、任意を含め 約 50 LW）。
- 担当: 未割当。

## 完了の条件（案）

1. 振る舞いを変えずに src/kern/vm/ へ分け、全部の platform の build が warning 0、関数の本文の照合が一致、kd-host-test・pin-host-test が PASS、T1 の起動の回帰が PASS。
2. 判断（design.md §6 の 1〜8）は着手の前にユーザーに聞く。

## Phase

未定（判断の後に design.md §3 の S0〜S9 から作る）。
