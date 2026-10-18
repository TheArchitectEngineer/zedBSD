<!-- awesome-plan project=zedbsd record=ws244-p001 -->

# ws244-p001: 棚卸しと書き直しの方針

Status: in-progress（2026-10-15 P7 が中断、2026-10-18 P1 が再開し棚卸しと方針を書いた。Q1 の判定待ち）
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

## 2026-10-18 P1（q1250）
- [inventory.md](../inventory.md) を最新の tree（WS245 の xd-*、WS258 の gen9-* の後）で書き直した。対象は 4 群: intel/ の上流の抜粋 23 file（D+N）、GT の core（C）、compiler・render（Mesa の手順の言及）、display の残り（O）。
- 判断が要る点 2 つ（inventory.md の末尾）: (1) PRM に載る command・opcode の名前を使ってよいか、(2) license の package を外すか。

### 書き直しの方針
- 値（register の offset・bit・表・WA の番号）は残し、block ごとに複数行の comment で `Value(s) referenced: <版> <path> (<名前>)`。参考にした手順は `Procedure referenced:`。file の先頭には出典・派生の宣言・SHA-256 を書かない。データだけになった file から外部の MIT の表示を外す。
- 名前は zedBSD の名前（WS245 の `I915_XD_*` に合わせ、GT は `I915_GT_*`、command は判断 1 次第）。改名は意味の単位で、旧名の互換の alias を残さない。
- 写した code（GT の core）は PRM（Gen9・Gen12 の Vol. 2・Vol. 7 など）と hardware の振る舞いから、関数の分け方・名前・comment を自前で書く。Linux の行ごとの言い換えをしない。**動作は変えない**: register に書く値と順序を保つ。確かめの道は、書き直しの前後で同じ host 試験の register の書き込みの列（mmio-record）を比べる事。
- comment の中の開発の管理情報・試験の進め方の話は、触った file から消す。

### 順序と Phase
依存の少ない header から、使う側へ。p002 → p003 → p004〜p006 → p007 → p008 → p009。各 Phase は 1 つの群で、群ごとに commit と merge の依頼。

### 試験の道（各 Phase 共通）
- build 3 組の warning 0: `BUILD=build/ws244-<p> vmunix`（既定）、`I915_TESTS=y`、Gen9 の config（Phase ごとに config.mk を tests/ に置く）。
- host 試験: 触った file を使う i915 の host 試験（tests/ の host-*-test、eu-test など）と、GT の書き直しは mmio-record の列の前後比較。
- 実機（5330 Gen12・SV7/7th・8th gen Gen9 の起動・desktop・render）は WS の最後に T1 へ 1 回の依頼（Q1 の一覧）。i915 の「全部を実装してから試験、だめなら二分探索」の規則に従い、main への merge は Q1 の判断（名前と comment だけの Phase は build と host 試験で merge 可、GT の code の書き直しは実機の後）。

### 危険と見積もり
- p003・p004〜p006 は 70 file が使う gt-regs.h の改名と GT の流れの書き直し。退行の危険は GT の init・reset・RC6。mmio-record の比較で値と順序の不変を見る。
- 見積もり: p002 2 LW、p003 4 LW、p004 4 LW、p005 4 LW、p006 5 LW、p007 4 LW（判断 1 が「改名」なら +4）、p008 1 LW、p009 2 LW。

## 残り（次の世代の再開点）
- Q1 の判定と判断 1・2 の回答を待つ。旧版の残り（旧 display の関数の手の確認、vbt-defs.h など）は WS245 で対象の file が無くなったので不要。
- 判定が出たら p001 を cleared にし、p002 から群ごとに書き直す。
