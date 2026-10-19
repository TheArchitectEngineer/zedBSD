<!-- awesome-plan project=zedbsd record=ws269-p010 -->

# ws269-p010 graphics（TA・3D・transfer・render pass・WSI）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §6 の p010、追補 [design-p010.md](../design-p010.md) 版 2.1
- Queue: q1362 / q1362-i01（P1、2026-10-20、追補 版 1）、q1367 / q1367-i01（P1、2026-10-20、版 2: review 1 と p010b の反映）。q1370 / q1370-i01（P1、2026-10-20、版 2.1: review 2 の反映）。どれもコードなし
- Status: in-progress（設計の追補 design-p010.md 版 2.1 を 2026-10-20 Q1 が差分で確かめて受け入れた（design-p010-review-2 の結論「Q1 が差分を見るだけで足りる」、67ac336aa、merge 1a57f1153）。p010d 以降の実装に入る。O-ENC の SR の burst の ld・st の 1 点は P2 の p013 で確かめる）
- 起点（2026-10-20 07:24 ユーザー）: 「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」

## 分割（版 2）

p010 を p010a〜p010o の 15 の Phase（a・b は済み、c〜o は各 3〜10 LW、合計 97〜99 LW）に分け、compiler を p013（encoder の広げ、O-ENC）・p014（compiler、O-SHADER-ABI・O-VSFS）として ws.md に作った（P2）。依存は名前を付けた出力（O-P006・O-P007・O-P009・O-p010x）。範囲・依存・確かめは [design-p010.md](../design-p010.md) §10。

## 決めた事（版 2 の要約）

- image は LINEAR（rowPitch 32 byte かつ 2 pixel の倍数）と twiddled（OPTIMAL、CPU は触らない）。p010l まで linear の features を出さず external は ENOTSUP（shared の道に入らせない）。
- render: heap（PDSCODEDATA・USCCODE の割り当て、RGNHDR、TRANSFER_FRAG）、FW の context 4 つ（identity・recovery も 4 つ）、render context の static state と SR・PT を Mesa の形で、global・local の free list（GROW_UPDATE は delta 0、RECONSTRUCTION に応える）、HWRT は rt_data 2 組で時間切れ・HWR の後に作り直す、1 render = geometry・FRAG_PR・fragment、EOT と pixel event の PDS。
- transfer は IPF を TRANSFER_FRAG の heap に、FAST_2D（copy・clear は p010f、blit は p010o）。
- compiler の境: compile の入力に attrib・output の format・blend の設定・sample の数、要求の表に BLEND_CONSTANTS、pack と blend は compiler（D-P3 (b)）。
- WSI: readback（OPTIMAL から copy）を p010k1（clear だけ、compiler なし）・p010k2（vkdemo）、shared を p010l（受け入れに必須）。
- UAT の段の行は順を決めず、kick ごとの sync の word と context ごとの identity で止まった段と reset・fault の理由を出す。段の名前は `render-node` に揃えた。
- p010 の受け入れから venus-frame を外す（D-P7）。

## ユーザーの判断（推奨を採って進める、採否はユーザー）

design-p010.md §9 の D-P1〜D-P7。D-P3 は (b)（Q1 の判断）。新しく D-P6（sysctl の UAPI を残すか）と D-P7（受け入れから venus-frame を外す、design.md §6 の文面の変更）。design.md §3.5（free list の grow）と §6（p010 の受け入れ）の改訂は Q1。

## 確かめ

| 確かめ | 結果 |
| --- | --- |
| 追補の文書 | 版 2 を書いた（2026-10-20 q1367）。末尾 §12 にレビュー 1 の全指摘への対応の表 |
| p010a の code の comment | Guardrail の開発の管理情報（`ws269-p010a`・`design-p010.md`）を stage.c・stage-log.c・stage.h・gpu-selftest の main.c・Makefile から消した（cbb9bc09e）。段の名前の console の行を `render-node` に揃え、host 試験 `plan/ws269/tests/p010/run.sh build/p010a/host4` で `stage-host-test: 24 passed, 0 failed`、`selftest-host: 0 failed`（1a5d6a3e9） |
| design-reviewer | 版 1 はレビュー 1（[design-p010-review-1.md](../design-p010-review-1.md)）。版 2 はレビュー 2（[design-p010-review-2.md](../design-p010-review-2.md)、Blocker なし）。版 2.1 は Q1 の差分の確認（再レビューは要らない、review 2 の結論） |
| 版 2.1 | 2026-10-20 q1370。末尾 §13 にレビュー 2 の全指摘への対応の表。主な変更: 時間切れの後は HWRT・free list を core に残す、PBE の clip を必須に、combined kick・FENCE_PR・`pds_pr_bgnd`・cleanup ctl、static data area（BUG-342）、105/112 の扱い、p010n の実機を p010e へ、p010f に transfer の context の recovery、§5.4 の O-ENC の固定、LW 105〜107 |

## 残り

- design-reviewer の再レビューと対応。
- D-P6・D-P7 のユーザーの判断、design.md §3.5・§6 の改訂（Q1）。
- p013・p014 の phase.md（P2）と、`shader.h` の合意。
