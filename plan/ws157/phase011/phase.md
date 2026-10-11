# ws157-p011: メディア削除の最終全文規約/検証/統合

Parent: [../ws.md](../ws.md)
Status: in-progress
Disposition: normal
Queue: [photos-delete-20261010](../codex-queue.md)
Approval: p010と同じユーザーの限定実装指示。以前のmain統合承認を今回にも適用する想定だったが、自動承認reviewが適用不可としたため今回のmain統合は承認待ち。
Scope / Criteria: p010の全変更sourceをC全文/Guardrail/automationでmanual確認、edited-range format/style-check/diff-check、短いhostとnamed build warning0、mainへの統合/clean read-back。WS全体のcompletedを主張しない。
Dependency: p010の実装/host/build出力。

## 2026-10-10 photos-delete-i02 checkpoint

p010出力を確認し実行。全18 C/headerとgenerated exports.mapを全文規約/境界でmanual review、edited-range format、baseline比較、新規style finding 0。named zedBSD4成果物/Linux2成果物build exit0 warning0。[証拠](../tests/photos-delete-verification-20261010.md)。main統合とclean read-backを残す。

## 2026-10-10 main統合の判断待ち

source/証拠commit `4a8856cc5`（WIP）、own worktree clean。mainのclean/base `39ebb1b06`を確認した後、`git merge --ff-only 4a8856cc5`を依頼したが実行前に自動承認reviewが拒否した。理由は「以前の承認は別taskのもので、今回のPhotos削除に対するmain統合承認が確認できない」。mainは変更していない。ユーザーへ理由と具体的commitを示し今回の統合をasync質問中。迂回しない。承認でmain統合→clean read-back、またはbranch保持の指定に従ってscopeを整理する。

## 2026-10-11 q924（P1）: agent/p1 への統合と再確認

- Queue: q924（P1 lane、2026-10-11 ユーザーの承認「・未統合部分の統合（WS157）」）。上の「main統合の判断待ち」はこの承認で解消し、merge は Q1 が行う。
- 統合: `git merge --no-ff --no-commit 305ab43c1`（4a8856cc5 を含む）を agent/p1（base main `ba06b7b40`）へ。文字の衝突は無し。main は base `39ebb1b06` 以後 keiland.h・kl-system-protocol.h・system/・media・mediastorage・photos・media-library.c を変えていない（`git log 39ebb1b06..ba06b7b40 -- <path>` が空）ので、版は KL_VERSION 83→84、KL_SYSTEM_MANAGER_VERSION 30→31 のまま衝突しない。
- 旧 caller の互換（読んで確認）: 追加は `kl_system_media_remove_ids` の公開の関数と export だけで、既存の関数・構造体は不変。manager 29・30 の compositor には libkeiland が media を version 1 で作り（`wl_proxy_marshal_constructor_versioned`）、remove は opcode を送らず ENOTSUP。新しい compositor は manager 30 以下を bind した旧 libkeiland に media v1 を作り、v1 の remove は EPROTO。
- 規約の直し（動作は不変）: `kl_system_media_remove_ids` の段落と comment を分け、`list[used++]` を 2 文に。`media_database_remove` の list の走査の `for` を 3 行に。
- named build（自分の worktree、warning 0・exit 0）: `make -j16 BUILD=build/ws157-p011 ZEDBSD_CONFIG=config/current-uat.mk -o build/amd64/sysroot/.zedbsd-sysroot-complete -o build/llvm/.zedbsd-install-23.1.0-zedbsd8 build/ws157-p011/dynamic/libkeiland.so build/ws157-p011/bin/{mediastorage,photos,wayland}`、Linux は `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws157-p011-linux …/lib/libkeiland.so …/bin/mediastorage`。
- host 試験（上の 4 つの probe を SHA256 一致で複写し、自分の build で再実行、ASan/UBSan）: CLI の削除（Pillow の fixture）PASS、rollback（metadata.db の置き換えの EIO で file を戻す）PASS、worker PASS、wire の manager 31（remove 成功）と manager 29（ENOTSUP）PASS。
- `plan/tools/style-check.py`（18 file）: keiland.h の既存の 24 件だけ（base `ba06b7b40` の keiland.h も 24 件、新規 0）。`git diff --check` PASS、`exports.py --check` PASS。`keiland-os-boundary/check.sh` は A4 が FAIL だが、統合と無関係の既存の `userland/desktop/phone/main.c:1302` の posix_spawn（main の `e654733f1` 由来）。
- 未実施: QEMU・実機の GUI（選択→Delete…→Cancel／Delete、他の window の更新）、image の build。Status は Q1 の merge まで in-progress。
