<!-- awesome-plan project=zedbsd record=ws245-p001 -->

# ws245-p001: Gen12（ADL-P、Latitude 5330）の display の書き直しの設計

Status: in-progress（q991、P2、2026-10-16）
Parent: [WS245](../ws.md)
Approval: 2026-10-16 ユーザー「同時にサブエージェントで、i915のリライトをやります。WSがあると思います。」

## 範囲

ws.md の p001 の行のとおり: Intel の公開の PRM（Gen12・ADL-P）と hardware の振る舞いから、display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口（Gen9 と共有の部分を保つ）、データの表の引用の方法（Value(s) referenced:）、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もりを design.md に書く。design-reviewer を通す。**code は書かない。** 今の Linux 由来の code は「何をするか（外から見える振る舞い・register の列）」を知るためだけに読み、構造・名前・制御の流れを設計に持ち込まない。

## 結果

（Q1 が書く）
