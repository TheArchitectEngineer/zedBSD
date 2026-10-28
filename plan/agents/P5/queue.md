# P5 の Queue lane

## 2026-10-29 01:37 generation

Worktree `/home/awe/zedBSD-worktrees/p5-design`、定義 kernel-and-driver-designer。

| Queue | Phase | 範囲 | State |
| --- | --- | --- | --- |
| q1666 | 詳細の決まっていない WS の設計 | code は書かない。判断は Q1 経由でユーザーへ。P1〜P4・P6 の WS は除く。原文「P5は詳細が決まっていないWSの設計のみを、ユーザの判断を聞きながら全般的に進める」 | in-progress |

- 2026-10-29 追加（q1666 の範囲）: ユーザー「設計のためにFable 5.1で調査が必要なWSは何であるか、表にして教えてください。優先度の高いものから、P5に依頼しつつ、その管理下のサブエージェント(Fable 5.1 High)に依頼したいです。」→ P5 が配下に Fable 5.1 High の調査だけのサブエージェントを立ててよい（同時 2 本まで、結果は P5 が設計へ）。順: WS242 → WS250 → WS217 → WS303 → WS301 → WS300 → WS306 → WS295 → WS186 → WS287 → WS225 → 保留の WS（WS080・WS082・WS150・WS201・WS147・WS144・WS098）。
- 2026-10-29 追加 2: ユーザー「さらに、設計のためにOpus 5.5で調査が必要なWSは何であるか、表にして教えてください。優先度の高いものから、P5に依頼しつつ、その管理下のサブエージェント(Opus 5.5 High)に依頼したいです。」→ zedBSD の source・移植の対象の棚卸しは Opus 5.5 High の調査だけのサブエージェント（同時 2 本、Fable と合わせて 4 本まで）。順: WS302 → WS281 → WS001 → WS171 → WS209・WS204・WS205・WS197 → WS212・WS158・WS152 → WS198・WS115〜WS117・WS126 → app の WS（WS176・WS180・WS184・WS185・WS234・WS146・WS280・WS285・WS013・WS026・WS172）。除く: WS153・WS240・WS257・WS226・WS292。
