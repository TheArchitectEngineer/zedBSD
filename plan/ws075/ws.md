<!-- awesome-plan project=zedbsd record=ws075 -->

# WS075: i915 の高度化（今のデスクトップとグラフィックスを 5330 の i915 のネイティブ実行器で）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: L1・L2 は達成（C6 中央値 67.3 ms、p027・p029）、MRT・GS などの実行器の範囲は WS031（2026-10-27 00:09 ユーザー「WS031をcompletedにしてください。」）と同じ扱い。描画の高速化は 2026-09-30 ユーザーの指示でラップアップ、シェーダは 2026-10-07 ユーザー「シェーダはやることがないときに取り組んでほしいです。」）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

今の desktop と graphics を Dell Latitude 5330（Alder Lake-P、`8086:46a8`）の i915 のネイティブ Vulkan 実行器（WS031）で動かす: zdesktop（glass、backdrop のぼかし、tab、System Menu）と Vulkan の client（files、terminal、mview）、EGL/GLES 2・3（egltest）と X11 の GLX の GL（固定機能、GL 3.0〜3.2 の glxtest）。そのための i915 の compiler・実行器・driver の不足（F-023、F-022 の残り）と、性能と安定（完了の割込み、非同期の実行器、vsync、BUG-056・BUG-057）。HDMI の主出力とデモの image（[hdmi-main-output.md](hdmi-main-output.md)、`demo/`）も扱った。作業の手引きは [guide.md](guide.md)。

## ユーザーの決定

- 2026-09-27: 「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」（WS031 の planning の Phase のうち同じ範囲を移した）
- p019（main の中継）: 「MAILBOX・IMMEDIATE はデモの後に回す」（Future Work F-057）。
- 2026-09-30 朝（Q1 経由）: デモ critical の WS は「広く浅く」。段（L1・L2・L3…）ごとに数値目標を持つ小さな Phase に分ける。
- 2026-09-30（main 経由）: 「分岐の中の計算を飛ばす、にします」（p023）。
- 2026-09-30（すりガラス、p028 の結果を見て）: 以前の発言「コンポジターが遅いのは、あきらかに、すりガラスエフェクトのせいでは。おもいきってやめてみるのもいいかも。」、答え「blur をやめる」。同日の追加「blurですが、アプリごとに有効・無効を切り替えられるようにしましょう。Settingsは常用ではないので有効でOK、Filesやスクリーンキーボードでは比較して見た目で決めたいです。常用アプリではパフォーマンスを優先します。でも、スクリーンキーボードって、若干ダサいんですよね。だからそれを緩和するために、多少パフォーマンスを犠牲にしてもいいとは思います。」。見比べの後、Files と keyboard の panel は「無効（壁紙だけ）」。blur が有効なのは Settings だけ。
- 2026-09-30: 「いったん描画の高速化はラップアップして、優先度が高いWSやPhaseの中では優先順位を下げ、機能性の実装にフォーカスしましょう。」（p031 を中断）
- 2026-10-07: 「シェーダはやることがないときに取り組んでほしいです。」
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」（p032）
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- 調査（p001: shader の survey 122 module、client ごとの command、実機の zdesktop 6/6）と desktop の新しい機能の実機の確認（p002: home 4/4・menu 11/11・x11 6/6）。
- 実行器: primitive topology・線・index の型・vkFreeDescriptorSets（p003、vkx 9/9）、texture の種類・cube・配列・3D・mip・layer への描画・sampler の compare・descriptor 配列（p005、egltest の 7 場面 failures 0）、MRT・occlusion query・storage buffer・stencil・multisample（p006 の増分 1〜5）。
- compiler（GLES 2 の核、p004: vke2 17/17 ほか）。geometry shader（p007a の a1〜a4、p007b の b1〜b4）は実装と host 試験まで。
- 安定と性能: 完了待ちの割込み（p008）、BUG-056・057・094（p009、App Home の 8 app、20 分の負荷で failed 0）、FIFO の present の先行（p019）、RPS（p020）、分岐の中の send と ALU を飛ぶ（p021・p023）、C6 の物差し（p024）、BUG-117（p025）、frame の長さの分析（p026）。
- 段: L1（p027: C6 中央値 91.3 ms、stress 100 回で停止 0）、L2（p029: blur を窓ごと・既定は無効、C6 中央値 67.3 ms）、L3 の計測（p030）。
- HDMI の主出力（p011〜p013: `display=hdmi|auto`・`display.mode`、デモの image で splash・greeter・login・session・30 分の表示）、BUG-091（p014）、BUG-085 の再試験と BUG-094 の原因（p015）、lease の替わり目で HDMI を点けたまま（p016）、BUG-120（p032）。
- 実機の証拠は 5330 の passthrough（`flock /tmp/i915-hw.lock`）、QEMU（Venus）の証拠とは分けて各 Phase に記録した。素の 5330 の確認は段ごとに未実施の物がある。

## 制限・移管

- L3（C6 中央値 50 ms）は未達。p031（文字の draw をまとめる）は build まで済み・未検証で中断（2026-09-30 ユーザーの指示）。p018（非同期の実行器）・p022（scoreboard の緩和）は効かず uncleared。draw ごとの pipeline の停止の削減は候補のまま。
- p006 の増分 6（実機）、p007a の a5・p007b の b5（gl32 の場面と実機）は未実施。GL 3.2 の geometry shader は実機の確認なし。
- p017（BUG-058 の再試験）は 6 回で再現せず、原因は未特定。GPU の object の枠は BUG-120（p032 で 2,048 まで）。
- MAILBOX・IMMEDIATE は F-057。規約の全文と統合回帰（p010）は WS の外の一括の見直しへ。
- 試験（`tests/`）は 5330 の i915 の実機の試験と計測の道具として `plan/tools/` へ移す（Q1 の OK の後）。デモの image の config（`demo/`）は `plan/uat/config-uat.mk`・`tools/release/license-inventory.py` が使うので、この directory に残す。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws075-p001 | 調査: shader の不足・client の command・実機の zdesktop | cleared（2026-09-27） |
| ws075-p002 | desktop の新しい機能を実機で | cleared（2026-09-27） |
| ws075-p003 | 実行器: primitive topology・線・index・vkFreeDescriptorSets | cleared（2026-09-27） |
| ws075-p004 | compiler（GLES 2 の核） | cleared（2026-09-27） |
| ws075-p005 | texture の種類と実行器の image・sampler・descriptor 配列 | cleared（2026-09-28） |
| ws075-p006 | MRT・occlusion query・texel buffer・storage buffer・stencil・multisample | in-progress のまま WS を閉じた（増分 1〜5 済み、増分 6 は実機未実施） |
| ws075-p007 | GL 3.2 の stage（geometry shader、layered、PrimitiveId）の設計 | planning のまま WS を閉じた（設計と review の反映済み） |
| ws075-p007a | geometry shader の compiler（a1〜a5） | in-progress のまま WS を閉じた（a1〜a4 の実装と host 試験） |
| ws075-p007b | geometry shader の実行器（b1〜b5） | blocked（UAT 待ち）のまま WS を閉じた（b1〜b4 の実装と host 試験、b5 は未） |
| ws075-p008 | 性能 1: 完了待ちを割込みへ | cleared（2026-09-29） |
| ws075-p009 | 安定: BUG-056・BUG-057 ほか | cleared（2026-09-29） |
| ws075-p010 | 規約の全文との照合、統合回帰 | planning のまま WS を閉じた（WS の外の一括の見直しへ。Phase の dir は作られていない） |
| ws075-p011 | HDMI の主出力の実機の事前調査（H1） | cleared（2026-09-28） |
| ws075-p012 | HDMI の主出力（H2） | cleared（2026-09-28） |
| ws075-p013 | デモの形での確認（H4） | cleared（2026-09-28） |
| ws075-p014 | BUG-091 の修正 | cleared（2026-09-28） |
| ws075-p015 | BUG-085 の再試験、BUG-094 の原因 | cleared（2026-09-28） |
| ws075-p016 | lease の替わり目で HDMI を点けたまま | cleared（2026-09-28） |
| ws075-p017 | BUG-058 の再試験 | uncleared（再現せず、原因は未特定） |
| ws075-p018 | 性能 2: 非同期の実行器 | uncleared（計測で効かない見込み、未実装） |
| ws075-p019 | 性能 3: present mode と vsync | cleared（2026-09-29、MAILBOX・IMMEDIATE は F-057） |
| ws075-p020 | RPS の割込みと boost | cleared（2026-09-29） |
| ws075-p021 | 走らない block を飛ぶ | cleared（2026-09-29） |
| ws075-p022 | encoder の scoreboard の緩和 | uncleared（patch は保留） |
| ws075-p023 | 分岐の中の ALU を飛ぶ | cleared（2026-09-30） |
| ws075-p024 | C6 の物差し | cleared（2026-09-30） |
| ws075-p025 | BUG-117 | cleared（2026-09-30） |
| ws075-p026 | compositor の frame の長さの分析 | cleared（2026-09-30） |
| ws075-p027 | L1 の判定 | cleared（2026-09-30） |
| ws075-p028 | L2 の計測 | cleared（2026-09-30） |
| ws075-p029 | L2 の実装（blur を窓ごと） | cleared（2026-09-30） |
| ws075-p030 | L3 の計測 | cleared（2026-09-30） |
| ws075-p031 | L3: 文字の draw をまとめる | uncleared（中断、2026-09-30 ユーザーの指示） |
| ws075-p032 | BUG-120: GT の object の表を伸ばす | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…」） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws075/` にある（p031 の実装の差分は `phase031/exp/`）。Queue の履歴は [plan/history](../history/index.md)。
