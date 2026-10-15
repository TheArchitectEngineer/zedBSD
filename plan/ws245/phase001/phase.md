<!-- awesome-plan project=zedbsd record=ws245-p001 -->

# ws245-p001: Gen12（ADL-P、Latitude 5330）の display の書き直しの設計

Status: in-progress（q991、design.md の改訂 2 は main 280c9fdba、design-review.md の反映が残る）
Parent: [WS245](../ws.md)
Approval: 2026-10-15 ユーザー「同時にサブエージェントで、i915のリライトをやります。WSがあると思います。」

## 範囲

ws.md の p001 の行のとおり: Intel の公開の PRM（Gen12・ADL-P）と hardware の振る舞いから、display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口（Gen9 と共有の部分を保つ）、データの表の引用の方法（Value(s) referenced:）、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もりを design.md に書く。design-reviewer を通す。**code は書かない。** 今の Linux 由来の code は「何をするか（外から見える振る舞い・register の列）」を知るためだけに読み、構造・名前・制御の流れを設計に持ち込まない。

## 結果

- **q991 P2（提出 6bccaef57、Q1 が commit の実在と行数を確かめて merge）**: [design.md](../design.md) 522 行。接頭辞 `xd`、新旧を並べ p008 で切り替え（`i915.display=old` を逃げ道）、p011 で旧を削除。Phase p002〜p012、36 LW（±40%）。PRM は ADL-P の版が公開されておらず、TGL・ACM の PRM と 5330 の dump から。
- design-reviewer は担当が起動したが結果が届く前に終わった（design.md 第 8 節）。レビューは未完。
- 記録の訂正（2026-10-15）: Q1 がこの後に「p001 cleared・D1〜D6・p002〜p005 cleared」と書いた記録は、実在しない commit に基づく誤りだったので取り消した（485ad5be6、ユーザー「誤りとして補正して、先に進めましょう」）。

## 改訂 2（2026-10-15、P1、ユーザーの指示で停止・通常のラップアップ）

- 提出 7cdf5879d（Q1 が実在と差分を確かめて merge、280c9fdba。基点が日付の修正より前だったので design.md の日付の 4 行を 2026-10-15 に直して衝突を解いた）。design.md 678 行（改訂前 522）。
- ユーザーの決定 1〜4 を反映: 口 `xd.h` と `struct i915_xd_ops`、旧 code は `old-xd.c`（薄い adapter）、boot の語 `i915.display=xd|old`、p012 で旧を削除。display 12 の分岐は持たない（5320 は bootfb）。手順の第一の出典は Linux v6.8.12、PRM は補助、§5.4 を guardrail の形に。食い違いは Linux を優先。
- design-review-1.md の指摘: 直した 36・直さない 2（m1 PRM の再確認、m14 要約に細部が無い）。Q1 が §8 の表で数を確かめた。
- Phase p002〜p013、合計 51 LW（±40%）。module 25 → 19。
- §7 の残りの判断 #0〜#10（p009 の既定、HDMI は DVI mode、CDCLK を mode set で変えない、PSR off、storm 検出無し、OpRegion runtime 無し、VBT/OpRegion の layout の出典、5330 の VBT/OpRegion の bytes を fixture に置くか、旧 code の残し方、eDP 1.4 の rate 表、ktest-display の置き換え）。#7 は design-review.md の M13（5330 の VBT の bytes は 2026-10-14 にユーザーの判断で消した）と食い違うので、レビュー 2 の反映で扱う。
- 残り: design-review.md の反映（未着手）。
