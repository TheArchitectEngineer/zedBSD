<!-- awesome-plan project=zedbsd record=ws131-p020 -->

# ws131-p020: Files を新しい API へ（toplevel と desktop surface、DnD）

Status: in-progress（q807、P1。実装と host の確認まで済み、T1 の結果待ち）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: none
依存: p018 cleared。旧 ws090-p010 の窓の部分はこの Phase（D9 の決定）
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/files/`、`libkeiland/app/`（desktop surface の役、DnD の source）、`plan/ws131/`

## 目的と結果

Files の自前の窓（`window.c` 1,340 行、toplevel と desktop surface の 2 役）と present（1,243 行）を除き、窓の役に desktop surface と DnD の source を足して移す。Files の xattr は app 側の OS の依存として残す（D14、2026-10-03 user: タグ（`tags.c`）とコピー（`task.c`）は今のまま、FreeBSD の extattr の読み替えは Files の中）。情報の panel（`info.c`）は xattr の名前だけを出す簡単な表示にする。

## 範囲

1. libkeiland: `KL_WINDOW_DESKTOP` の役、`kl_window_start_drag`。
2. Files の窓・present・入力・clipboard・DnD を libkeiland へ、menu（897）・titlebar（453）・glass（152）を宣言的に。CPU の canvas・text の写しの置き換え（旧 ws090-p009）は WS131 の完了の後に WS090 で扱う（D9 の決定）。
3. 情報の panel（`info.c`）の xattr の表示を名前だけの一覧にする（D14）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `plan/tools/files/files-regress.sh`・`host-build.sh`・`files-open.sh`、`plan/ws094/tests/desktop-guest.sh`・`files-desktop-guest.sh`、`plan/ws081/tests/run-filestouch.sh`、`plan/ws127/tests/files-p002.sh`、boot-test、C9（desktop の icon を含む時）。Linux: Files の窓と desktop の PNG。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS127・WS094 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q807（P1、2026-10-06）: 途中（q813 の優先の投入で中断）

- libkeiland（KL_VERSION 46、b755b18d3・6fe75b7ca・この commit）: desktop surface の役（`kl_window_options` の `role`・`token`、`KL_WINDOW_ROLE_DESKTOP`、`kl_window_desktop_place`）、drag and drop の一般化（`kl_window_answer_drop`・`_receive_drop`・`_finish_drop`・`_start_drag`、input `KL_WINDOW_DROP_MOTION`・`_DROP_ACTION`・`_CONTROL_DROP`・`_POPUP_DONE`、`KL_DND_*`）、`kl_window_set_control_value`・`_control_suggestions`・`_focus_control_mode`。
- Files: `window.c`・`window.h`・`dnd.c`・`present.c`・`menu.c`・`titlebar.c`・`glass.c` を libkeiland の上に、`main.c` の調整（repeat・raw の flush を除く）。zedBSD の build と keiland-linux の gcc は exit 0・warning 0、`plan/tools/files/host-build.sh` built。
- 残り（当時）: 他の Files の source の旧名、行数の記録、clang の build、info.c の xattr の表示（D14）、確認と T1 の依頼。→ 下の「q807 の続き」で済み。

## q807 の続き（P1、2026-10-06、q813・q807-i03 の後）

- 旧名: Files の全 source の `kui_`・`keiland_`・`KEILAND_` の API 名 67 か所を `kl_`・`KL_` に、header guard `KEILAND_FILES_*_H` を `FILES_*_H` に。残る `KEILAND_` は API ではない物だけ: `paths.h` の path（`KEILAND_BINDIR`・`_DATADIR`・`_SYSCONFDIR`・`_FONT_BOLD`・`_FONT_FALLBACK_MONO`、他の app と同じ）、artwork の生成物の `KEILAND_MARK_LAYERS`、環境変数の名前 `KEILAND_DESKTOP_TOKEN`（zdesktop との約束）。
- D14: 情報の panel の拡張属性は名前だけ（`fm_attribute.size` と `lgetxattr` を除き、`ui-info.c` は名前の行）。`plan/tools/files/host-model.c` の期待を名前だけに。
- main loop: `window.c` の `kl_app_dispatch`・`kl_app_take` だけ（`poll`・`wl_display_*`・`wl_registry` は Files に無い）。
- 行数（前 0adf0c0fc → 後）: window.c 1391→643、present.c 1243→149、dnd.c 909→384、menu.c 864→612、titlebar.c 481→388、glass.c 152→160、main.c 1624→1608（計 6664→3944）。

### 確認

- build: zedBSD amd64 の libkeiland・files（exit 0、warning 0）、`make keiland-linux` の gcc と clang（exit 0、warning・error 0）、`exports.py --check` OK。
- host: `plan/tools/files/host-build.sh` built、`build/ws071-host/files-model` を worktree の build の中の新しい folder で直に実行 PASS（`host-model.sh` は host の `rm` を含むので P1 は走らせない、2 つ目・3 つ目の volume の試験は未実施）、`plan/ws081/tests/run-filestouch.sh` ok (20)、glass の files-render の描画。
- 未実施: FreeBSD の native build と `native-build-audit.py`、`keiland-os-boundary/check.sh`（Q1）、QEMU（T1）: `files-regress.sh`・`files-open.sh`、`plan/ws094/tests/desktop-guest.sh`・`files-desktop-guest.sh`、`plan/ws127/tests/files-p002.sh`、boot-test、Linux の Files の窓と desktop の PNG。
