<!-- awesome-plan project=zedbsd record=queue -->

# Queue q499: Windows Venusの実画面

<!-- awesome-plan-current:start -->
Status: active（2026-09-29）
Active Queue: q499-i01 / [ws085-p001](ws085/phase001/phase.md)。Windows版QEMUのVenus表示・速度とSDLタッチ入力を修正するユーザー指示。2026-09-29 に vendor 内のWINQ-EMU QEMU/virglrendererソース直接変更を追加許可。vendorのcommit/pushはユーザーがレビュー後に行う。
Last finished Queue: [q498](history/queue-q498.md)（ws073-p028 cleared。Noct smoke を外し `make` が完走）
Executor: main（計画と merge）+ サブエージェント N=9（2026-09-29 夕〜、19 時に 6 → 8、19 時 50 分に 9）: WS074 `.claude/worktrees/agent-a346b6e810eda5cb6`、WS081 `.claude/worktrees/agent-abe8d4ea8794ae4fc`、WS073 `.claude/worktrees/ws073-bugs`（wt/ws073）、WS086 `ws086-ls`（wt/ws086）、WS087 `ws087-sh`（wt/ws087）、WS088 `ws088-nightly`（wt/ws088）、WS075 `ws075-i915`（wt/ws075、実機は lock の下で Phase の終わりだけ）、WS035 `ws035-keiland`（wt/ws035、F-048）。q499（ws085-p001）は main が取り込み済み、Windows の確認はユーザー WS089 `ws089-settings`（wt/ws089、設定のアプリ）。 20 時: WS088 のエージェントは BUG-105（ws073-p032）の後 WS080 p001 へ（`ws080-coff`、wt/ws080）、WS081 のエージェントは BUG-106 へ。 20 時 5 分: WS086 完了、そのエージェントは WS074 p031 へ（`ws074-dom`、wt/ws074-dom）。 20 時 20 分: WS080 p001（設計）の後そのエージェントは WS091 画像 viewer（`ws091-imageview`、wt/ws091）、WS087 のエージェントは p004 の後 WS092 text editor（`ws092-editor`、wt/ws092）、WS073 のエージェントは BUG-107（socket の上限）。 20 時 50 分: WS073 の bug（BUG-102・104・051・105・107・108・031）を終えたエージェントは BUG-106 の再確認の後 WS095 IME の p001（`ws095-ime`、wt/ws095）。 21 時 30 分: effort の切り替え（ユーザー）: Mid（phase-runner-mid）＝WS074 の JS（agent-a346…）と DOM（ws074-dom）、WS089、WS091、WS092、WS095 を新しいエージェントに引き継いだ。High（phase-runner）＝WS075 の p018（ws075-i915）と p020 RPS（ws075-rps）、WS035 の BUG-110（ws035-keiland）。
<!-- awesome-plan-current:end -->

Upcoming Work Outlook: [master の Outlook](master.md) を見る（2026-09-28 の夜に整理）。

2026-09-29 q499-i01: Files起動停止を再現し、Windows renderer の IMPORT_RESOURCE のpaddingとinline fd所有権を修正。DLL build、通常make、Files開閉・再起動とTerminal同時起動PASS。詳細はws085-p001。配布DLL反映済み、vendor未commit。

2026-09-29 q499-i02（ユーザーの追加指示、[ws073-p028](ws073/phase028/phase.md) の後処理、cleared）: 旧 userland/noct が package 自動発見に混ざる問題を修正。clean な旧 checkout を削除し、Makefile の検索から旧パスを除外。通常 make PASS、warning なし。正規 Noct は userland/base/noct/。
