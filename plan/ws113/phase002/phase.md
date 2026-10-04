<!-- awesome-plan project=zedbsd record=ws113-p002 -->

# ws113-p002: i915のHPD・複数display出力

Parent: [WS113](../ws.md)
Status: planned
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: none / 実装未承認
Purpose / goal: driverからGPU表示イベントを提供し、複数出力を同時に扱う
Prerequisites: p001 cleared/driver契約
Investigation bound: 120分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

i915/displayのHPD→表示イベント通知、接続済み出力のgeneration、複数claim/mode/presentを実装。既存UAPI責務で足りなければ変更を局所化。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

HPD接続/切断で表示イベントと列挙内容が一致し、2出力を同時にpresentできる。host contractと実i915の下層証拠を分ける。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: connectorの固定slot/非0ID、output generationとdevice topology sequenceの分離、HPD workerのcoherent inventory publish→poll_notify、独立open ACKを実装・検証する。単一rdをper-output lease/pipe/PLL/scanoutへ分け、切断中のbuffer retirementと残るhead継続を証明する。EXT control全entryに必要なpower/next-first-pixel/vblank能力、present completionの不足を差分設計し、共有/HAL所有境界の承認前にAPI変更しない。

Verification / resume: H01–H03/H07–H10を適用。connector数/connected数/plane indexを混同しない。2台の独立claim/presentはactual i915両画面とcounterで確認し、model PASSを代替にしない。D-ID A2/boot anchor/初回fixtureはmain採択。D-ATOMICの依存部分は回答後に選定。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: 既存PCI address+kind/DDIでname[64]内のlocal key生成。ordinal禁止。旧bootはpreferred anchor。GPU UUID nativequeryを必須APIから外す（比較としてのみ残す）。power/timing能力はnative-contract.mdの差分review入力。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p002-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p002: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。H01–H03/H07–H10を適用。connector数/connected数/plane indexを混同しない。2台の独立claim/presentはactual i915両画面とcounterで確認し、model PASSを代替にしない。D-ID/D-ATOMIC/D-PORTとboot overrideの材料が必要部分を選定前に解決。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p002: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 初回fixture eDP+HDMIとall-connected初回extendedを入力にする。未移植portを完成扱いにしない。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p002: mainのD-ID A2/旧bootpreferred技術採択messageを受領。既存PCI address+kind/DDIでname[64]内のlocal key生成。ordinal禁止。旧bootはpreferred anchor。GPU UUID nativequeryを必須APIから外す（比較としてのみ残す）。power/timing能力はnative-contract.mdの差分review入力。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2、zedBSD 優先）

入力: [残りの契約の確定](../phase001/contracts-beta2.md)（D-GOP・D-GOP-INV・D-RELEASE・D-BOOT2）。**範囲を絞った**: 2 つ目の出力を同時に出すのは [p011](../phase011/phase.md)、native の power・refresh は [p012](../phase012/phase.md) に分けた。

範囲（i915、`src/drivers/gpu/i915/display/`）:
- A. **scanout の規則（Guardrail、WS051 p002 と共有、C4）**: 起動の時は firmware の出力先（takeover の readout で active だった pipe・port）だけを引き継いで scanout する。eDP・HDMI・DP（USB-C の DP-alt は WS051 の TC の port の後）のどれでも初期化を試み、対応していない interface なら `i915: GOP output on PORT not supported, firmware picture kept` を log に出して firmware の framebuffer を保つ。`output.c` の `display=` の auto・hdmi・edp の選択と `I915_OUTPUT_HDMI_WAIT_MS` の待ちを除き、`display=` を見たら「ignored」を log に出す。
- B. **inventory と HPD**: 接続している全ての connector（eDP・HDMI・DP）を `GPU_DISPLAY_QUERY` に出す（固定の slot、非 0 の display_id、output の generation）。scanout 中の物にだけ ACTIVE。HPD の worker が接続・EDID・mode を確定してから短い lock で publish し、device の topology の sequence を進め、lock の外で `poll_notify()`。固定 1 の sequence を実の HPD に置き換える。D-ID A2 の key（`zedbsd-port-v1:pci:SEG:BB:DD.F:KIND:PORT`）を `name[64]` に。
- C. 列挙・mode の問い合わせは scanout を始めない（D-GOP-INV）。GOP の出力先でない connector の CLAIM は p011 まで `EOPNOTSUPP`（偽の成功を返さない）。

手順: 1) takeover の readout の結果から GOP の出力先（pipe・transcoder・DDI・port の種類）を記録する口を作る。2) `output.c` を「GOP の出力先を引き継ぐ」に書き換え、HDMI の優先を除く。3) connector の表と generation・topology の sequence を HPD の worker に。4) host の試験（`src/drivers/gpu/i915/tests/display/` の host の modeset の試験に、GOP の出力先が eDP・HDMI の各場合と、HPD の plug・unplug の sequence・ACK の race（H01〜H03・H07））。5) build（zedBSD の kernel、vmunix の kernel include check まで）。

試験: host（上の 4）、QEMU は i915 が無いので boot-test だけ（Venus の画面が変わらないこと）。実機（T1 が 5330 で、または p008 に寄せる）: eDP だけ・eDP+HDMI（GOP は eDP）で起動して、HDMI が点かないこと、`GPU_DISPLAY_QUERY` が 2 つ（eDP ACTIVE、HDMI CONNECTED）を返すこと、HDMI の抜き差しで sequence が進むこと（`plan/ws113/tests/` に小さい probe を足す: native の QUERY・EVENTS を読むだけの test program）。
受け入れ: 上の実機の 3 点と host の試験の PASS、build の warning 0、規約。目安 3〜4h。依存: p001（C4 の分担の決定）。衝突: WS051 p002（同じ file、C4）、WS075・WS084 の i915 の Phase（Q1 が確かめる）。
