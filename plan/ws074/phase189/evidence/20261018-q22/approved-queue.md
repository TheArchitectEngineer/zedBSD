<!-- awesome-plan project=zedbsd record=queue -->

# ws074-q20261018-22 / harness HTTP停止待機の短縮・全1491同値確認

Status: active（2026-10-18）
Owner: このCodex chat単独。
Approval: [ユーザー自走指示](autonomy.md)をこの有限scopeへ適用。q21 cc65dc3f6/history hash read-back済み。
Timebox: 最大45分（16:50 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-22-i01 | [p189](phase189/phase.md) harness shutdown partial | WptServer workerのserve_forever poll_intervalをPython既定0.5秒から0.01秒へ明示する1行変更のみ。browser終了後shutdown/select待機を短くし、HTTP内容・report callback追加・URL/query・分類・eligible1491/未attempt292・fonts・settle5000/process20を変えない。既存分類5対照、同じq21 frozen browser/libで全1491を実測し、q21全ID/whole/callback全status/順序/欠損/追加・membership hash一致とelapsedを確認、raw/gzip/checkpoint/commands/hashを全保存。意味のある差異が残ればuncleared。native C/SDK/WPT/batch/census/static/scripted/crash/基準/分母は変更しない | in-progress | q21 final全1491 493 PASS/11538 callback PASSとfrozen pair/1491 raw、stdlib3.13 serve_forever/shutdown実source |

Dependency graph: q21 history/baseline/pair → 1行poll設定 → 既存分類5 → 全1491実測/同値照合/elapsed → archive/清算。
Generation bounds: plan/ws074/tests/run-interop2025-harness.pyのThread kwargs1行とWS074記録/証拠だけ。Python専用full standard無し、Guardrail/既存tool・FS規則を直接適用。mirror testは追加せず、既存font fixtureのTemporaryDirectory cleanup試験は未選択、5つのpure分類対照を使う。新native source不変なのでstatic1561/scripted413/crash48/DOM/C/Acidはq21のbyte同一pair証拠を再利用し再実行しない。push/削除/make check/QEMU/共有board・cache/toolchain/sysrootを行わない。
Bounds: 全1491既存40分上限/Queue45分を守り、実測前に速度改善を主張しない。HTTPが使える既存escalated環境で実行し、未完/差異はraw/resumeとunclearedを残す。whole p189/p100/WS100%は先取りclearしない。
Upcoming Work Outlook: generatedWindow等の未attempt292と環境5、native animation/module/ViewTransition/anchor、p249/target/p100。
