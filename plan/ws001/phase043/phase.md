<!-- awesome-plan project=zedbsd record=ws001-p043 -->

# ws001-p043: tabs の XCU の形と幅（台帳 #117）

Status: in-progress（2026-10-07 q834 P2: 実装、host の比較 55/55、zedBSD の build warning 0。guest は p041〜p043 をまとめて T1 へ）
Parent: [WS001](../ws.md)
Queue: q834（2026-10-07、P2）

## 範囲（Q1 の ACK 2026-10-07、正常系）

1. -0（stop を消すだけ）、2. -1・-2（64 個の上限で失敗していた）、3. column 1 の stop で `\E[0C` を送って全部が 1 つずれる不具合、
4. 幅（COLUMNS、TIOCGWINSZ、terminfo の cols。幅の外の stop は設けない）、5. TERM が無い時の既定（vt100）、6. 複数の operand を 1 つの list に。

## 実装

`userland/base/tabs/main.c` を書き直した（coding-style の全文、style-check 0）。

- 予め決まった形は表（`tabs_forms`）、-0〜-9 は一様の間隔（-0 は消すだけ）。後の option が勝つ。
- stop の配列は幅の大きさ（最大 4096 桁）。column 1 と幅の外の stop は設けない（1 には cursor がもう居る。VT100 は `\E[0C` を 1 桁の移動とみなすので、
  前は `tabs 1,10,20` が 2・11・21 に設けていた）。
- 幅: COLUMNS（ncurses と同じ）、標準出力の端末の幅、entry の cols、80 の順。
- TERM が無いか空なら vt100 の entry（XCU の「unspecified default」）。
- list: comma か blank で区切り、2 つめから `+n`。operand が複数でも 1 つの list。昇順でない・0・先頭の `+n`・数でない物は usage で 2（XCU。ncurses は受ける）。

## 確かめ（2026-10-07）

- host: `python3 plan/ws001/tests/tabs-stops.py build/ws001-p043/tabs` → 55/55。ncurses 6.5 の tabs と、VT100 として解いた stop の組（column 1 を除く）と
  終了状態を 25 の形 × 幅 80・120 で比べ、わざと違う 5 つ（幅の外、昇順でない、0、先頭の `+n`、数でない）は zedBSD の期待で確かめる。
  host の build は `include/libc/terminfo.h` だけを別の directory に写して include する（zedBSD の .zti の reader）。
- zedBSD: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws001-p043z build/ws001-p043z/bin/tabs`（find・ls も）-Werror で warning 0。
- guest: 未実施。`plan/ws001/tests/pinned/guest.sh` に「find -exec + splits at the argument limit」（16 KiB の {ARG_MAX} で 600 個の長い名前が 2 回以上）と
  「tabs on the vt100 entry」を足した。T1 にまとめて依頼する。

## 積み残し（backlog へ）

+m の左の余白（XCU 以前の形）、tbc・hts の無い端末（今は「does not support tab programming」で 1）、端末への書き込みの途中の失敗。
