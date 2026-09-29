<!-- awesome-plan project=zedbsd record=queue -->

# Queue q499: Windows Venusの実画面

<!-- awesome-plan-current:start -->
Status: active（2026-09-29）
Active Queue: q499-i01 / [ws085-p001](ws085/phase001/phase.md)。Windows版QEMUのVenus表示・速度とSDLタッチ入力を修正するユーザー指示。2026-09-29 に vendor 内のWINQ-EMU QEMU/virglrendererソース直接変更を追加許可。vendorのcommit/pushはユーザーがレビュー後に行う。
Last finished Queue: [q498](history/queue-q498.md)（ws073-p028 cleared。Noct smoke を外し `make` が完走）
Executor: main（計画と merge）+ サブエージェント N=9（2026-09-29 夕〜、19 時に 6 → 8、19 時 50 分に 9）: WS074 `.claude/worktrees/agent-a346b6e810eda5cb6`、WS081 `.claude/worktrees/agent-abe8d4ea8794ae4fc`、WS073 `.claude/worktrees/ws073-bugs`（wt/ws073）、WS086 `ws086-ls`（wt/ws086）、WS087 `ws087-sh`（wt/ws087）、WS088 `ws088-nightly`（wt/ws088）、WS075 `ws075-i915`（wt/ws075、実機は lock の下で Phase の終わりだけ）、WS035 `ws035-keiland`（wt/ws035、F-048）。q499（ws085-p001）は main が取り込み済み、Windows の確認はユーザー WS089 `ws089-settings`（wt/ws089、設定のアプリ）。 20 時: WS088 のエージェントは BUG-105（ws073-p032）の後 WS080 p001 へ（`ws080-coff`、wt/ws080）、WS081 のエージェントは BUG-106 へ。
<!-- awesome-plan-current:end -->

Upcoming Work Outlook: [master の Outlook](master.md) を見る（2026-09-28 の夜に整理）。

2026-09-29 q499-i01: Files起動停止を再現し、Windows renderer の IMPORT_RESOURCE のpaddingとinline fd所有権を修正。DLL build、通常make、Files開閉・再起動とTerminal同時起動PASS。詳細はws085-p001。配布DLL反映済み、vendor未commit。

2026-09-29 q499-i02（ユーザーの追加指示、[ws073-p028](ws073/phase028/phase.md) の後処理、cleared）: 旧 userland/noct が package 自動発見に混ざる問題を修正。clean な旧 checkout を削除し、Makefile の検索から旧パスを除外。通常 make PASS、warning なし。正規 Noct は userland/base/noct/。
