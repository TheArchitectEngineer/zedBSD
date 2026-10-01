<!-- awesome-plan project=zedbsd record=ws078-p004 -->

# ws078-p004: 見える文字列（boot の logo・greeter・lock・banner 等）

Status: incomplete（ws.md の Phase の表のとおり。この file は 2026-10-01 に手順のために作った。それまでの記録は ws.md の表の p004 の行が正）
Disposition: normal
Parent: [WS078](../ws.md)
Queue: なし
依存: p001（planning のまま。棚卸しは ws.md と [guide.md](../guide.md) §2.3 で代わった）

## これまで（ws.md の表の p004 の行の要約。2026-09-28）

- boot の logo を Kei の印・語・「powered by zedBSD」に描き直した（b3f5c5f9、`plan/ws035/kei-identity-design.md` の段階 1）。全画面の起動画面は ws035-p107（段階 2）。
- File Manager の help・terminal の About・system bar の名前・tool の `--version` の「(Kei)」・fetch と browser の User-Agent・EGL・GLES・GLX・X server の vendor・
  installer の文言と SVG・service console・既定の hostname `kei` を Kei に。user の data の path、menu の label、試験の process 名の pattern も。
- Venus: zdesktop-p062 PASS、files-p012 PASS（lean image）、system bar に Kei。graphical な起動での表示の確認は未実施。
- 残し（決定）: uname の sysname と version、disk の format の印、GPT の partition 名、kernel と loader の文言、source の copyright の header。

## 残り（2026-10-01 の棚卸し、guide.md §2.3 A）

| 所 | 行 | 今 |
| --- | --- | --- |
| `userland/desktop/browser/data/start.html` | 5・26・27・33・34・59 | 「zedBSD Browser」（title と見出し。窓の title にも出る）、「The Web browser of the zedBSD desktop」、「written for zedBSD」、「in a zdesktop window」、footer「zedBSD · browser」 |
| `plan/ws074/tests/browser-start.sh` | 62 | `expect_navigate "path=$pages/start.html title=zedBSD Browser"`（上と一緒に直す。WS074 の file なので main が当てる） |
| `docs/reference/kernel-boot-parameters.md` | 325 | `login=graphical  the greeter (zdesktop --greeter) on the display` |
| `include/libc/wayland/API-PROVENANCE.md` | 69〜71 | `zed-gpu-buffer-v1-client-protocol.h`、「Other zdesktop clients」「zdesktop extension」。**WS104 p001 の後**は `userland/desktop/keiland/wayland/API-PROVENANCE.md`。file 名の行は p008（提案）と一緒 |

範囲の外: 注釈（p007 提案）、make の変数と header の file 名（p008 提案）、`TERM=zed`（p009 提案）、retro の名前（ユーザーの判断待ち）、試験の log の印（当面は変えない）。

## 手順（2026-10-01 追記）

1. 始める前に main が、WS074 の worktree が `userland/desktop/browser/data/start.html` と `plan/ws074/tests/browser-start.sh` を変えていないことを確かめる
   （`git worktree list`、`git -C <worktree> diff main --stat -- userland/desktop/browser/data plan/ws074/tests/browser-start.sh`）。
2. `start.html` を直す（ユーザーの判断 0: 画面には機能の名前を出し、Keiland は出さない。OS の名前は Kei）。既定の文言:
   - 5・26 行: `Browser`（title と h1）。
   - 27 行: `The Web browser of Kei, with an engine of its own.`
   - 33〜34 行: `This page is drawn by <code>browser</code>: its HTML parser, style engine and layout are written for Kei,` / `and the page is painted with Vulkan in a window of the desktop.</p>`
   - 59 行: `Kei &middot; browser`
   文言は既定で、別の言い方が良ければ main が変えてよい（委ねられた範囲）。行の数は変えない。
3. `plan/ws074/tests/browser-start.sh:62` の `title=zedBSD Browser` を `title=Browser` にする（main）。他に「zedBSD Browser」を待つ試験が無いことを
   `git grep -n "zedBSD Browser" -- plan ':!plan/history' userland` で確かめる（2026-10-01: この 1 行と start.html だけ）。
4. `docs/reference/kernel-boot-parameters.md:325` を `login=graphical  the greeter (/bin/wayland --greeter) on the display` にする。
5. `API-PROVENANCE.md` の 70〜71 行の「zdesktop clients」「zdesktop extension」を「clients of the desktop's compositor」「the compositor's own extension」にする
   （WS104 p001 の後なら新しい path。69 行の file 名は p008 で直す）。
6. 確かめ（guide.md §5.2、`<W>` = `ws078-p004`）: browser の image で `browser-start.sh` を流し PASS、`start.png` で title と見出しを見る。基準の image で
   `session.png`（system bar）と `about.png`（Settings の About）を撮る。boot test。
7. 実機（ユーザー、guide.md §6 の H1〜H5）: 次の demo の image でユーザーが見る。結果をこの file に「実機」として分けて書く。未実施なら「未実施」と書く。

## 完了の条件

- `git grep -n -iE 'zedbsd|zdesktop' -- userland/desktop/browser/data docs/reference/kernel-boot-parameters.md` が 0 行。
- `browser-start: PASS`、`start.png`・`session.png`・`about.png` に「zedBSD」「zdesktop」「Keiland」が無い（About の副題「powered by zedBSD」は残す決定）。PNG をユーザーに見せる。
- `boot-test: PASS`（`OUTPUT=build/ws078-p004/boot plan/tools/boot-test.sh build/ws078-p004-criteria/hdd-image.img`）。
- 実機の H1〜H5 はユーザーの確かめ。未実施でも p004 は QEMU の分で cleared にしてよいかは main が決める（実機の分を残りとして書く）。
