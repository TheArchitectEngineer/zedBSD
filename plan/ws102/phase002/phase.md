<!-- awesome-plan project=zedbsd record=ws102-p002 -->

# ws102-p002: keyboard.c の骨組み（角の認識器・開閉・空の panel）

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS102](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws102-keyboard`、branch `wt/ws102`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §2.2・§2.3・§4 の p002 を扱う。

- 右下・左下の角の認識器（pointer と touch）。
- 下端の Wiseview と desktop の swipe から角を除く（D3）。
- 開閉（閉じる key、同じ swipe の toggle、もう一方の角で panel を替える）。
- 空の glass の panel、overlay と scanout、log。

受け入れ:

- 注入の右下の swipe が 10 回とも受け取られ（開くと閉じるが交互）、真上への stroke では 0 回開く（L1 の (a)）。
- 既存の gesture が働く。
- C9 が PASS。

## 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/wayland/keyboard.c`（新規） | 下の 2 つの角（`ZWL_KEYBOARD_ZONE` 28 px）の認識器。corner.c と同じ数値（arm 14 px・1500 ms、±25° の対角、commit 108 px か flick 40 px・0.8 px/ms）。<br>panel: flick は右下、key の辺 `clamp(height/11, 64, 96)`、4×4 の key と 36 px の帯。QWERTY は下端の全幅、高さは `clamp(height×0.38, 260, 420)`。<br>開閉: 閉じる key、同じ角の swipe で toggle、もう一方の角で替える。<br>描画: volume の popup と同じ白いすりガラス、題と × の key。armed の間は、角の panel を薄く示す hint を出す。<br>公開: `zwl_keyboard_button`・`_motion`・`_tick`・`_showing`・`_at`・`_close`・`_draw`。log は `ZWL OSK …` |
| `shell.c` | 次の所に hook を入れた。<br>`zwl_glass_button`・`zwl_glass_edge_button`: corner の直後。Home・Wiseview の下端・desktop の swipe より先。<br>`glass_motion_take`・`zwl_glass_edge_motion`。<br>frame の最後の描画、`zwl_glass_overlay`（全画面の窓の上でも合成する）、静止の判定、tick。<br>`zwl_glass_title_at`: panel と下の角の press は title bar の 2 本指の待ちに入れない |
| `zwl.h`・`glass.h`・`Makefile` | 宣言、`ZWL_KEYBOARD_ZONE`、source の一覧 |

IME の file（`ime.h`・`text-input.c`・`input-method.c`）と `seat.c` は変えていない。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … BUILD=build/ws102-amd64 build/ws102-amd64/bin/wayland`（と libkeiland・libwayland-client・libtruetype・libvulkan・wltest） | rc 0、warning 0 |
| style | `plan/tools/style-check.py keyboard.c shell.c`、`git diff --check` | 0 件 |
| guest（QEMU の Venus、pen image `build/main-pen/hdd-image.img` の複写） | `plan/ws102/tests/osk-guest.sh build/ws102-shots/p002 install start pointer edges touch` | PASS（25 の確認、下） |
| 回帰: 右上の角・Home・Wiseview（WS079-p010） | `plan/ws079/tests/zdesktop-p010.sh build/ws102-amd64 …` | PASS |
| 回帰: WS099 の C9 | ws099 の criteria image の複写に、この compositor を入れて `criteria.sh … C9` | 10 本すべて PASS |
| boot test | `plan/tools/boot-test.sh build/ws102-c9.img` | PASS（`build/ws102-boot-test/login.png`） |

guest で確かめたこと（判定は log と画面。console・serial は読んでいない）:

- pointer:
  - 右下の角の swipe で、`press corner=flick source=pointer`・`armed`・`commit`・`open kind=flick x=950 y=434 width=318 height=354` が出た（`flick-open.png`）。
  - 閉じる key で `close reason=key` が出た。
  - 同じ swipe を 2 回で、開いてから `close reason=gesture` になった。
  - 左下の swipe で `open kind=qwerty x=12 y=484 width=1256 height=304` が出た（`qwerty-open.png`）。その後の右下の swipe で flick に替わった。
- D3（衝突）:
  - 右下の角から真上への stroke では `cancel` になり、keyboard も Wiseview も開かない。
  - 下端の中央からの swipe で Wiseview が開き（`wiseview.png`）、keyboard の press は増えない。
  - 左端の角のすぐ上からの swipe で desktop が替わり、keyboard の press は増えない。
- touch（`touchinject`）:
  - 右下からの 10 回の swipe で、`source=touch` の press 10・commit 10、開く 5・閉じる 5（交互）になった。
  - 角から真上への 10 回の stroke では、開くのは 0 回だった。

## 見つけたこと・制限

- 閉じる key の淡い円は、白い glass の上ではほとんど見えない（× は見える）。p003 の key の描画で合わせる。
- lock・App Home・Wiseview が開いたときに panel を閉じる処理は p005。今は、開いている間の Home や Wiseview の上にも panel が残る。
- panel の上の press は panel が取り、窓には行かない（閉じる key 以外はまだ何もしない）。
- 1920x1080 の配置の確認は p005。
- 実機、Windows の上の QEMU は未実施。

## Resume point

2026-09-30: cleared。次は p003（flick の表と key の描画）。
