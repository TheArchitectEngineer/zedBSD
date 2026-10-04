<!-- awesome-plan project=zedbsd record=ws113-p006 -->

# ws113-p006: Settings Displayページ

Parent: [WS113](../ws.md)
Status: planned
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: none / 実装未承認
Purpose / goal: モード選択とドラッグ配置を提供
Prerequisites: p005 cleared/libkeiland公開API
Investigation bound: 90分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

既存読み取り専用Displayページを実設定UIへ置換。出力識別、全拡張/全mirror、配置drag、適用/失敗/変更通知を実装。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

Settingsからlibkeilandのみで切替と配置変更ができ、hotplug後の一覧を更新する。無効配置時は利用者に理由を表示。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: public libkeilandだけでmode二択、出力一覧、extended配置drag draft/edge snap/Apply/cancel、mirror配置dragの無効化を実装する。Apply前にhardwareを変更しない。hotplug通知・stale再読込・backend failure・適用成功だが保存失敗を実状態に合わせて表示する。

Verification / resume: D02/D03/D08/D09を適用。driver/GPU/Vulkan direct call無しを監査。draftとauthoritative snapshotが競合する場合の利用者操作、接続0/1/2、未対応server、保存失敗を確認。WS089の旧stub履歴を遡及変更しない。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: Settingsはkind/portから人向けlabelを表示、driver EDIDnameの同handle書換に依存しない。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p006-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p006: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。D02/D03/D08/D09を適用。driver/GPU/Vulkan direct call無しを監査。draftとauthoritative snapshotが競合する場合の利用者操作、接続0/1/2、未対応server、保存失敗を確認。WS089の旧stub履歴を遡及変更しない。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p006: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 非重複/辺連結edge snapを配置draft/Apply検査へ。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p006: mainのD-ID A2/旧bootpreferred技術採択messageを受領。Settingsはkind/portから人向けlabelを表示、driver EDIDnameの同handle書換に依存しない。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2）

入力: [契約の確定](../phase001/contracts-beta2.md) D-MODES・D-BRIGHT、contracts.md §8 の Settings の段。

範囲（`userland/desktop/settings/`、`page-look.c` の `se_display_draw` を新しい `page-display.c` へ）: libkeiland の `kl_system_displays_*` だけを使う。
- 頁の上: 出力の配置の図（出力ごとの card に label・解像度、内蔵は panel の印）。拡張の時は card を drag で動かせ、離すと辺に snap（重ならない・辺で連結）。drag は draft だけを変え、Apply で送る。
- モード: 「Extend」・「Mirror」の二択（segmented）。mirror の時は配置の drag を無効にする。
- 明るさ: snapshot に has_backlight の出力（内蔵の panel）がある時だけ slider（0〜100）。drag 中は 100 ms ごとに set_brightness、離した時に最後の値。Fn の key の変更は snapshot の変更で slider に反映。
- Apply・Revert、結果の表示（stale は再読み込みして「画面の構成が変わりました」、unsupported・backend_failed は理由、saved=0 は「保存できませんでした」）。hotplug で一覧を更新。出力が 1 つの時は配置の図を 1 つの card で示し、モードは選べるが効果は同じと示す。
- 解像度・refresh・回転・出力ごとの off は出さない（D-MODES）。

手順: 1) 頁の model（draft・snap の計算）を描画と分け、host の試験（`plan/ws089/tests/` の settings-render に Display の頁の絵、snap の計算の case）。2) 描画と入力（既存の Settings の widget）。3) 明るさの slider。

試験: host（settings-render の Display の頁の PNG: 1 出力・2 出力の拡張・mirror・明るさの slider あり／なし、snap の case）。QEMU（T1、Venus の 2 出力）: Settings の Display の頁で拡張 ⇔ mirror を 5 回、card の drag で左右を入れ替え（M2）、PNG。実機（p008）: 明るさの slider と Fn の key。
受け入れ: M2 の QEMU の PASS と PNG、host の PASS、warning 0、規約。目安 3h。依存: p005。衝突: WS089 の Settings の Phase と直列（`page-*.c`・`pages.c`）。

### D-LIMIT の反映（2026-10-05）

limited の出力の card を薄く描き「同時に表示できる数の制限で使えません」と示す（drag はできない）。hotplug や他の出力の解放で limited が外れたら通常の card に戻る。
