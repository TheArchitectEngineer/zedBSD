<!-- awesome-plan project=zedbsd record=ws075 -->

# WS075: i915 の高度化（今のデスクトップとグラフィックスを 5330 の i915 のネイティブ実行器で）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: none（サブエージェント、WS068 から続けて）
Resume point: p001〜p005 cleared（2026-09-28。p005: texture の種類・depth の copy・形式、実機の egltest の 7 場面が全て failures 0）。BUG-077・BUG-056 は GPU core の close の修正で resolved（2026-09-28）。次は p006（MRT・query・texel/storage buffer・multisample）
<!-- awesome-plan-current:end -->

## 目標

2026-09-27 ユーザー: 「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」
（WS068 の GL 3.2 は Venus で PASS。main の中継）。

今の desktop と graphics を Dell Latitude 5330（Alder Lake-P、`8086:46a8`）の i915 のネイティブ Vulkan 実行器（WS031）で動かす:

- zdesktop（glass、backdrop のぼかし、tab、System Menu）と、その上の Vulkan の client（files、terminal、mview）。
- EGL/GLES 2・3（egltest の場面）と X11 の GLX の GL（固定機能、GL 3.0〜3.2 の glxtest）。
- そのために要る i915 の compiler（SPIR-V → GEN）と実行器（Vulkan の command → GEN）と driver（fence、共有、割込み）の不足の補い
  （[F-023](../future-work.md)、F-022 の残り）、性能と安定（完了の割込み、非同期の実行器、vsync、BUG-056・BUG-057）。

WS031 の単一目標（vkdemo のネイティブ描画）とは別の到達目標なので、新しい WS に置いた。WS031 の planning の Phase のうち
この目標に要るものはここへ移した（下の表）。

## 受け入れ

1. 実機（5330、capture）で zdesktop の glass・backdrop・tab・System Menu の絵が Venus と同じ形で出る（`CAPTURE=zdesktop`・
   `plan/tools/titlebar/menu-hw.sh`）。
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
| [ws075-p001](phase001/phase.md) | 調査: shader の全ての不足（host の survey、122 module）、client ごとの実行器の command（静的）、今の zdesktop の実機の capture | cleared（2026-09-27。実機の zdesktop 6/6 PASS、client の shader・command は全て通る。不足は GL/GLES の側） | — |
| [ws075-p002](phase002/phase.md) | desktop の新しい機能を実機で確かめる: tab、System Menu（`plan/tools/titlebar/menu-hw.sh`）、files・terminal（App Home）。出た不足を直す（zdesktop は直さず i915 の側で） | cleared（2026-09-27。実機で home 4/4・menu 11/11・x11 6/6。files の scenario は desktop の変更が落ち着いてから） | p001 |
| [ws075-p003](phase003/phase.md) | 実行器: primitive topology（triangle strip・fan、line list・strip、point list。今は triangle list だけ）、幅 1 以外の線、index の型、vkFreeDescriptorSets（F-023）。GL の app の大半が要る | cleared（2026-09-27。strip・fan・line・point を描く、実機の vkx 9/9。幅・PointSize・vkFreeDescriptorSets は後） | p001 |
| [ws075-p004](phase004/phase.md) | compiler（GLES 2 の核）: 補間の Flat・NoPerspective・Centroid、input builtin（FragCoord・FrontFacing・PointCoord・VertexIndex・InstanceIndex）、output PointSize、texture() の bias・offset と textureLod、local・interface の配列・struct と配列の定数、微分、Determinant・MatrixInverse・pack half、8・16 bit の vertex format | cleared（2026-09-27。実機 vke2 17/17・vke1 4/4・vkx 9/9・vkc 9/9、zdesktop・x11 の capture 6/6。Grad・fine の y 微分は p005） | p001 |
| [ws075-p005](phase005/phase.md) | texture の種類: compiler の texelFetch（OpImage・OpImageFetch）・textureSize、shadow（Dref）、integer sampler、cube・配列・3D の sampler。実行器の cube・配列・3D の image、mip level・layer への描画（ws031-p030）、depth の copy、sampler の compare 等（ws031-p034）、descriptor 配列（ws031-p035） | cleared（2026-09-28。実機 egltest の glsl・glsl3・fbo・cube・es3・formats・volumes が failures 0、vke1 6/6・vke2 17/17・vkx 9/9・vkc 9/9、zdesktop・files PASS） | p004 |
| ws075-p006 | 実行器と compiler: MRT（ws031-p031）、occlusion query（sync の module）、texel buffer（buffer view）、storage buffer（transform feedback の VS の store）、multisample の image と resolve | planning | p005 |
| ws075-p007 | GL 3.2 の stage: geometry shader（compiler の stage と 3DSTATE_GS）、gl_Layer と layered の描画、PrimitiveId。着手前に分ける | planning | p006 |
| ws075-p008 | 性能: 完了待ちを割込みへ（ws031-p044）、非同期の実行器（ws031-p045）、present mode と vsync（ws031-p027） | planning | p002 |
| ws075-p009 | 安定: BUG-056・BUG-057（実機の zgears の止まり）ほか p002〜p008 で出た bug | planning | p002 |
| ws075-p010 | 規約の全文との照合、統合回帰（最後） | planning | 全 Phase |

各 Phase の受け入れは、host の survey（`plan/ws075/tests/shader-survey/run.sh`）の該当の不足が 0 になることと、実機の capture
（egltest・glxtest の場面の capture の scenario は p003 で足す）。


## files の実機の場面（main の依頼、2026-09-28）

`plan/ws031/tests/i915-capture.py` の `files`（`ZDESKTOP_APP=home plan/ws075/tests/capture-hw.sh files zdesktop OUTDIR`）:
Home から Files を起動し、Ctrl+T・Ctrl+Tab・Ctrl+W でタブを開閉・切り替え、title bar の double click で dock、下端からの drag で
Wiseview を開閉、dock した窓の閉じる button で終える（9 検査）。Files の窓の位置は先に map された窓の数で変わるので、desktop との
差分から窓を見つける（`changed_box()`）。`config-zdesktop-hw.mk` に files とその library を足した。

| run | 結果 |
| --- | --- |
| hw-1 | image に /bin/files が無い（config を直した） |
| hw-2 | Files の glass・タブ・Wiseview は PASS、dock は固定の座標の誤りで FAIL（窓の検出に替えた） |
| hw-3・hw-4 | [BUG-077](../bugs/BUG-077.md): render engine の hang（engine_recover 未実装、device lost）で compositor が停止 |
| hw-5 | 9/9 PASS。画面 `build/ws031-shots/ws075-files-20260928-*.png`（sheet・desktop・files・tab-new・tab-first・tab-closed・docked・wiseview・wiseview-closed・ended） |

Files まで進んだ 4 回のうち 2 回が BUG-077。実機の証拠のみ（QEMU は未実施）。BUG-077 は 2026-09-28 に GPU core の修正で resolved
（修正の後の fix-files1〜3 は 9/9 PASS、`build/ws075-bug077/`）。

## 初期のグラフィックの試験の削除（main の依頼、2026-09-28）

ユーザーの判断（`plan/master.md` の「古いグラフィックの試験の driver」）により、初期の Venus・WSI の host 試験（build できない
ものと Venus だけのもの、参照の無い 2 つ）と `plan/tools/venus-console.c` を削除した。一覧と理由、残したもの（GPU core の host 試験、
QEMU の遠隔の harness、`vkdemo_oracle.py`）は [`plan/ws014/tests/README.md`](../ws014/tests/README.md) の 2026-09-28 の節。
i915 の executor の試験（vkx・vke1・vke2・vkc、gentool、capture の場面、`plan/ws031/tests/run-vk-host-tests.sh`）は残す。

## WS031 から移した Phase

2026-09-27 に移した（WS031 の表に印）。範囲の正本は WS031 の元の Phase の記述（`phase016`・`phase017`・`phase018/phase.md`）。

| WS031 | 移した先 |
| --- | --- |
| ws031-p027（present mode・vsync） | p008 |
| ws031-p030（mip level・layer への描画） | p005 |
| ws031-p031（MRT） | p006 |
| ws031-p034（sampler） | p005 |
| ws031-p035（descriptor 配列・VS の sampled image） | p005 |
| ws031-p038（整数の varying） | p004 |
| ws031-p040（local の配列・構造体） | p004 |
| ws031-p041（OpSwitch・関数呼出し） | p004（今の corpus には無い。client は glslc -O で inline 化される。GL の shader で要るとき） |
| ws031-p044（完了の割込み） | p008 |
| ws031-p045（非同期の実行器） | p008 |
