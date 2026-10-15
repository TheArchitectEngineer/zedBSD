# 記録の調査（2026-10-15 15:45、調査のサブエージェント、repository は変えていない）

ユーザー「他にも記録の食い違いがあるかもしれません。サブエージェントを立てて、記録の調査をし、報告させてください。」。報告の要約を Q1 が写した。**[Q1 確認]** は Q1 が自分で確かめた物。直す手順は [record-integrity.md](../../record-integrity.md)。機械の一覧: [status.txt](status.txt)（`plan/tools/record-audit/status.py` の出力、2026-10-15 15:55）・[links_broken.txt](links_broken.txt)・[paths_broken.txt](paths_broken.txt)。

## この session で直した物（Q1、2026-10-15）

- A1 LW の換算 → 1 LW ≈ 実時間 15〜25 分に揃えた（cff98dcc0）。
- A2 Master の先頭の block → 今の状態に書き直した。前の経過の 2 行は decisions-log へ。
- A3 WS037・A4 WS236 の ws.md の現状の欄 → 書き直した（ee9aafa77）。
- A5 design.md の VBT の fixture の案 → 先頭の注記で取り下げ（本文は WS245 の次の改訂で）。
- A6 WS245 の表の p001 と決定 1 の Phase の番号 → 直した（ee9aafa77・714e7a906）。
- A8 Queue の次の番号 → q1200 から（ユーザーの承認）。

## 次の session で直す物（ユーザーの合意: 量の多い物は担当に分けて）

### 重い

1. **A7 Bug Board と ticket の先頭の状態** [Q1 確認]: BUG-318・320・321 は board が resolved で ticket の先頭が scheduled、BUG-294 は board が resolved で ticket が tracking、BUG-317 は board が tracking で ticket が scheduled。
2. **A9 Queue Board の欠け**: q937〜q983 の行が queue.md に無い。P1〜P3 の lane は 10-14 0 時台から未更新。queue.md:25-28 の「Active」「次の未予約 ID は q923」が古い。10-15 の WS037・WS236・WS239・BUG-318〜321 の作業にも行が無い。
3. **ユーザーの決定の decisions-log への記入**: 10-15 の決定（nvrtx の既定 on、Pi の VCHIQ・VIDEO の既定 on、Pi の BT、720p30、A3 の revert、R8 の合格の条件、MIT・GPL の guardrail など）が先頭の block と ws.md にしか無い。この session の決定は decisions-log に入れた。前の Q1 の session の分が残り。priority の block（10-06）と outlook の block（10-05）も古い。

### 古い・欠けた記述

4. Master の WS の一覧に無い WS: WS177・179・180・181・203・233〜239。
5. 一覧と ws.md の状態の食い違い 54 件（status.txt の REGDIFF）。ws.md が completed で一覧が未完の物（WS029・061・066・073・078・085・088・114・122・148・149・151・160・166・167・188・192・199）、WS232（一覧 completed、ws.md incomplete）、WS231（ws.md が planned のまま）など。
6. Phase を実行したのに WS が planning・planned のままの物 19 件（WS_NOT_STARTED_BUT_EXEC）。
7. ws.md の Phase の表と phase.md の食い違い 47 行（TABLEDIFF）。
8. completed の WS に残った Phase の directory（ws035 の 120 個、ws073 の 55 個など 12 WS）と試験（ws005/tests の 25 個など）。ユーザーの承認（2026-10-15）: 規則どおり Q1 の手順で消す。消す前に、今後も使う試験は plan/tools/ に移して Master の Tools に登録する。
9. plan/agents/registry.md が 10-11 のまま。
10. handover-20261015b.md の中の古い記述（「P1 の改訂は未完」の段落）。
11. beta2.md が「今の状況（2026-10-13 夜）」のまま。WS217 p003・WS196・Codex の WS074 の記述。
12. ws241/phase001: 「実機は未実施」の行が T1-697 の結果と並ぶ。T1-697 は requests.md に行が無い。
13. 工数の表（master.md）に completed・closed の WS 30 件の残りの LW。
14. Tools と判断の表の path（master.md:1040 の試験の path、:958 の Birch-Lake.ppm → .png、:1134 の demo-accounts.sh、:675 と known-bugs.md:284 の plan/ws074/tests/rtld-dlopen.sh）。
15. WS118 の 59a94c6aa（作り直しの前の commit）の記述が ws.md と registry.md で食い違う。

### 日付・体裁

16. 「2026-10-15 夜」は 10-14 から 15 にかけての夜を指す（10-15 02:31 の 4afe4ab99 で書かれた）。master.md・ws037・ws236・ws237・ws239・ws074・ws037/phase006・phase007・ws236/phase011 の 12 か所を「10-15 未明」などに。
17. T1 の台帳: T1-642 が 2 行（agents/T1/requests.md:718・:720）、T1-681 の link が T2 を指すべき（:750）。
18. B1 の証拠の画像（ws074 p228〜p236 の build/review/*.png 14 個）が host に無い。p189〜p239 の 17 個の phase.md に Status の行が無い。
19. Status の行が無い Phase: ws031 の 18 個。規定に無い状態の語（test-wait・「cleared 候補」）。known-bugs.md の BUG-293・295・296 の行は列が 1 つ多い。
20. 今も使う文書の壊れた link: ws237/design.md:5・:14・:149、ws089/ws.md:101、future-work.md:64、guardrail.md:192、ws079/guide.md:281（他の 70 件余りは履歴）。
21. guardrail の vc4 の古い節と 10-15 の「GPL の定数・表も引用元を示して使ってよい」の節の関係（矛盾かどうかは未確認）。

## 調べなかった範囲

plan/ws074 の中身（機械的な点検だけ）、plan/history/ の本文、T の結果の本文と cleared の判定の照合、全 ws.md の Resume point（WS037・236・241・245・039 以外）、guardrail の古い節、code と記録の照合（config の 3 file 以外）、GitHub。
