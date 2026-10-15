<!-- awesome-plan project=zedbsd record=ws244 -->

# WS244: i915 の Linux 由来の code の書き直し（データだけを引用で残す）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-16 作成、P7 が p001 から）
Primary Milestone: MG006
Related Milestones: MG002
Parent: [Master](../master.md)
Related: [Guardrail](../guardrail.md)「MIT・GPL の出典のデータの引用」
Resume point: p001（棚卸しと方針）から。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-16）

「code・コメント・構造:については、MITであっても写してはいけません。もしかして写した箇所があるんですか？」
「範囲を全部の WS に広げます。」
「i915はWSを立てて、すぐに対処してください。」

## 背景

`src/drivers/gpu/i915/display/` のほぼ全部と `src/drivers/gpu/i915/intel/` の header・表（約 50 file）は、先頭に「Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/…)」と Intel などの MIT の表示を持ち、`userland/base/licenses/i915-driver/LICENSE` も派生の code を含むと書く。新しい規則（code・comment・構造は MIT でも写さない、定数・表はデータとして block ごとに引用元を示して使う）に合わない。

## 目的と完了の条件

- i915 の tree（display・intel・その他の i915 の file）に、Linux・Mesa から写した code・comment・構造が残らない。register の offset・bit・表（DDI の buf trans、PLL の表、forcewake の範囲など）は、データとして block ごとに引用元（Linux の版・file・表の名前）を示して残す。
- 外部の MIT の著作権表示は、データだけになった file から外す。`userland/base/licenses/i915-driver/` は内容に合わせて直すか無くす。
- 動作は変えない: 今の 5330（Gen12）・SV7・7th/8th gen（Gen9）の display が同じに動く（実機の確かめはユーザー、試験の一覧に載せる）。
- 書き直しの手順は p001 で決め、design-reviewer を通す。書き直しは Intel の公開の PRM（Programmer's Reference Manual）と hardware の振る舞いから書き、Linux の code を行ごとに言い換えない。

## Phase（案、p001 で確定）

| ID | 内容 | 状態 |
| --- | --- | --- |
| p001 | 棚卸し（file・関数ごとに「データ／写した code／自前」を分類、Mesa 由来の i915 compiler の encode なども含む）と書き直しの方針・順序・試験の道 | planned |
| p002〜 | file の群ごとの書き直し（p001 で分割） | planned |
| 最後 | license の表示と package の整理、規約の全文の見直し | planned |
