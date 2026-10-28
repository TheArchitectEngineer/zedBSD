<!-- awesome-plan project=zedbsd record=ws303 -->

# WS303 熱と fan（温度の読み・fan の回転数・thermal mode）

- Master: [plan/master.md](../master.md)（2026-10-27 09:39 作成）。Primary Milestone: MG004。関係: [WS301](../ws301/ws.md)（DVFS、熱の上限の口は WS301 p002）、WS039 p012（amdrdna の SMU の fan）。
- Status: planning（2026-10-27 09:39 新設）
- 出典: 2026-10-27 ユーザー「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいですとお伝えください。ファン制御もあるといいですね。」、（クリック）fan は WS301 とは別の WS。

## 単一の到達目標

5330 を最初の対象に、温度の読み、fan の回転数の読み、（できる機種で）fan の制御か thermal mode の選択を、/dev/system の口で提供する。5330 は EC が fan を回すので、OS からは回転数の読みと Dell の thermal mode の選択になる見込み（[pm-plan.md](../agents/P1/pm-plan.md)）。

## Phase

設計: [design.md](design.md)（第 2 版、2026-10-29 P5、q1666）、レビュー [design-review.md](design-review.md)。Phase の表は design.md の「第 2 版の Phase の表」が本体（下は写し）。
WS301 p002 への依存は、passive cooling を入れる時（Q303-6 の B）だけに要る（推しの A では外れる、ユーザーの判断待ち）。

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調査と設計（ACPI の fan・DPTF、EC、Dell の WMI、UAPI）、design-reviewer | — | in-progress（設計の第 2 版まで。ユーザーの判断 Q303-1〜8 待ち） |
| p002 | ACPI WMI の層と host の harness の拡張（6 LW） | p001、harness の置き場所（Q1） | planning |
| p003 | registry・UAPI・`thermal` の道具（5 LW） | p001（Q303-1） | planning |
| p004 | Dell の provider と 5330 の試験（6 LW） | p002、p003、Q303-7・8 | planning |
| p005 | Pi 5 の fan を registry に（1 LW） | p003 | planning |
| p006 | 規約の全文と文書（2 LW） | p004、p005 | planning |

## 再開点（2026-10-29 P5、q1666-i02）

- 設計の第 2 版まで。ユーザーの判断 Q303-1〜8（design.md §8 と第 2 版の改訂の問い）を Q1 経由で待つ。答えが来たら design.md に原文と解釈を書き、Phase の表を確定する。
- 5330 の Debian の読みの記録（2026-10-29 02:42、読みだけ）は scratchpad にあり git に入れていない。要る値は design.md §2.3 に写した。
