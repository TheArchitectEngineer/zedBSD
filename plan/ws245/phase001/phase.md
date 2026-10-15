<!-- awesome-plan project=zedbsd record=ws245-p001 -->

# ws245-p001: Gen12（ADL-P、Latitude 5330）の display の書き直しの設計

Status: in-progress（q991、design.md は main 4625f0a3e、レビュー待ち）
Parent: [WS245](../ws.md)
Approval: 2026-10-15 ユーザー「同時にサブエージェントで、i915のリライトをやります。WSがあると思います。」

## 範囲

ws.md の p001 の行のとおり: Intel の公開の PRM（Gen12・ADL-P）と hardware の振る舞いから、display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口（Gen9 と共有の部分を保つ）、データの表の引用の方法（Value(s) referenced:）、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もりを design.md に書く。design-reviewer を通す。**code は書かない。** 今の Linux 由来の code は「何をするか（外から見える振る舞い・register の列）」を知るためだけに読み、構造・名前・制御の流れを設計に持ち込まない。

## 結果

- **q991 P2（提出 6bccaef57、Q1 が commit の実在と行数を確かめて merge）**: [design.md](../design.md) 522 行。接頭辞 `xd`、新旧を並べ p008 で切り替え（`i915.display=old` を逃げ道）、p011 で旧を削除。Phase p002〜p012、36 LW（±40%）。PRM は ADL-P の版が公開されておらず、TGL・ACM の PRM と 5330 の dump から。
- design-reviewer は担当が起動したが結果が届く前に終わった（design.md 第 8 節）。レビューは未完。
- 記録の訂正（2026-10-15）: Q1 がこの後に「p001 cleared・D1〜D6・p002〜p005 cleared」と書いた記録は、実在しない commit に基づく誤りだったので取り消した（485ad5be6、ユーザー「誤りとして補正して、先に進めましょう」）。
