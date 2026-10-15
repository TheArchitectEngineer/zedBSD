<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12 系（display 12・13）の display の書き直し

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。p001 の [design.md](design.md) は改訂 2（P1、7cdf5879d、678 行、main 280c9fdba）。レビューは 2 回（[design-review-1.md](design-review-1.md) は反映済み、[design-review.md](design-review.md) は未反映））
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理、棚卸し plan/ws244/inventory.md・tests/inventory.py）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: q1200（P1、2026-10-15 夕）で改訂 3 中。design-review.md（2 回目のレビュー、blocker 4・major 18・minor 16）の指摘を design.md の §8 の形で反映 → 必要ならもう一度レビュー → §7 の判断 #0〜#10 をユーザーに → p002 から実装（Queue の承認が要る）。Phase の表は design.md の 6.2 節（p002〜p013、51 LW ±40%）に display 12 の差と display 11 の Phase を足す（次の改訂）。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「Gen12（5330）のディスプレイは書き直します。WSを立ててください。」
（前提の決定: 「code・コメント・構造:については、MITであっても写してはいけません。」「範囲を全部の WS に広げます。」「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」）

## 対象（WS244 の棚卸しの「Gen12 の display の Linux 環境」の群、約 6 万行）

`src/drivers/gpu/i915/display/` の ddi・dp・dp-sink・dp-internal.h・pipe・plane・clock・watermark・vbt・vbt.h・panel・panel-backlight・power・dmc・takeover・takeover-internal.h・hotplug・hotplug-internal.h・modeset・modeset-internal.h・internal.h・opregion・opregion-internal.h・aux・gmbus・hdmi・hdmi-mode・edid・edid-read・color・vblank・phy・state・diagnostics・dp-ext・dp-ext-kern・tc・dkl-phy。どれも「Derived from the Linux kernel v6.8.12」と Linux の型・関数の分け方・comment を持つ（ddi.c 107/111、dp.c 92/92、pipe.c 74/74、plane.c 43/44 の関数が写し）。intel/ の header のうち Gen12 だけが使う struct の配置（vbt-defs.h・ref.h・connector.h・wm.h）も含める。

## ユーザーの決定（2026-10-15、design.md のレビューの後）

「5は、(1) a、(2) 5330のみ、(3) Linuxのコードを参考にする、(4) Linuxを優先、でおねがいします。」
1. 逃げ道の形は (a): 口（xd.h）を最初の方の Phase で決め（design.md 改訂 2 では p003）、own の file は最初からその口だけを呼ぶ。口の実装を旧（薄い adapter、旧 code を消す Phase で削除。design.md 改訂 2 では p012）と xd で丸ごと切り替える（状態を混ぜない）。
2. 「5330のみ」（意味は未確認。下の対象の方針が優先）。Q1 は当初「Tiger Lake（display 12）は支えない」と記録したが誤り（2026-10-15 ユーザーの補足「Tiger Lakeは実機で動作確認済みです。display 10,11以外は動作確認済みです。」「記録が食い違っているだけです。」）。display 9・12・13 は実機で動いている機能で落とさない。xd は display 12（Tiger Lake、5320）と display 13（ADL-P、5330）の両方を持つ。display 10・11 は今も支えていない（範囲外）。
3. 手順は Linux の code を参考にする（Tiger Lake・Alchemist の PRM は補助）。guardrail のまま、code・comment・構造は写さず、値は block ごとに Value(s) referenced:、参考にした手順は Procedure referenced: <版> <path> (<関数>) を付ける。
4. PRM と Linux（5330 で動いている実績）の値・手順が食い違う時は Linux を優先する。

## 対象の機種（2026-10-15 ユーザー「すでに多数の機種で動作しており、5330を特別扱いはしません。Skylake～Raptor Lake Refreshまですべてサポートします。」）

- WS245 は 5330 を特別扱いしない。対象は今の Gen12 の code が動かしている全機種: display 12（Tiger Lake・Rocket Lake・Alder Lake-S・Raptor Lake-S/HX と Raptor Lake Refresh のデスクトップ）と display 13（Alder Lake-P/U・Raptor Lake-P/U）。display 9（Skylake〜Comet Lake）は gen9-*.c（WS244）。
- display 9・12・13 は実機で動作確認済み（ユーザー「display 10,11以外は動作確認済みです。」）。display 11（Ice Lake）と display 10 は今の code で支えていない。Ice Lake（display 11）は、この WS の最後の方の Phase で新しく支える（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」）。display 10 は範囲外。
- 下の「目的と完了の条件」の「5330」の記述（2026-10-15 の作成時）は、この方針で読み替える。実機の確かめは 5330 に限らない。

## 目的と完了の条件

- Gen12 の display を、Intel の公開の PRM（Gen12、ADL-P）と hardware の振る舞いから設計し直して書く。Linux の code・comment・構造（関数の分け方、型、制御の流れ、名前）を写さず、行ごと・関数ごとの言い換えもしない。
- register の offset・bit・表（DDI の buf trans、PLL・clock の表など）はデータとして独自の名前で残し、block ごとに「Value(s) referenced: <版> <path> (<名前>)」を付ける。file の上部には出典を書かない。
- 外向きの口（display の API、Gen9 と共有の部分）は保つ。
- 書き直しの後、`Derived from the Linux kernel` と Linux の MIT の表示は Gen12 の file から消え、`userland/base/licenses/i915-driver/` は WS244 の最後で整理する。
- 完了（対象の機種の方針で読み替え: 5330 だけでなく display 12・13 の対象機種で）: 5330 の実機で、起動・GOP の引き継ぎ・内蔵 panel（eDP）・backlight・外部の DP/HDMI（USB-C を含む）・hotplug・mode set・suspend の戻りが今と同じに動く（ユーザーの確かめ、試験の一覧に載せる）。

## Phase（案、p001 で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: PRM からの display の model（pipe・transcoder・DDI・PHY（combo・Dekel/TC）・PLL・plane・watermark・power well・DMC・AUX・GMBUS・VBT・opregion）、外向きの口、データの表の引用の方法、書き直しの順序と Phase の分割、試験の道（host と 5330）、LW の見積もり。design-reviewer を通す | in-progress（design.md 改訂 2、レビュー 2 の反映が残る） |
| p002〜 | 群ごとの書き直し（p001 で分割。WS244 の案の順: edid → vbt → aux・gmbus → backlight・panel → power・dmc → clock・PLL → watermark → pipe・plane・ddi・dp → takeover・hotplug） | planned |
| display 11 | Ice Lake（display 11）を xd で支える（今の code は支えておらず、実機の確かめも無い）。xd の display 12・13 の後、規約の全文の見直しの前に置く。番号は design.md の Phase の表の改訂で決める（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」） | planned |
| 最後 | 規約の全文の見直し、出典の監査（file の上部に出典が無く、block ごとに付いていること） | planned |
