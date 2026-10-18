<!-- awesome-plan project=zedbsd record=ws244 -->

# WS244: i915 の Linux 由来の code の書き直し（データだけを引用で残す）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成、p001 を P7 が途中まで実施して中断。2026-10-18 ユーザーの指示で P1 が再開、q1250）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: p001 の棚卸しは途中（plan/ws244/inventory.md、P7 5a352c47c）。**2026-10-15 ユーザーの分割**: Gen12（5330、後に「5330を特別扱いはしません」で display 12・13 の全機種）の display は書き直し → [WS245](../ws245/ws.md) に移した。この WS の残りは (1) **Gen9 の display（display/gen9-*.c）を guardrail に沿って直す**（file の先頭の「code は自前、順序と表は Linux」などの出典の宣言を消し、表・順序の値は block ごとに「Value(s) referenced: …」、参考にした手順は「Procedure referenced: …」を付ける。code が写しなら書き直す）、(2) GT（gt-power.c・workarounds.c・verify-workarounds.c の「Follows Linux …」の関数）、(3) compiler・render（Mesa の手順の言及）、(4) intel/ の header（データは block ごとの引用に、file の先頭の MIT の表示と出典を消す）、(5) display の自前の file の言及の確認、(6) 最後に license の package（userland/base/licenses/i915-driver/）の整理。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

「code・コメント・構造:については、MITであっても写してはいけません。もしかして写した箇所があるんですか？」
「範囲を全部の WS に広げます。」
「i915はWSを立てて、すぐに対処してください。」
「Gen12（5330）のディスプレイは書き直します。WSを立ててください。Gen9 のディスプレイはガードレールにそって修正してください。」（→ Gen12 は WS245）
「ガードレイルに、ファイル上部には参考にしたことや引用したことを一切書かず、該当箇所にブロック単位で記載することを明記してください。」

## 背景

`src/drivers/gpu/i915/display/` のほぼ全部と `src/drivers/gpu/i915/intel/` の header・表（約 50 file）は、先頭に「Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/…)」と Intel などの MIT の表示を持ち、`userland/base/licenses/i915-driver/LICENSE` も派生の code を含むと書く。新しい規則（code・comment・構造は MIT でも写さない、定数・表はデータとして block ごとに引用元を示して使う）に合わない。

## 目的と完了の条件

- i915 の tree（display・intel・その他の i915 の file）に、Linux・Mesa から写した code・comment・構造が残らない。register の offset・bit・表（DDI の buf trans、PLL の表、forcewake の範囲など）は、データとして block ごとに引用元（Linux の版・file・表の名前）を示して残す。
- 外部の MIT の著作権表示は、データだけになった file から外す。`userland/base/licenses/i915-driver/` は内容に合わせて直すか無くす。
- 動作は変えない: 今動いている display 9・12・13 の全機種（例 5330・SV7・7th/8th gen。2026-10-15 ユーザー「Skylake～Raptor Lake Refreshまですべてサポートします。」）の display が同じに動く（実機の確かめはユーザー、試験の一覧に載せる）。
- 書き直しの手順は p001 で決め、design-reviewer を通す。書き直しは Intel の公開の PRM（Programmer's Reference Manual）と hardware の振る舞いから書き、Linux の code を行ごとに言い換えない。

## Phase（案、p001 で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 棚卸し（file・関数ごとに「データ／写した code／自前」を分類、Mesa 由来の i915 compiler の encode なども含む）と書き直しの方針・順序・試験の道 | in-progress（中断、[phase001](phase001/phase.md) の「残り」） |
| p002〜 | file の群ごとの書き直し（p001 で分割） | planned |
| 最後 | license の表示と package の整理、規約の全文の見直し | planned |
