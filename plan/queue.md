<!-- awesome-plan project=zedbsd record=queue -->

# Queue q499: Windows Venusの実画面

<!-- awesome-plan-current:start -->
Status: active（2026-09-29）
Active Queue: q499-i01 / [ws085-p001](ws085/phase001/phase.md)。Windows版QEMUのVenus表示・速度とSDLタッチ入力を修正するユーザー指示。2026-09-29 に vendor 内のWINQ-EMU QEMU/virglrendererソース直接変更を追加許可。vendorのcommit/pushはユーザーがレビュー後に行う。
Last finished Queue: [q498](history/queue-q498.md)（ws073-p028 cleared。Noct smoke を外し `make` が完走）
Executor: main 1名（q499）。既存のWS074・WS081のworktreeは触らない。
<!-- awesome-plan-current:end -->

Upcoming Work Outlook: [master の Outlook](master.md) を見る（2026-09-28 の夜に整理）。

2026-09-29 q499-i01: Files起動停止を再現し、Windows renderer の IMPORT_RESOURCE のpaddingとinline fd所有権を修正。DLL build、通常make、Files開閉・再起動とTerminal同時起動PASS。詳細はws085-p001。配布DLL反映済み、vendor未commit。

2026-09-29 q499-i02（ユーザーの追加指示、[ws073-p028](ws073/phase028/phase.md) の後処理、cleared）: 旧 userland/noct が package 自動発見に混ざる問題を修正。clean な旧 checkout を削除し、Makefile の検索から旧パスを除外。通常 make PASS、warning なし。正規 Noct は userland/base/noct/。
