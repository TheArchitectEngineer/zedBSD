<!-- awesome-plan project=zedbsd record=ws051 -->

# WS051: USB-C の DisplayPort Alternate Mode

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 2026-10-07 P2: p001 の design.md 第 4 版（§14）、design-reviewer の後に p002（TC PLL の enable の番地・DVO の code）。それ以前: 2026-10-04 p001 の design.md 第 1 版（調査と移植の範囲、Phase の案）。次は design-reviewer のレビューの反映。§10 は 2026-10-04 に決定済み（TBT-alt 範囲外、GOP の出力先の引き継ぎ、hotplug は Vulkan の Display の拡張で Keiland へ）。i915 の display が前提、UCSI（WS050）は必須でない（2026-10-04 ユーザーの決定 5）
<!-- awesome-plan-current:end -->

## 目標

USB-C の port につないだ DisplayPort の display（USB-C の monitor、USB-C から DP・HDMI への変換）を i915 が DisplayPort Alternate Mode で駆動でき、
抜き差しを Vulkan の Display の拡張の経路で Keiland に知らせ、Keiland の指示で画面を出す（2026-10-04 ユーザーの決定。mirror・拡張・出力 off は
Keiland が決める）。i915 は GOP の出力先以外に自分の判断で scanout を始めない（Guardrail「GPU の driver の scanout の規則」）。

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
- GOP の出力先の引き継ぎ（`takeover.c`）と、今の外部 display の優先の挙動の廃止（Guardrail の規則、決定 2）。GOP の出力先なら eDP・HDMI・DP・
  USB-C のどれでも初期化を試み、非対応なら firmware の画面を保つ。
- 抜き差しと HPD の IRQ の扱い。抜き差しは display の UAPI の事象（`GPU_DISPLAY_EVENT_CHANGE`）から libvulkan の Display の拡張の通知へ
  （WS113 と同じ経路）。TC の output を display の UAPI の列挙に出し、Keiland の claim・present の時だけ出力する。

## 受け入れ

- 対象機の USB-C port につないだ DP の monitor に、Keiland の指示（display の UAPI の claim・present）で画面が出る。抜き差しが Vulkan の Display の
  拡張の通知に届き、差し直すと Keiland の指示で戻る。起動時に USB-C の display があっても GOP の出力先がそのまま出る（driver が切り替えない）。
- 規約の全文、build、boot test。実機の証拠が中心（QEMU には無い）。

## Phase 一覧

| Phase | 内容 | Status | 依存 | 対象 |
| --- | --- | --- | --- | --- |
| [ws051-p001](phase001/phase.md) | 調査と設計: i915 の TCSS・Type-C PHY（DKL）・TC PLL・DDI の手順、今の i915 の display の範囲、画面の構成、WS050 との連携 | in-progress（2026-10-07 P2: [design.md](design.md) 第 4 版（§14: §13 の決定、WS113 p002 との分担、WS050 の口、WS084）、design-reviewer に掛ける） | — | 設計文書 |
| ws051-p002 | TC PLL の enable の番地（H8）、VBT の DVO の code を vbt-defs.h に（L1）（GOP の引き継ぎと外部優先の廃止、GOP が USB-C の時に firmware の画面を保つ判定は WS113 p002 part A が実装・host 試験済み、design §14.2） | planned | p001 | `src/drivers/gpu/i915/display/`（takeover.c・diagnostics.c）、i915 の ktest |
| ws051-p002b | TC の port の核（`tc.c`）、TC の AUX の power domain（H1）、AUX_USBC の well の TC の分岐（H2）、DE の HPD の配送（H5）、診断（向きは記録だけ） | planned | p002 | `src/drivers/gpu/i915/display/` |
| ws051-p003 | DKL PHY と TC PLL、DDI の TC の clock、ADL-P の DKL の buffer translation、DP_MODE、FIA の lane 数、GOP が USB-C の時の引き継ぎ（M5） | planned | p002b、bare metal の Linux の正解値（読み取り） | 同上 |
| ws051-p004a | TC の AUX・DPCD・EDID の診断、外部 DP の object（M6）、調べた後の同期の disconnect（M2）、branch device と sink count（H7） | planned | p003 | 同上 |
| ws051-p004b | display の UAPI での出力（QUERY への TC の output、claim・mode・present での link training（fallback、M3）・modeset・scanout） | planned | p004a、WS113 の契約 | 同上 |
| ws051-p005 | 抜き差しの事象（`GPU_DISPLAY_EVENT_CHANGE`、2 秒の猶予と 5 回の retry、scanout 中の抜けの停止、IRQ_HPD の retrain、M4）、S0ix の口（M10） | planned | p004b | 同上 |
| ws051-p006 | 規約の全文の確認と最終の確認 | planned | p002〜p005 | WS の全 source |

## 2026-10-04 予定（Q1）

ユーザー「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」→ [queue.md](../queue.md) の q679（WS050 p001）・q680（WS051 p001）・q681（WS052 p001）。設計は WS049 の q677（BUG-165、DSDT）と並走、実装は q677・q678（ws049-p007）の後。

## 2026-10-04 ユーザーの決定（WS050 §10、Q1 経由）と WS051 への影響

決定 5（HPD・pin は i915 の TCSS・FIA から、UCSI 2.0 以上で取れる時は UCSI からも）により、WS051 は UCSI を待たずに進める。決定 4（向きは 1.x でも
i915 から）により、p002 で TCSS・FIA の register から向きが取れるかを確かめる。i915 の値は WS050 の Type-C の層へ報告する（WS050 p005）。

## 2026-10-04 WS051 §10 のユーザーの決定（Q1 経由、design.md の末尾に記録）

TBT-alt は範囲外。driver は GOP の出力先を引き継ぎ自分で出力先を変えない（今の外部 display の優先はやめる。p002 で直す）、USB-C の DP の接続は
Keiland が Vulkan の Display の拡張で通知を受け、mirror・拡張・出力 off を決める。Guardrail に「GPU の driver の scanout の規則」（GOP の出力先
以外に scanout を始めない、GOP の出力先ならどのインタフェースでも初期化を試みる）。
