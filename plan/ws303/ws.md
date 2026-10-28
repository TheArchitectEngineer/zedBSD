<!-- awesome-plan project=zedbsd record=ws303 -->

# WS303 熱と fan（温度の読み・fan の回転数・thermal mode）

- Master: [plan/master.md](../master.md)（2026-10-27 09:39 作成）。Primary Milestone: MG004。関係: [WS301](../ws301/ws.md)（DVFS、熱の上限の口は WS301 p002）、WS039 p012（amdrdna の SMU の fan）。
- Status: planning（2026-10-27 09:39 新設）
- 出典: 2026-10-27 ユーザー「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいですとお伝えください。ファン制御もあるといいですね。」、（クリック）fan は WS301 とは別の WS。

## 単一の到達目標

5330 を最初の対象に、温度の読み、fan の回転数の読み、DVFS の strategy（thermal profile）の選択、fan を上げる段の指定、非常の水準での停止を、`/dev/thermal` の口と `/sbin/thermal` で提供する（2026-10-29 ユーザーの回答 Q303-1・2・5 で改めた。旧: /dev/system の口）。5330 は EC が fan を回すので、OS からは回転数の読み、Dell の thermal mode への strategy の伝達、（確かめられれば）SMM の fan の state での上げになる。

## Phase

設計: [design.md](design.md)（第 3 版、2026-10-29 P5、q1666）、レビュー [design-review.md](design-review.md)（第 1 版）・design-v3-review.md（第 3 版）。Phase の表は design.md の「第 3 版の Phase の表」が本体（下は写し）。

## ユーザーの回答（2026-10-29、Q1 の中継、原文）

- Q303-1「/dev/thermal」、Q303-2「rootだけが変えられるが、sessiond経由でも変えられる。/sbin/thermalからDVFSストラテジを選択。強制的にファンを大きくする（何段階かあり）ことはできるが、実際の温度に対して必要なファン回転数を下回らせるオーバーライドは不能。」、Q303-3「さっき書いた/sbin/thermal」、Q303-4「profile を作る」、
  Q303-5「温度に対して必要な回転数を下回らせることはできず、回転を上げる。非常水準に達したとき、電源を落とす。コントロールできないときはファームウェアにまかせたり、ストラテジだけファームウェアに伝える。」、Q303-6「入れる」、Q303-7「Debian で読む (推し)」、Q303-8「よい (推し)」。
- 解釈（P5、design.md の「第 3 版の改訂」）: UAPI は新しい `/dev/thermal`。変更は root と sessiond 経由。`/sbin/thermal` が strategy を選ぶ。fan は上げる段だけ。非常の水準で停止。passive を入れるので WS301 に依存する。確かめの問い Q303-9〜14 を聞き直し中。
- Q1 の決定（2026-10-29）: AML の harness は p002 で `plan/tools/aml-host/` に移す。`/dev/acpi` は BUG-351。

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調査と設計、design-reviewer | — | in-progress（第 4 版。Q303-9〜15 待ち） |
| p002 | ACPI WMI の層、harness の移しと拡張（6 LW） | p001 | planning |
| p003 | registry・`/dev/thermal`・非常の停止（init の SIGUSR2・直の切り）・試験の注入・`/sbin/thermal`（8 LW） | p001 | planning |
| p004 | Dell の provider と 5330 の試験（7 LW） | p002、p003 | planning |
| p005 | sessiond の THERMAL の要求（2 LW） | p003 | planning |
| p006 | Pi 5 の provider（profile と boost、3 LW） | p003 | planning |
| p007 | WS301 の policy の受け手と passive（2 LW） | p003、WS301 p002・p003・p004 | planning |
| p008 | 規約の全文と文書（2 LW） | p004〜p007 | planning |

## 再開点（2026-10-29 P5、q1666-i02）

- 設計の第 4 版（ユーザーの回答 Q303-1〜8 と第 3 版の review を反映）。聞き直し Q303-9〜15（design.md の第 4 版の問いが最新）の答えを待つ。p003 は 8 LW、合計 約 30 LW（design.md の第 4 版の変更）。
- 5330 の Debian の読みの記録（2026-10-29 02:42、読みだけ）は scratchpad にあり git に入れていない。要る値は design.md §2.3 に写した。
