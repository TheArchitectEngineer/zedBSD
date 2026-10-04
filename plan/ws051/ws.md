<!-- awesome-plan project=zedbsd record=ws051 -->

# WS051: USB-C の DisplayPort Alternate Mode

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 2026-10-04 p001 の design.md 第 1 版（調査と移植の範囲、Phase の案）。次は design-reviewer のレビューと §10 の判断（TBT-alt、1 画面、hotplug の範囲）。i915 の display が前提、UCSI（WS050）は必須でない（2026-10-04 ユーザーの決定 5）
<!-- awesome-plan-current:end -->

## 目標

USB-C の port につないだ DisplayPort の display（USB-C の monitor、USB-C から DP・HDMI への変換）に、DisplayPort Alternate Mode で画面を出す。

## きっかけ

2026-09-24 ユーザー指示: 「USB-C DisplayPort Alternative Modeの実装。」

## 前提と今あるもの

- DP の mode に入るのは PD controller・EC の firmware と IOM（PMC の mux）で、i915 は TCSS・FIA の register で状態（live status、PHY の ready、
  lane と pin の割り当て）を読み、HPD は i915 の Type-C の hotplug の割り込みで受ける（Linux の i915 の `intel_tc.c` と同じ分担）。
  **UCSI（WS050）は必須の依存でない**（2026-10-04 ユーザーの決定 5: WS051 は UCSI を待たずに進める）。UCSI 2.0 以上で HPD・pin が取れる時は
  WS050 がそれも取り、i915 の値と統合する（決定 5 の補足）。firmware が自分で DP mode に入らない時は WS050 の SET_NEW_CAM を使う。
  plug の向きは UCSI 1.x の機種では i915 の TCSS から取る（決定 4、取れるかは p002 で確かめる）。
- 画面を出すのは GPU の display engine: 対象機（Alder Lake-P）では i915 の Type-C の subsystem（TCSS: FIA の lane の割り当て、Type-C PHY、
  DP の link training、HPD の割り込み）。i915 の driver は WS029・WS031 にある（render と Vulkan）。**display の modeset（pipe・transcoder・DDI）の
  範囲と、内蔵 panel 以外の出力がどこまであるかは p001 で調べる。**
- zdesktop（WS035）の複数 display の扱いは、出力が出た後の話。

## 範囲

- TCSS・FIA で DP-alt の状態を読み（live status、PHY の ownership、TC cold、lane と pin の割り当て）、HPD を受け取る。読んだ HPD・pin・向きを
  WS050 の Type-C の層に報告する。
- i915 の TCSS: lane を DP に割り当て、Type-C PHY を DP で使い、DDI・transcoder・pipe を立てて link training、EDID の読み出し。
- 抜き差しと HPD の IRQ の扱い、画面の構成の変更を上（framebuffer・zdesktop）に通知する。

## 受け入れ

- 対象機の USB-C port につないだ DP の monitor に画面が出る（mirror か拡張かは p001 で決める）。抜いて差し直すと戻る。
- 規約の全文、build、boot test。実機の証拠が中心（QEMU には無い）。

## Phase 一覧

| Phase | 内容 | Status | 依存 | 対象 |
| --- | --- | --- | --- | --- |
| [ws051-p001](phase001/phase.md) | 調査と設計: i915 の TCSS・Type-C PHY（DKL）・TC PLL・DDI の手順、今の i915 の display の範囲、画面の構成、WS050 との連携 | in-progress（2026-10-04。[design.md](design.md) 第 1 版、レビュー待ち） | — | 設計文書 |
| ws051-p002 | TC の port の核（`tc.c`）、VBT の DVO の code の修正、TC の HPD、診断（向きの確かめを含む） | planned | p001 | `src/drivers/gpu/i915/display/` |
| ws051-p003 | DKL PHY と TC PLL、DDI の TC の clock、buffer translation | planned | p002、bare metal の Linux の正解値 | 同上 |
| ws051-p004 | 外部 DP の検出と出力（TC の AUX、DPCD・EDID、link training、modeset、出力の選択） | planned | p003 | 同上 |
| ws051-p005 | 抜き差し（HPD の pulse、disconnect と再 connect） | planned | p004 | 同上 |
| ws051-p006 | 規約の全文の確認と最終の確認 | planned | p002〜p005 | WS の全 source |

## 2026-10-04 予定（Q1）

ユーザー「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」→ [queue.md](../queue.md) の q679（WS050 p001）・q680（WS051 p001）・q681（WS052 p001）。設計は WS049 の q677（BUG-165、DSDT）と並走、実装は q677・q678（ws049-p007）の後。

## 2026-10-04 ユーザーの決定（WS050 §10、Q1 経由）と WS051 への影響

決定 5（HPD・pin は i915 の TCSS・FIA から、UCSI 2.0 以上で取れる時は UCSI からも）により、WS051 は UCSI を待たずに進める。決定 4（向きは 1.x でも
i915 から）により、p002 で TCSS・FIA の register から向きが取れるかを確かめる。i915 の値は WS050 の Type-C の層へ報告する（WS050 p005）。
