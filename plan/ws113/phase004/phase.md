<!-- awesome-plan project=zedbsd record=ws113-p004 -->

# ws113-p004: compositorの出力・表示モード

Parent: [WS113](../ws.md)
Status: planned
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: none / 実装未承認
Purpose / goal: 全拡張または全mirrorで複数outputを描画
Prerequisites: p003 cleared/Vulkanの実複数出力
Investigation bound: 120分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

出力ごとのcompose/surface/swapchain/present、論理座標、mirror複製、disconnect再構成を実装。GPU操作はlibvulkanのみ。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

1/2 displayの拡張とmirrorが安定、異解像度mirrorも全内容、接続/切断後も残るdisplayで継続。Linux/FreeBSD単一表示を保つ。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: outputごとのcompose/swapchain/render状態とdynamic wl_outputを作る。0接続でもserver/device watcherを保持。extendedはsigned global originとhalf-open境界、mirrorは1論理desktopを各native modeへGPU aspect-fitし黒帯をopaqueに塗る。全接続集合のvalidate/stage/applyとrollback/degradedの実状態を保持する。起動/保存anchorは採択されたpolicyに従う。

Verification / resume: D01–D03/D06、H08を適用。異解像度で四隅全contentを確認、common mode/driver retiming/refresh同期を仮定しない。配置overflow、資源不足、切断中applyのtruthful snapshotを確認。p007へroot ownerを使える描画/input/output契約を渡す。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: 保存keyはlocal port scope。connected集合を旧bootpreferredで隠さず全参加。unknown/invalid keyでもworking状態を保ち、mode/capability validate。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p004-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p004: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。D01–D03/D06、H08を適用。異解像度で四隅全contentを確認、common mode/driver retiming/refresh同期を仮定しない。配置overflow、資源不足、切断中applyのtruthful snapshotを確認。p007へroot ownerを使える描画/input/output契約を渡す。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p004: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 初回全extended/internal anchor、非重複/辺で連結/edge snapをoutput state/input契約へ。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p004: mainのD-ID A2/旧bootpreferred技術採択messageを受領。保存keyはlocal port scope。connected集合を旧bootpreferredで隠さず全参加。unknown/invalid keyでもworking状態を保ち、mode/capability validate。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-05 計画（q702、ベータ2）

入力: [契約の確定](../phase001/contracts-beta2.md) D-BOOT2・D-HOTPLUG・D-RELEASE・D-STORE・D-QEMU、contracts.md §5・§6。

範囲（compositor `userland/desktop/wayland/`、GPU は libvulkan だけ）: 出力の表（token・persistent key・generation・状態 detected → validated → claimed → active → retiring → gone）、出力ごとの display の surface・swapchain・合成、global の論理座標（signed、half-open、辺で連結）、全拡張と全 mirror（mirror は各出力の native の mode へ aspect-fit、黒い帯、独立の swapchain）、device event の fence で再列挙して出力を足す・外す、0 台で server を保つ、出力ごとの `wl_output` の global（add・remove）、displays.conf の読み書き（D-STORE、明るさは p005）、設定の transaction（validate → 準備 → 再照合 → 適用 → 実 present の確認 → snapshot の publish、失敗は rollback か実状態を degraded で公開）。input の clamp を出力の集合の境界に（contracts.md §6 の共有の辺の処理）。

手順: 1) `compose.c` の 1 出力の state を出力の配列へ（swapchain・frame・damage）。2) `display.c`・`shell.c` の画面の大きさの参照を「窓の owner の出力」「論理の desktop」に分ける（窓の所属そのものは p007、p004 は全ての窓を anchor の出力に置く）。3) hotplug（fence → 再列挙 → transaction）。4) mirror の aspect-fit。5) host の試験（座標・辺の判定・transaction の validate・aspect-fit の計算、`plan/ws113/tests/host-layout.c`）。

試験: host（上の 5）。QEMU（T1、Venus の `max_outputs=2`、zdesktop `--glass`）: 拡張で 2 つの出力の PNG（`zdesktop-check.py` を出力ごとに撮れるか確かめる、QMP の screendump は head を選べる）、mirror で同じ絵、1 出力の guest で今までの回帰（boot-test、files-regress）。guest の起動に `max_outputs=2` の選択肢を足す（`plan/tools/guest/`、Q1 の許可）。実機は p008。
受け入れ: QEMU で拡張・mirror の 2 出力の PNG、1 出力の回帰 PASS、出力が 0 になっても落ちない（QEMU で起こせれば）、Linux・FreeBSD の単一 display の build を壊さない（KMS の複数出力は p010）、warning 0、規約。目安 4〜5h。依存: p003。衝突: WS099 の compositor の Phase（`compose.c`・`shell.c`・`display.c`）と同時に流さない。

### D-LIMIT の反映（2026-10-05）

anchor でない出力の swapchain の作成が `VK_ERROR_INITIALIZATION_FAILED` なら、その出力を limited にして使わずに続ける（server・他の出力は保つ、窓をその出力に置かない）。topology の変化と、他の出力の swapchain の解放の後に再び試す。QEMU の試験に「Venus の出力を上限より多くつないで、limited の出力があっても落ちない」を足すか p004 で確かめる。
