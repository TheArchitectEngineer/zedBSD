<!-- awesome-plan project=zedbsd record=ws168 -->

# WS168: プレビュー（縮小表示）を作る隔離された専用の command

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」。旧: incomplete）
Primary Milestone: MG006
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-05）「プレビューの作成を行う専用コマンドを作成。このコマンドはchrootと、そのほかの可能な限りのjailを適用して実行する。…入力ファイルと出力ファイルはfd 0,1でオープンされた状態で起動し、…このプロセスはそのほかのファイルをオープンできない。ネットワークも利用できない。forkも不能。純粋にfd 0,1の入出力と計算のみ行える。」ベータ2。

## 結果（2026-10-13 完了）

- 設計（p001）: 子を最初から sandbox の中に起こす system call `sandbox_spawn`（UAPI `include/uapi/sandbox.h`、HAL は不変）、許す操作の集合（既存の fd の read・write・close・fstat、匿名の memory、exit など。open・socket・fork・exec などは断る）、静的 link の `keiland-preview`、呼び出し側の時間・memory の上限。H1〜H7 はユーザーが 2026-10-05 夜に全部推奨どおりに決定（isatty は TCGETS だけ ENOTTY、2026-10-06）。
- kernel（p002）: `sandbox_spawn`・libc の wrapper・`userland/tests/sandboxtest`（QEMU の T1-245 など PASS）。
- command（p003）: `userland/desktop/preview/`（`keiland-preview`、静的 link、zedBSD は sandbox_spawn、Linux は seccomp）。
- 呼び出し側（p004）: Files・Settings が縮小画像を自分で decode せず keiland-preview の子に作らせる（FreeBSD は in-process のまま）。QEMU は T1-395 で PASS。
- 回帰: `tests/scenarios/apps/files/thumbnails.md`（AAT、`plan/tools/aat/scenarios/helpers_preview.py`）、`plan/tools/files/host-model.c`・`host-build.sh` の thumbnail の項。

## 制限・移管

- 磨きの残り（2 つ同時、失敗の印、fuzz など）は WS177 の backlog-p2 の WS168 の行（ws177-p010 が扱った）。
- FreeBSD は sandbox が無く in-process。実機の UAT は残りの UAT の扱い。全文規約の見直しはベータ3。
- 設計の本文は [preview-sandbox-design.md](preview-sandbox-design.md)（p001 の phase.md を 2026-10-13 に移した）。source の注（`include/uapi/sandbox.h`、`userland/desktop/preview/`、Files の thumb）から参照されるので残す。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws168-p001 | 要件と設計（kernel の `sandbox_spawn` の UAPI、keiland-preview、呼び出し側、Linux・FreeBSD） | cleared（2026-10-08） |
| ws168-p002 | kernel の `sandbox_spawn`・libc の wrapper・sandboxtest | cleared（2026-10-06） |
| ws168-p003 | `keiland-preview`（静的 link、Linux の seccomp）と host 試験 | cleared（2026-10-08、T1-395） |
| ws168-p004 | Files・Settings を keiland-preview の子に切り替え（FreeBSD は in-process のまま） | cleared（2026-10-08、T1-395） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
