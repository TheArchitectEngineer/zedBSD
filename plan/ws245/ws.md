<!-- awesome-plan project=zedbsd record=ws245 -->

# WS245: i915 の Gen12 系（display 12・13）の display の書き直し

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成。p001 の [design.md](design.md) は改訂 3a（P1、q1200、941 行）。レビューは 3 回（[design-review-1.md](design-review-1.md)・[design-review.md](design-review.md)・[design-review-3.md](design-review-3.md)、全部反映済み: design.md §8・§9・§10））
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [WS244](../ws244/ws.md)（i915 の Linux 由来の code の整理、棚卸し plan/ws244/inventory.md・tests/inventory.py）、[Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: q1204（P1、2026-10-15 夕）で p002 を実装中。以前: 2026-10-15 P1（q1200）: design.md 改訂 3a（多機種: display 12 の TGL・RKL・ADL-S/RPL-S と display 13 の ADL-P 系、display 11 は p014。Phase p002〜p015、80 LW ±40%、実機の往復は別枠 6〜10 LW。3 回目のレビュー blocker 2・major 17・minor 16 を全部反映）。次: §7 の判断 #0〜#22（特に #11 PRM の Notices、#13 merge の時期、#15 passthrough の scenario、#16 他の WS の Phase、#17 TGL の TC、#18 RKL・ADL-S の今の状態、#20 GT、#22 Gen9 の機械）をユーザーに → p001 の clearance（Q1）→ p002 から実装（Queue の承認が要る。p003 以降の merge はデモの後: #13）。
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

## 実装の開始とロールバックの体制（2026-10-15 夕）

ユーザー:「WS245は、ロールバックできる体制にしておいて、今から実装します。」
- ロールバックの基点: main 0c25c87a7（WS245 の実装の commit が入る前）。Phase ごとに別の merge commit にするので、`git revert -m 1 <merge>` で Phase 単位に戻せる。
- kernel の振る舞いは p009 まで変えない: p002・p004〜p008 は新しい file と host 試験だけ。p003 の adapter 以降も `i915.display=old` が既定で、xd は opt-in（design.md §7 #0、既定の切り替えは p011）。`old` は p013 まで逃げ道。
- §7 の判断は既定の案で進め、既定の無い #18（RKL・ADL-S/RPL-S を支えるか）・#22（Gen9 の機械）は回答待ち。#18 の回答までは device.c の ID の表に RKL・ADL-S を足さない（今の振る舞いのまま）。
- merge の時期（§7 #13、2026-10-15 夕 ユーザー「WS245はいつでもロールバックできるので、今マージします」）: デモの後まで待たず、p003 以降も Phase の成果ができ次第 main に merge する（既定の案の「p003 以降はデモの後」を置き換え）。戻す時は Phase の merge commit を revert する。

## Phase（design.md §6.2、改訂 3a。p001 の clearance で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: Linux v6.8.12 の分岐（決定 3・4）と PRM（補助）から、display 11〜13 の model（platform の記述、pipe・transcoder・DDI・PHY（combo・Dekel・MG）・PLL・plane・watermark・DBUF・power well・DMC・AUX・GMBUS・VBT・opregion・Type-C の 2 つの流れ）、外向きの口と契約、データの表の引用の方法、書き直しの順序と Phase の分割、試験の道（host・T1 の 5330 passthrough・実機の一覧）、LW の見積もり。design-reviewer を 3 回通す | in-progress（design.md 改訂 3a。§7 の判断をユーザーに聞いてから clearance） |
| p002 | 土台: register と表の header（記述の 9 行）、xd-io、xd-platform（PCI ID に RKL・ADL-S/RPL-S を display だけの種類で）、host の試験の枠と own の register の model、own の型の分離、Gen9 の own への移設 | planned（6 LW） |
| p003 | 口 `xd.h` と `struct i915_xd_ops`、旧 code への adapter `old-xd.c`、own の file の付け替え、`i915.display=xd|old`（kern/boot.c、Q1 の承認。既定 old） | planned（7 LW。merge はデモの後: design.md §7 #13） |
| p004 | VBT（合成の fixture、4 platform の V2/V3/V4）・EDID | planned（3 LW） |
| p005 | AUX・GMBUS | planned（3 LW、p006 の後） |
| p006 | power（W1・W3、TGL の pcode の well、IRQ の hook、DC・DC9）・DMC（v2、載せる間の well）・clock（CDCLK の 4 表と crawl/relock、combo DPLL、TC PLL）・xd-tc の register の部分 | planned（8 LW） |
| p007 | DDI（combo PHY、swing、DDI_BUF_CTL、DP_TP）・DP（訓練）・panel（PPS、backlight） | planned（6 LW） |
| p008 | pipe・DBUF（2/4 slice、join、WM の level と調整、SAGV、relight の列）、init/uninit の全体の列 | planned（6 LW） |
| p009 | screen・takeover・display の段・diag、xd の opt-in（既定は old）。T1 の passthrough（lcdb）、5330・5320 の実機の一覧 | planned（9 LW） |
| p010 | hotplug・Type-C（流れ A/B）・外部 DP・HDMI・head（relight）、own の output.c の port の規則を platform の記述に。T1 の passthrough（hdmib・dual …）、5330・5320 の実機の一覧 | planned（10 LW） |
| p011 | suspend の戻り・log の文言・`i915.display` の既定を xd（verified の行だけ。5330・5320 の確かめの後） | planned（4 LW） |
| p012 | platform の網羅（記述の 9 行の host の列、RKL・ADL-S は「未確認」） | planned（3 LW） |
| p013 | 旧 code の削除（adapter・tc-kern.c・dc9.c・ktest・旧 host 試験）と出典の監査 | planned（4 LW） |
| p014 | display 11（Ice Lake）: MG PHY・MG PLL・transcoder EDP・ICL の well と TC の流れ・DMC v1。実機は無い（2026-10-15 ユーザー「display 11は、i915のリファクタリングの既存WSに最後の方のphaseとして入れてください。」） | planned（8 LW） |
| p015 | 規約の全文の見直しと p014 の出典の監査 | planned（3 LW） |
