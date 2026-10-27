<!-- awesome-plan project=zedbsd record=ws303 -->

# WS303 熱と fan（温度の読み・fan の回転数・thermal mode）

- Master: [plan/master.md](../master.md)（2026-10-27 09:39 作成）。Primary Milestone: MG004。関係: [WS301](../ws301/ws.md)（DVFS、熱の上限の口は WS301 p002）、WS039 p012（amdrdna の SMU の fan）。
- Status: planning（2026-10-27 09:39 新設）
- 出典: 2026-10-27 ユーザー「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいですとお伝えください。ファン制御もあるといいですね。」、（クリック）fan は WS301 とは別の WS。

## 単一の到達目標

5330 を最初の対象に、温度の読み、fan の回転数の読み、（できる機種で）fan の制御か thermal mode の選択を、/dev/system の口で提供する。5330 は EC が fan を回すので、OS からは回転数の読みと Dell の thermal mode の選択になる見込み（[pm-plan.md](../agents/P1/pm-plan.md)）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調査と設計（ACPI の _FAN・_TZ、EC、Dell の SMM／WMI、UAPI） | WS301 p002 | planned |
