<!-- awesome-plan project=zedbsd record=ws245-p001 -->

# ws245-p001: Gen12 系（display 12・13）の display の書き直しの設計

Status: in-progress（q1200 P1: design.md の改訂 3a は P1 の branch（efbd30ff5 の後）、3 回のレビューを全部反映。残りは §7 の判断のユーザーへの確認と Q1 の clearance）
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
- ユーザーの決定 1〜4 を反映: 口 `xd.h` と `struct i915_xd_ops`、旧 code は `old-xd.c`（薄い adapter）、boot の語 `i915.display=xd|old`、p012 で旧を削除。display 12 の分岐は持たない（5320 は bootfb）〔2026-10-15 訂正: 誤り。display 12 は実機で動作確認済みで、xd に含める。設計は次の改訂〕。手順の第一の出典は Linux v6.8.12、PRM は補助、§5.4 を guardrail の形に。食い違いは Linux を優先。
- design-review-1.md の指摘: 直した 36・直さない 2（m1 PRM の再確認、m14 要約に細部が無い）。Q1 が §8 の表で数を確かめた。
- Phase p002〜p013、合計 51 LW（±40%）。module 25 → 19。
- §7 の残りの判断 #0〜#10（p009 の既定、HDMI は DVI mode、CDCLK を mode set で変えない、PSR off、storm 検出無し、OpRegion runtime 無し、VBT/OpRegion の layout の出典、5330 の VBT/OpRegion の bytes を fixture に置くか、旧 code の残し方、eDP 1.4 の rate 表、ktest-display の置き換え）。#7 は design-review.md の M13（5330 の VBT の bytes は 2026-10-14 にユーザーの判断で消した）と食い違うので、レビュー 2 の反映で扱う。
- 残り: design-review.md の反映（未着手）。→ 改訂 3 で済み（下）。

## 改訂 3・3a（2026-10-15、P1、q1200）

- 改訂 3（d87fe9599、main 260834101、875 行）: 対象の機種の方針（ws.md「対象の機種」）で本文を多機種に書き直した。表 P1（platform: TGL・RKL・ADL-S/RPL-S・ADL-P 系・ICL）と表 P2（platform ごとの手順の差）、`struct i915_xd_platform` の記述（§3.1）、Type-C の流れ A（ADL-P）と流れ B（TGL・ICL）、display 11 の Phase p014、Phase p002〜p015。design-review.md（レビュー 2、blocker 4・major 18・minor 16）の 38 件を §9 の表で扱った（直した 38、直さない 0。うち 9 件は改訂 2 で直っていた物の確認）。
- 3 回目のレビュー（design-reviewer、[design-review-3.md](../design-review-3.md)）: blocker 2・major 17・minor 16。全件を Linux v6.8.12 の source と今の code で確かめて改訂 3a（efbd30ff5、941 行）で直した（§10 の表、直した 35・直さない 0）。主な変更: 1 → 2 画面は live の遷移をやめ resident の relight（B1）、own の `output.c` の port の規則の置き換えを p010 に（B2）、流れ A の電源と順序（M1）、well の所属の表 W3（M2）、DMC の載せる間の well と MMIO の範囲（M3）、latency の 3 段の調整（M4）、crawl の条件（M5）、記述を platform × subplatform の 10 行に（M6）、V2 の TGL/ICL（M7）、run の契約に EAGAIN・acquired・RELIGHT（M8）、PCI ID は display だけの種類（M9）、Phase の依存（M10）、p009 の NONE（M11）、既定の切り替えは verified の行だけ（M12）、DPLL_SSC を消す（M13）、scenario の書き直し +4 LW（M14）、Gen9 の実機（M15）、p014 の抜け（M16）。
- Phase p002〜p015、合計 80 LW（±40 %。1 LW ≈ 15〜25 分）。実機の往復は別枠 6〜10 LW。
- §7 の判断 #0〜#22 をユーザーに聞く（ws.md の Resume point）。主な既定: #0 既定の切り替えは p011、#11 PRM は照合だけ、#13 p003 以降の merge はデモの後、#15 passthrough の scenario 7 つを xd に書き直して残す、#17 TGL の TC を p010 に、#18 RKL・ADL-S は今 i915 が付かない（聞く）、#20 GT は起こさない、#21 未確認の platform は opt-in、#22 Gen9 の機械（聞く）。
- 未実施: build・host 試験・QEMU・実機（設計の Phase なので対象外）。レビューの「確かめられなかった事」（pipe DMC の well、5320 の HDMI の port、試験の kernel の余裕）は §10 の末尾。
