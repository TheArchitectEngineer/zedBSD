<!-- awesome-plan project=zedbsd record=ws244-p001 -->

# ws244-p001: 棚卸しと書き直しの方針

Status: in-progress（2026-10-16 P7、使用量の残りが少ないための通常のラップアップで中断）
Parent: [WS244](../ws.md)

## 範囲
i915 の tree（src/drivers/gpu/i915/）と userland/base/licenses/i915-driver/ の、Linux・Mesa から写した code・comment・構造の棚卸しと、書き直しの方針・順序・Phase の分け方・試験・見積もりを決める。code は変えない。

## 済んだこと
- 大きさ: i915 の tree は約 29.4 万行（tests を含む）。display は約 10 万行。
- 棚卸しの script: [tests/inventory.py](../tests/inventory.py)（tree を読むだけ。file ごとの要約と、`--functions` で関数ごとの C/S の行）。
- [inventory.md](../inventory.md) に、file の群の見立てと、生成した file ごとの要約を書いた（途中）。
- 分かったこと:
  - Gen12（5330）の display は、Linux の型・関数の分け方・comment を持つ「Linux 環境」（modeset-internal.h・internal.h・dp-internal.h・hotplug-internal.h・takeover-internal.h）の上で、Linux の text を書き換えた物。ddi.c（107/111 関数が C）・dp.c（92/92）・pipe.c（74/74）・plane.c（43/44）・panel-backlight.c（37/40）など。これは関数単位の言い換えでは規則を満たせず、設計からの書き直しが要る。
  - Gen9 の display（gen9-*.c）は「code は自前、順序と表は Linux から」と宣言。順序・表を PRM か引用元つきのデータにすれば残せる見込み。
  - compiler（eu.c・compile.c）は自前の配置の logic に Mesa の規約を引用する形。encode の field は intel/eu-encoding-*.h のデータ。
  - GT の gt-power.c・workarounds.c・verify-workarounds.c は「Follows Linux X()」の関数が多い。

## 残り（次の世代の再開点）
1. inventory: derived の file の「O」の関数を手で確かめる（file の宣言から派生と見るべき物が多い）。データの block（define・表）ごとの出典の表を作る。intel/ の struct の配置（vbt-defs.h の VBT の block、ref.h・connector.h・wm.h）が「構造」に当たるかを決める。
2. design.md: 書き直しの方針（PRM Gen9・Gen12 と hardware の振る舞いから設計、外向きの口は保つ、データは独自の名前で block ごとに引用元）、順序（edid → vbt → aux・gmbus → backlight・panel → power・dmc → clock・PLL → watermark → pipe・plane・ddi・dp → takeover・hotplug → GT → license の整理）、Phase の分け方（p002〜）、各 Phase の試験（host 試験、warning 0、実機: 5330 Gen12・SV7・7th/8th gen Gen9 の起動・mode set・hotplug・backlight）。
3. 危険と見積もり（LW）: Gen12 の display の約 6 万行が書き直しの本体。動作の退行（5330・Gen9 は実機で動作中）。
4. WIP commit して Q1 に SHA を送る（Q1 が design-reviewer を回す）。
