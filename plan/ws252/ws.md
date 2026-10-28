<!-- awesome-plan project=zedbsd record=ws252 -->

# WS252: i915 の Rocket Lake・Alder Lake-S/Raptor Lake-S・Ice Lake の GT（描画の engine）の対応

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p006・p008 は実装・host 試験・5330 の回帰 PASS（2026-10-26 q1599 P4）。残りの p007 は RKL・ADL-S・ICL の実機の確かめだけ（実機が無い） → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

Related: [WS245](../ws245/ws.md)（Gen12 の display の書き直し、§7 #18・#20・#21）

## 目標

RKL・ADL-S/RPL-S（Gen12）と ICL（Gen11）の GT（render・copy・compute の engine、Vulkan の executor）を i915 で支え、`identifiers[]`・`device.c` に ID を足して attach する。設計は [design.md](design.md)（版 3.1、[design-review-3.md](design-review-3.md) の Minor 1〜9 に対応）。

## ユーザーの決定

- 2026-10-15（クリック）: WS245 design.md §7 #20「RKL・ADL-S・ICL の GT（描画の engine）は起こさない。この WS は display だけ。GT の対応は別の WS。」への回答「別の WS を今立てる」。
- 2026-10-19（クリック）: D2「実機ができたら全部」、D1-R「C 既定で付ける」、D1-I「C 既定で付ける」、D3「Meteor Lake対応のときに入れます。」（この WS では GuC・HuC を使わない）、D5「無いまま」。
- 2026-10-19（クリック）:「設計まで」（実機が無いので p001 の設計で止める）。→ 2026-10-26 23:17 ユーザー「P4を立てて、WS244, WS245, WS252の実装を終わらせます。今から5330を起動するので、それにアクセスしてテストしてほしいです。」で実装へ（q1599）。
- 2026-10-27 q1601 ユーザー「zedBSD-firmware/ツリーに、i915の依存としてすべて入れられませんか？」→ DMC の firmware を i915-firmware の package へ。

## 結果

- p002（f8b65c839）: Gen12 の platform の記述の行（TGL・ADL-P・RKL・ADL-S・RPL-S）、gen12-golden を platform ごとに、Linux から期待を抜き出す script。
- p003: RKL・ADL-S・RPL-S を既定で attach（D1-R=C）。
- p004〜p006（e68361995・4e316feac・557c9de05）: ICL の GT の core と WA の list、Gen11 の EU の compiler と state（gen110）、video・PXP の除外と既定の attach（D1-I=C）。
- DMC（q1601）: driver が名前を出す DMC の全部（icl・tgl・rkl・adls・adlp）を i915-firmware の package に入れた（linux-firmware dc85cced から取得、sha256 を照合）。
- p008: 規約の全文の見直し（merge 636a82593）。
- 確かめ: host 試験（gen12-golden の Linux の照合、render-host、eu-gen11）と 5330（display 13、Gen12）の回帰 PASS。TGL・ADL-P の MOCS の列は host の golden で不変。

## 制限・移管

- RKL・ADL-S/RPL-S・ICL の実機の確かめ（p007: GT の初期化・Vulkan の三角形・blit・video）は実機が無く未実施（D5「無いまま」）。
- Gen12 の MOCS（TGL・RKL の表、entry 63）は [BUG-335](../bugs/BUG-335.md)。
- GuC・HuC は Meteor Lake の対応の時（D3）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws252-p001 | 調べと設計（Gen12・Gen11 の GT の差、engine・firmware・WA、実機の有無、Phase の分割）、design-reviewer | cleared（q1265、版 3.1） |
| ws252-p002 | Gen12 の platform の記述の行、gen12-golden、Linux から期待を抜き出す script | in-progress のまま WS を閉じた（f8b65c839、host 試験・5330 PASS） |
| ws252-p003 | RKL・ADL-S・RPL-S の attach（D1-R=C）、DMC | in-progress のまま WS を閉じた（既定で attach、5330 PASS、実機は未確認） |
| ws252-p004 | ICL の GT の core と WA の list | in-progress のまま WS を閉じた（e68361995、Linux 照合・5330 PASS、実機は未確認） |
| ws252-p005 | ICL の compiler と state（Gen11 の EU、gen110） | in-progress のまま WS を閉じた（4e316feac、host 試験 PASS、実機は未確認） |
| ws252-p006 | ICL の video・PXP の除外・attach（D1-I=C）・DMC | in-progress のまま WS を閉じた（557c9de05、5330 PASS、DMC は q1601、実機は未確認） |
| ws252-p007 | 実機の通し（GT・Vulkan・blit・video、5330 の回帰） | 未実施のまま WS を閉じた（実機が無い。2026-10-27 ユーザー「実機確認だけが…」） |
| ws252-p008 | 規約の全文の見直し | cleared（2026-10-27 00:52 Q1） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws252/phase001`〜`phase008`、`plan/ws252/tests/`）は削除する。git の commit `fb1f6afee` の `plan/ws252/` にある。設計とレビューは [design.md](design.md)・[design-review-1.md](design-review-1.md)・[design-review-2.md](design-review-2.md)・[design-review-3.md](design-review-3.md)。Queue の履歴は [plan/history](../history/index.md)。
