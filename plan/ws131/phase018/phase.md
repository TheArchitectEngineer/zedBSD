<!-- awesome-plan project=zedbsd record=ws131-p018 -->

# ws131-p018: Terminal・Notes を新しい API へ（DnD・primary・tablet・fd の監視）

Status: planning（p002 第 2 版はユーザーのレビュー済み（2026-10-03、D7 は確認中）。開始はユーザーの承認と P2 の終了の後に Q1 が指示）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: none
依存: p016 cleared。D8 の単独走行で番号の順
目安: 4〜5h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/terminal/`・`notes/`、`libkeiland/app/`・`ui/`（DnD の受け・primary・tablet・fd の監視）、`plan/ws131/`

## 目的と結果

Terminal（pty の fd・自前の clipboard 808 行と primary 330 行）と Notes（tablet の自前の registry）の自前の registry を除き、libkeiland に DnD の受け・primary・tablet・`kl_app_watch_fd` を足して移す。Terminal の pty は Terminal に OS の依存を残す（D14、2026-10-03 user）: `TIOCSWINSZ` は 3 OS で同じで、`openpty` の header（`<pty.h>`／FreeBSD の `<libutil.h>`）だけを macro の block で切り替え、checker の許可の表に載せる。

## 範囲

1. libkeiland: `kl_window_accept_drops`、primary（自分の選択を自分で paste する時の詰まりの回避を保つ）、tablet の event、窓ごとの repeat の無効、全 motion。
2. Terminal・Notes の menu（413・316）・tabs を宣言的に。scroll の model の移行は WS090 の残りとして WS131 の完了の後に扱う（D9 の決定）。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- 対象の app に旧名（`kui_`・`KUI_`・`keiland_`・`KEILAND_`）と自前の menu・titlebar・glass の結線・main loop が無い（grep）。結線の行数の減りを記録。
- zedBSD: `plan/ws081/tests/run-termtouch.sh`・`run-notestouch.sh`、`plan/ws128/tests/notes-p002.sh`、`demo-s8-s9.sh`、Terminal の clipboard・PRIMARY・drop の guest の手順、boot-test。Linux: 2 app の PNG と Terminal の shell。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: WS128・WS079 と同じ file。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## q807（P1、2026-10-06）: 中断の時点

Terminal から始めた（Notes は P2 の WS175 p007 の統合の後、Q1 の指示）。調べただけで code は未変更のまま、q812（ws090-p020、Mahora の font）の優先の投入で止めた。再開の時の設計の案:

- libkeiland（KL_VERSION 44）: 選択の変化の input `KL_WINDOW_SELECTION`（code は clipboard か primary、pressed は text の有無。Terminal の log `ZTERM CLIPBOARD selection text=`・`ZTERM PRIMARY offer text=` はここから）と、自分の選択かの問い合わせ。
  drop の受け `kl_window_accept_drops(window, types)` と input `KL_WINDOW_DROP_ENTER`・`_LEAVE`・`KL_WINDOW_DROP`、読み出し `kl_window_take_drop`（自分の drag の text は pipe を通さずに渡す、ws035-p093）。
  text の drag の source `kl_window_drag_text` と input `KL_WINDOW_DRAG_DONE`（dropped か）。titlebar の tab `kl_window_set_tabs`（`struct kl_tab_entry`）と input `KL_WINDOW_TAB`（選択・閉じる・新規）。
- Terminal: `kl_app` の loop と `kl_app_watch_fd`（shell の pty）、自前の registry・clipboard.c・primary.c を除き、uri-list を quote した語にする所だけ Terminal に残す。menu.c・tabs.c を表に。pty の header は `__FreeBSD__` の macro の block。
- 試験の log で library の中に移って出せなくなる物: `ZTERM PRIMARY send bytes=`（zdesktop-p100.sh:83）。受け手の `paste received bytes=` で代える案。
- 窓ごとの repeat の無効・全 motion・tablet は Notes の側で要る物（Notes の時に）。
