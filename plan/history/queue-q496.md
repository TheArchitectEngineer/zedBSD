# Queue q496: Remacs 用 host Noct の make 失敗

Status: finished（2026-09-29）
Approval: 2026-09-29 ユーザーの「makeするとtoolchainのnoctが失敗するようです。直せますか？」。範囲は失敗の再現と Noct/Remacs の build 規則の修正。
Timebox: この依頼の作業期間。依存: canonical host Noct が build 済み。

| Order | Attempt | Phase | Status |
| --- | --- | --- | --- |
| 1 | q496-i01 | [ws073-p028](../ws073/phase028/phase.md) | cleared（Noct smoke と Remacs の bytecode 再生成 PASS） |

全体の `make -j16` は clang package の大規模 build の途中で中断。外部の GitHub への同期は実施していない。
