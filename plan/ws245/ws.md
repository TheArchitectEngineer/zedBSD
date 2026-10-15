<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12（ADL-P、Latitude 5330）の display の書き直し

<!-- awesome-plan-current:start -->
Status: planning（2026-10-15 作成。p001 の [design.md](design.md) は main に入った（6bccaef57）、敵対的レビューは未完）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理、棚卸し plan/ws244/inventory.md・tests/inventory.py）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: p001 の design.md をレビュー（design-review-1.md と design-review.md）とユーザーの決定 1〜4 で改訂 → 再レビュー → 残りの判断をユーザーに。Phase の表は design.md の 6.2 節（p002〜p012、36 LW）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Gen12（5330）のディスプレイは書き直します。WSを立ててください。」
（前提の決定: 「code・コメント・構造:については、MITであっても写してはいけません。」「範囲を全部の WS に広げます。」「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」）

## 対象（WS244 の棚卸しの「Gen12 の display の Linux 環境」の群、約 6 万行）

`src/drivers/gpu/i915/display/` の ddi・dp・dp-sink・dp-internal.h・pipe・plane・clock・watermark・vbt・vbt.h・panel・panel-backlight・power・dmc・takeover・takeover-internal.h・hotplug・hotplug-internal.h・modeset・modeset-internal.h・internal.h・opregion・opregion-internal.h・aux・gmbus・hdmi・hdmi-mode・edid・edid-read・color・vblank・phy・state・diagnostics・dp-ext・dp-ext-kern・tc・dkl-phy。どれも「Derived from the Linux kernel v6.8.12」と Linux の型・関数の分け方・comment を持つ（ddi.c 107/111、dp.c 92/92、pipe.c 74/74、plane.c 43/44 の関数が写し）。intel/ の header のうち Gen12 だけが使う struct の配置（vbt-defs.h・ref.h・connector.h・wm.h）も含める。

## ユーザーの決定（2026-10-15、design.md のレビューの後）

「5は、(1) a、(2) 5330のみ、(3) Linuxのコードを参考にする、(4) Linuxを優先、でおねがいします。」
1. 逃げ道の形は (a): 口（xd.h）を p002 で決め、own の file は最初からその口だけを呼ぶ。口の実装を旧（薄い adapter、p011 で削除）と xd で丸ごと切り替える（状態を混ぜない）。
2. 対象は 5330（ADL-P）だけ。Tiger Lake（5320 など display 12）は支えない。
3. 手順は Linux の code を参考にする（Tiger Lake・Alchemist の PRM は補助）。guardrail のまま、code・comment・構造は写さず、値は block ごとに Value(s) referenced:、参考にした手順は Procedure referenced: <版> <path> (<関数>) を付ける。
4. PRM と Linux（5330 で動いている実績）の値・手順が食い違う時は Linux を優先する。

## 目的と完了の条件

- Gen12 の display を、Intel の公開の PRM（Gen12、ADL-P）と hardware の振る舞いから設計し直して書く。Linux の code・comment・構造（関数の分け方、型、制御の流れ、名前）を写さず、行ごと・関数ごとの言い換えもしない。
- register の offset・bit・表（DDI の buf trans、PLL・clock の表など）はデータとして独自の名前で残し、block ごとに「Value(s) referenced: <版> <path> (<名前>)」を付ける。file の上部には出典を書かない。
- 外向きの口（display の API、Gen9 と共有の部分）は保つ。
- 書き直しの後、`Derived from the Linux kernel` と Linux の MIT の表示は Gen12 の file から消え、`userland/base/licenses/i915-driver/` は WS244 の最後で整理する。
- 完了: 5330 の実機で、起動・GOP の引き継ぎ・内蔵 panel（eDP）・backlight・外部の DP/HDMI（USB-C を含む）・hotplug・mode set・suspend の戻りが今と同じに動く（ユーザーの確かめ、試験の一覧に載せる）。

## Phase（案、p001 で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: PRM からの display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口、データの表の引用の方法、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もり。design-reviewer を通す | planned |
| p002〜 | 群ごとの書き直し（p001 で分割。WS244 の案の順: edid → vbt → aux・gmbus → backlight・panel → power・dmc → clock・PLL → watermark → pipe・plane・ddi・dp → takeover・hotplug） | planned |
| 最後 | 規約の全文の見直し、出典の監査（file の上部に出典が無く、block ごとに付いていること） | planned |
