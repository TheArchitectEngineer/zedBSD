# P4 Queue lane

| Queue / attempt | Phase | Scope | Approval | Timebox | State |
| --- | --- | --- | --- | --- | --- |
| q593 / q593-i01 | [ws095-p012](../../ws095/phase012/phase.md) | IME の辞書を千語に拡張、held-out で測る | 2026-10-02 user「作業を開始しましょう。」 | 4h | finished / cleared |

| q595 / q595-i01 | [ws127-p001](../../ws127/phase001/phase.md) | Files の棚卸し（source 不変） | 継続 dispatch（user 2026-10-02） | 3h | finished / cleared |
| q603 / q603-i01 | [ws095-p013](../../ws095/phase013/phase.md) | BUG-139 preedit の大きさ | user 2026-10-02 | 4h | finished / cleared |
| q604 / q604-i01 | [ws095-p005](../../ws095/phase005/phase.md) | 候補の窓と右上の IME の status | user 2026-10-02 | 4h | finished / cleared |
| q606 / q606-i01 | [ws129-p010](../../ws129/phase010/phase.md) | 全 desktop app と base の config | user 2026-10-02 | 3h | finished / uncleared（i386 未検証） |

Next（予約）: ws127-p002（Files の改善）→ ws089-p010

## Merge requests
| P4-001 | q593 | 1c049e599..c9d9187ad（base 901037f9f） | userland/desktop/ime・plan/ws095 | integrated c7bbbf06a |
| P4-002 | q595 | 7f5e0199f..2142b1fc6（前回 c9d9187ad） | plan/ws127 | integrated 3381b12ab |
| P4-003 | q603 | 12bc714ea（前回 2142b1fc6） | textedit・plan/ws095・BUG-139 | integrated c0449523a |
| P4-004 | q604 | 4ca0b3e80（前回 12bc714ea） | wayland/{input-method,ime.h,compose,protocol,shell}・ime/・vmunix.mk の keiland-ime の link・plan/ws095 | integrated 5634eea24 |
| P4-005 | q606 | fe52488bc（前回 4ca0b3e80） | config/ci・config-amd64-userland.mk・wayland/{apps.conf,Makefile,home.c}・plan/ws129/phase010 | integrated 9271d769d |

2026-10-02: 利用枠の配分で待機（N=3）。予約 ws127-p002（files-p011.sh の 7 app の期待の直しを含む）→ ws089-p010 は保持。


## q699（2026-10-04 20:35 JST、user の追加指示）

Status: finished。Executor: P4単独、worktree `/home/awe/zedBSD-worktrees/p4` / branch `agent/p4`、base `f1fa284`。Timebox: この修正と確認を22時まで。q697/q698はclearedで終了済み。

承認元: 現在のuserが q698 の原因・修正計画の報告に続けて「では、修正してください。」と指示した。q698 の「製品実装しない」は当該調査attemptの履歴として保持し、今回の指示で以下の実装を承認したものとする。mainの未予約IDはq699、既存lane・Queueで同IDの使用が無いことを確認してP4のlocal laneに予約。共有Queueへの投影はQ1へ保留。

| Queue / attempt | Phase / Bug | Exact approved scope / criteria | Prerequisite | State |
| --- | --- | --- | --- | --- |
| q699 / q699-i01 | ws005-p032 / BUG-158 | q698 ticketの計画どおり、command_write_sequenceを16 bitで持ち、publish時だけ進める。DMA slotとwire tokenは256のまま。init/resetで0、prepare/abortは未消費、曖昧なCSR失敗では巻き戻さずreset必須。実transportの短いhost試験（256/512/65536、inline/external、abort/失敗/reset）、amd64 kernel build・変更範囲の全文規約確認、値の補正無しのUAT imageで5330 AX211単独・profile無し・console10分。host応答、複数の256境界越え、対象SW_ERRORと周期restart無し、正常なvCPU待ちを確認。製品の接続機能やLTR等の追加変更は含めない | q697/q698の実測と計画、base f1fa284 | cleared |

Graph: q697（cleared）→ q698（cleared）→ q699。source所有: `intel-ax211-transport.c/.h`、そのhost試験とP4の既存tracer（必要なread-only観測のみ）、BUG-158、Bug Boardの該当行、ws005-p032とP4 lane。main編集・merge・pushは従前どおりQ1の統合へ。GitHub公開はpending。

### q699 の終了（2026-10-04 20:51 JST）

q699-i01 **cleared**。WIP `acb4afa`（製品修正と回帰試験）、`999eb8b`（承認/checkpoint）。ordinary/ASan/UBSanで65536境界・abort/失敗/resetをPASS、旧実装は256で失敗。最終kernel build警告0、実行ELF hash一致。補正無しpassthrough688.531秒、console10分24秒、256境界10回、SW_ERROR/restart/recovery/panic各0、host ping/SSH各70回成功、最後12 vCPU idle。[Phaseの結果・引き継ぎ](../../ws005/phase032/phase.md)と[BUG-158](../../bugs/BUG-158.md)へ証拠を保存した。

QEMU/monitor終了、5330 AX211 driver無し・override(null)・blacklist保持、USB有線正常。whole Phase in-progress / BUG tracking / 実機UAT待ち。main統合と共有Queue/WS/Master/Past Log/GitHubへの投影はQ1へ保留。追加Queueは開始せずP4は停止する。
