<!-- awesome-plan project=zedbsd record=ws113-p007 -->

# ws113-p007: 窓の出力所属と画面間移動

Parent: [WS113](../ws.md)
Status: cleared（2026-10-08 Q1 の判定: T1-369 QEMU PASS、5330 の実機でユーザー「2つめのディスプレイにカーソール移動でき、ウィンドウも移動できました。」。残りは p015）
Disposition: normal
Primary Milestone: MG006（WSから継承）
Queue / attempts: q855（P1、2026-10-07〜08、Q1 の ACK「p007 の範囲 1)〜6) で進めてよい」）
Purpose / goal: 拡張表示で窓全体を1出力にだけ表示
Prerequisites: p004 cleared/論理座標・出力描画（p006とは独立）
Investigation bound: 120分の有限1 Phase Queue案。選定時にscope/時間を再照合する。

## Procedure / affected components

top-level窓の所属を持ち、drag pointerが隣画面に入った時点で一括切替。boundaryで二重描画しない。disconnect時は残るoutputへ退避。
[設計](../design.md)の対応段と[全文方針](../../standards/ws113-display.md)を契約とする。材料が変わればWSと影響する他Phaseへ同時反映し、既存Queueの実装scopeを拡張しない。

## Clearance / verification

窓は拡張時に単一outputでのみ見え、境界を跨ぐdragで全体が移る。窓の一部が別displayに現れない。mirror時は全画面複製。
結果は対象環境・source revision・commands・artifactsと結びつける。既存の実機/QEMUを混同しない。未解決の前提で調査上限に達したらattemptをunclearedとし、証拠と再開条件を記録する。

## Standards / limits / evidence

[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[scoped full rule](../../standards/ws113-display.md)、[automation](../../standards/automation.md#ws113-multi-display-coverage-2026-10-02)を実装前に読む。formatter/style-checkは補助、意味/所有/イベント順はfull/manual review。HAL API変更は差分ごとの事前承認。compositorのGPU UAPI直接ioctl禁止。無関係なtoolchain変更、aggregate make check、既存WS089/WS099のPhase改変は含めない。

Commands/results/commit/environment/artifacts/skipped checks: 未実施（計画のみ）。Findings: [現状調査](../design.md)。Resume: prerequisiteの実出力を確認し、このPhaseだけを新Queueへ選定・承認後に開始。

## p001契約調査による詳細化（2026-10-02）

[origin p001](../phase001/phase.md)、[契約](../phase001/contracts.md)、[ID/完了比較](../phase001/identity-completion.md)、[fixture](../phase001/fixtures.md)、[WS summary](../ws.md)を入力とする。

Procedure: root output_token/ownership_epochにpopup/subsurface/transient/decoration/shadowを従属させ、owner以外のrender listへ一切追加しない。global pointerのshared-edge越境でgrab offsetを保持してtree全体を1回切替、motion更新の後にbutton処理。大delta/負origin/nonshared edge/小target/未ACK resizeを扱う。disconnectは残るownerへtree退避、0台park。D-ATOMIC採択のphysical transition順を実装し、未採択解釈を勝手に緩和しない。

Verification / resume: D04–D07/D10を適用。root boundsが境界を跨いでも別outputに一部を描かない。logical ownerと実scanoutを別証拠にする。旧window有りqueued frame/高速往復/切断during drag/grab cancelを確認。p006との新依存は追加しない。

Status/dependenciesは上記のまま。未採択architecture/製品判断とactual prerequisiteを確認し、新QueueでこのPhaseだけを有限選定・承認後に実装する。q586はp001文書のみで後続sourceを許可しない。

## 採択済local port ID / native capability入力

[main採択A2とsource](../phase001/identity-completion.md)、[native capability結線](../phase001/native-contract.md)を使う。自Phaseへの影響: 退避先のdeterministic key順はA2の検証済local key、persist不可ならsession tokenの決定的順。reconnect同port新generation、window奪回無し。
後続の実装権限/依存は不変。actual API番号/layout/共有callback差分は選定前にowner/main review、HAL変更なら事前承認。

## Event

2026-10-02 / ws113-multidisplay-plan-20261002-ws113-p007-created: current userの5条件・3つの追加判断をこのPhaseへ投影。planned/Queue none。GitHub body/comment/Projectへの公開は保留。

2026-10-02 / ws113-contract-design-20261002-a3-ws113-p007: p001のsource/一次仕様で明らかになった不足に合わせ、上記の自Phase procedureと検証/resumeを詳細化。D04–D07/D10を適用。root boundsが境界を跨いでも別outputに一部を描かない。logical ownerと実scanoutを別証拠にする。旧window有りqueued frame/高速往復/切断during drag/grab cancelを確認。p006との新依存は追加しない。 origin/WSリンクは上記。planned/Queue noneを保持。GitHub body/comment/Projectはmainへdelivery依頼pending。

2026-10-02 / ws113-technical-choice-20261002-a3-ws113-p007: mainのdelegated technical decision messageからD-BOOT/LAYOUT/REC/AUTH/PORT通常案を採択記録。自Phase影響: 退避窓の自動奪回無し、edge連結入力を採用。D-ATOMIC回答前にphysical解釈を採択しない。 [origin](../phase001/phase.md)/[詳細](../phase001/identity-completion.md)/[WS](../ws.md)。依存/Queue権限不変、main remote delivery pending。

2026-10-02 / ws113-local-port-id-20261002-a3-ws113-p007: mainのD-ID A2/旧bootpreferred技術採択messageを受領。退避先のdeterministic key順はA2の検証済local key、persist不可ならsession tokenの決定的順。reconnect同port新generation、window奪回無し。 [origin](../phase001/phase.md)/[sourceと範囲](../phase001/identity-completion.md)/[WS](../ws.md)。既往eventを保存し、該当current designを更新。p001 in-progress、他Phase planned/Queue none。main remote delivery pending。

## 2026-10-08 実装（q855、P1）

範囲（Q1 の ACK の 1〜6）と結果:

| 区分 | 内容 |
| --- | --- |
| 平面 | 新しい `userland/desktop/wayland/plane.c`・`plane.h`: anchor の左上を原点にした平面。出力は slot（0 = anchor、1+N = head N、大きさ 0 は非表示）。`kwl_plane_at`（点を持つ出力）、`kwl_plane_move`（相対の移動: 届いた出力へ、どの出力にも無い点は表示中の出力の最も近い点へ、同点は今の出力 = 共有しない辺で止まる）、`kwl_plane_neighbour`（左右の隣、中心の近い方）、`kwl_plane_carry`（同じ割合の位置へ、出力の中に収める、上は title bar の分を空ける）。 |
| 所属 | `kwl_object.output`（窓の出力、0 が既定 = anchor）。窓・pointer の座標は平面（anchor の窓は従来どおり 0..width）。sheet は親の出力、popup・sub-surface は root の窓の出力（`kwl_window_output`、`kwl_popup_root`）。 |
| 描画（D-ATOMIC (a)） | `compose.c` の `compose_split`: frame の窓を anchor の分と head の分に分け、anchor の pass は anchor の窓だけ、head の pass（`heads.c` の `heads_record_extended`）は壁紙の後にその head の窓（glass は `shell.c` の `kwl_glass_draw_head` = `draw_window`、system bar・App Home・Wiseview は無し、glass は blur の壁紙）と popup と cursor。quad と glass の shape は `server->view_*`（描いている出力の平面の矩形）で NDC と box を出す（`compose_quad_part`・`glass_shape_draw`、sheet の scissor も）。popup の描画は出力で絞り、popup の配置の制約は親の窓の出力の矩形で行う。出力ごとの render list を変わった時に `KWL RENDER output=N surfaces=...` で log。frame の hold（frame callback）は全部の窓を含む。head は窓・pointer がある間（と前の絵にあった次の frame）は毎 frame 描き、image が間に合わない時は次の frame を求める。 |
| pointer | 相対の機器（mouse・touchpad）は `kwl_pointer_relative` で共有の辺を越えて隣の出力へ（`KWL POINTER output=N`）、絶対の機器（tablet・touch screen・QEMU の usb-tablet）は anchor（`kwl_pointer_absolute`）。窓の hit（`window_at`）は点の出力の窓だけ。pointer が head にある時の press は head の窓だけ: 角・縁・system bar とその部品・App Home・desktop の icon・docked の余白は anchor の物（release は従来どおり）。 |
| 移動 | title bar の drag で pointer が共有の辺を越えた瞬間に窓の出力を変える（grab offset は保つ、`why=drag`）。head では体の上限は head の上端＋title bar。bar への dock は pointer が anchor にある時だけ。Super+Shift+Left/Right（既存の短縮 key と衝突しないことを確認: Super+Tab、Super+Alt+文字、Super+Down、Super+L、Ctrl+Alt+矢印）で焦点の窓を隣の display へ（`why=key`、docked・fullscreen は動かさない、隣が無ければ `KWL WINDOW output none`）。 |
| 退避 | head を閉じる時（抜去・lost を含む）にその窓と pointer を anchor へ同じ割合で（`why=retreat`）。mirror を選んだら head の窓を anchor へ（`why=mode`）、位置の変更では head の窓・pointer が head と一緒に動く。 |
| 制限（範囲 6） | head の窓の dock・fullscreen は先に anchor へ移してから（`why=dock`・`why=fullscreen`）。Wiseview・App Home・切り替え・input method の候補は anchor だけ。新しい窓は anchor に開く。head の窓の commit は anchor の partial redraw にならず全体を描く（効率の制限）。 |
| 範囲 5 の enter/leave | compositor は今 `wl_surface.enter`・`leave` を一度も送っていない（1 出力の時も）ので、所属の変更での enter/leave は送らない（別の課題）。 |

確認（host、2026-10-08）:
- `sh plan/ws113/tests/host-plane.sh` → `WS113 p007 plane host test PASS (plain, ASan/UBSan)`（共有の辺の越え・戻り・共有しない辺で止まる・head の下・遠くへの跳び・低い head・非表示の slot・隣・3 つ並び・carry の割合・収め・大きい窓・anchor への戻り）。
- `sh plan/ws113/tests/host-output-switch.sh` → PASS（回帰）。
- build（warning 0）: `make -j16 BUILD=build/p1-wl ZEDBSD_CONFIG=plan/ws113/tests/config-amd64-p005.mk build/p1-wl/bin/wayland`（`ZEDBSD_TEST_SCREEN_CAPTURE=y` も）、`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p1-wl-linux all`。style-check: 新しい file 0、変えた file は増えない。

未実施:
- QEMU（T1 `plan/ws113/tests/displays-p007.sh`、image は `config-amd64-p006.mk`）: keyboard での移動・抜去の退避・mirror の退避を render list の行で判定。QEMU の tablet は絶対なので pointer の越えと title bar の drag での移動は QEMU では見られない。
- 実機（p008、5330）: mouse・touchpad で pointer が HDMI の画面へ越えること、title bar の drag で窓が移ること、HDMI の抜去。FreeBSD の build。

## T1-369 の判定（2026-10-08 Q1）

PASS（QEMU Venus 2 出力、`displays-p007: PASS`: 新しい窓は anchor、Super+Shift+Right で head 1・Left で戻る、head 1 を抜くと anchor、mirror で anchor、mirror の隣は none、KWL FAILED 無し、生存）。mouse の跨ぎと title bar の drag は QEMU の tablet が絶対座標なので実機（5330、ユーザー）で。

## 5330 の実機の UAT（2026-10-08 ユーザー）

「2つめのディスプレイにカーソール移動でき、ウィンドウも移動できました。」→ mouse の跨ぎと窓の移動は実機で OK。リサイズが効かない件と head の dock・状態・App Home の背景は [p015](../phase015/phase.md)。
