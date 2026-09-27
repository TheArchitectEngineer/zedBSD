<!-- awesome-plan project=zedbsd record=ws075 -->

# WS075: i915 の高度化（今のデスクトップとグラフィックスを 5330 の i915 のネイティブ実行器で）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: none（サブエージェント、WS068 から続けて）
Resume point: p001（調査）in-progress（2026-09-27 着手）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」
（WS068 の GL 3.2 は Venus で PASS。main の中継）。

今の desktop と graphics を Dell Latitude 5330（Alder Lake-P、`8086:46a8`）の i915 のネイティブ Vulkan 実行器（WS031）で動かす:

- zdesktop（glass、backdrop のぼかし、tab、System Menu）と、その上の Vulkan の client（zdesktop-files、zdesktop-terminal、mview）。
- EGL/GLES 2・3（egltest の場面）と X11 の GLX の GL（固定機能、GL 3.0〜3.2 の glxtest）。
- そのために要る i915 の compiler（SPIR-V → GEN）と実行器（Vulkan の command → GEN）と driver（fence、共有、割込み）の不足の補い
  （[F-023](../future-work.md)、F-022 の残り）、性能と安定（完了の割込み、非同期の実行器、vsync、BUG-056・BUG-057）。

WS031 の単一目標（vkdemo のネイティブ描画）とは別の到達目標なので、新しい WS に置いた。WS031 の planning の Phase のうち
この目標に要るものはここへ移した（下の表）。

## 受け入れ

1. 実機（5330、capture）で zdesktop の glass・backdrop・tab・System Menu の絵が Venus と同じ形で出る（`CAPTURE=zdesktop`・
   `plan/ws070/tests/menu-hw.sh`）。
2. 実機で egltest の es2・es3 の場面、GLX の zgears と glxtest の `--gl3`・`--gl31`・`--gl32` の検査が通る（通らない機能は
   device の feature として正しく断り、記録する）。
3. host の i915 の shader の検査で、上の client と libGLESv2 の生成する shader が全て通る。
4. 変えた source の規約の全文との照合、回帰（Venus の回帰と boot test、実機の回帰）。

QEMU（Venus）の証拠と実機（i915）の証拠は分けて書く。実機は `flock /tmp/i915-hw.lock` の下で 1 つずつ。

## 規則・境界

- AGENTS.md と [plan/coding-style.md](../coding-style.md) の全文、WS031 の [i915-rebuild-rules.md](../ws031/i915-rebuild-rules.md)。
- HAL（`include/hal/hal.h`）・UAPI（`include/drivers/gpu.h`）の変更は事前に提示する（`plan/ws075/proposed/`）。
- zdesktop の shader・source は desktop のサブエージェントが変えている。**zdesktop を直すのでなく i915 の側を直す**
  （例: panel.frag の関数呼出しは i915 の compiler で inline 化する）。zdesktop に触れる時は main を通して調整する。
- 試験は amd64 だけ。phase の最後に 1 回（host の試験、Venus の回帰、boot test、要るときだけ実機）。

## Phase

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws075-p001](phase001/phase.md) | 調査（host と実機 1 回）: i915 の shader の検査を最初の拒否で止めずに全ての不足を数える（今の zdesktop・client・libGLESv2 の生成する shader、egltest・glxtest の場面）、client ごとの実行器の opcode（Venus で記録し `render/dispatch.c` の表と突き合わせる）、今の zdesktop の実機の capture。不足の一覧と、下の Phase への割り当て | in-progress（2026-09-27 着手） | — |
| ws075-p002 | zdesktop を実機で: compiler の関数呼出しの inline 化（zdesktop の `panel.frag`。ws031-p041 の一部）と、p001 で出た desktop の不足。受け入れ 1 | planning | p001 |
| ws075-p003 | F-023 の残り: `gl_VertexIndex`・`gl_FragCoord`、OpSwitch（ws031-p041 の一部）、OpCompositeInsert、triangle strip、vkFreeDescriptorSets、`GPU_CAP_FENCE`・`GPU_CAP_ALLOCATION_SHARE` | planning | p001 |
| ws075-p004 | GLES の compiler: 整数の varying と Flat（ws031-p038）、local の配列・構造体・動的 index（ws031-p040）、member decoration、struct・配列の定数、OpImage | planning | p001 |
| ws075-p005 | GLES の実行器: mip level・layer への描画（ws031-p030）、MRT（ws031-p031）、sampler（compare 等、ws031-p034）、descriptor 配列・VS の sampled image（ws031-p035）、3D・配列の texture、vertex の store（transform feedback） | planning | p001 |
| ws075-p006 | GL 3.0〜3.2 を実機で（texel buffer、geometry の stage、layered、multisample の texture）。着手前に分ける | planning | p004、p005 |
| ws075-p007 | 性能: 完了待ちを割込みへ（ws031-p044）、非同期の実行器（ws031-p045）、present mode と vsync（ws031-p027） | planning | p002 |
| ws075-p008 | 安定: BUG-056・BUG-057（実機の zgears の止まり）ほか p001〜p007 で出た bug | planning | p002 |
| ws075-p009 | 規約の全文との照合、統合回帰（最後） | planning | 全 Phase |

p001 の結果で Phase の範囲と順を直す。

## WS031 から移した Phase

2026-09-27 に移した（WS031 の表に印）。範囲の正本は WS031 の元の Phase の記述（`phase016`・`phase017`・`phase018/phase.md`）。

| WS031 | 移した先 |
| --- | --- |
| ws031-p027（present mode・vsync） | p007 |
| ws031-p030（mip level・layer への描画） | p005 |
| ws031-p031（MRT） | p005 |
| ws031-p034（sampler） | p005 |
| ws031-p035（descriptor 配列・VS の sampled image） | p005 |
| ws031-p038（整数の varying） | p004 |
| ws031-p040（local の配列・構造体） | p004 |
| ws031-p041（OpSwitch・関数呼出し） | p002（関数呼出し）、p003（OpSwitch） |
| ws031-p044（完了の割込み） | p007 |
| ws031-p045（非同期の実行器） | p007 |
