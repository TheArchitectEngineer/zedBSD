<!-- awesome-plan project=zedbsd record=ws131-p014 -->

# ws131-p014: 旧 libkeiland の名前を kl_・KL_ に

Status: in-progress（q675、P2 generation10、2026-10-04。実装・zedBSD と Linux gcc の build・一部の host 試験まで。ユーザーの指示のラップアップで中断、下の Resume）
Disposition: normal
Parent: [WS131](../ws.md)、計画の正本 [design.md](../design.md)
Queue: q675
依存: p013 cleared、p011 cleared（旧 network・audio・preferences を除いた後）。判断 D16
目安: 3〜4h（1 Queue）。実行者: Q1 が割り当てる（high）
所有 path: `userland/desktop/libkeiland/`、`userland/desktop/keiland/keiland.h`、共有の source の内部の名前（`userland/desktop/picture/`・`artwork/` の `keiland_*`）、`plan/ws131/`

## 目的と結果

libkeiland の公開の名前（`keiland_*`・`KEILAND_*`、[rename-map.md](../rename-map.md) の 266、`paths.h` の install の path の macro を除く）を `kl_`・`KL_` にし、`keiland.h` の終わりに旧名の互換の macro の block を置く。共有の source の内部の `keiland_` も `kl_` に（D16）。ABI は変えてよく版は上げない（`KL_VERSION` は 21）。

## 範囲

1. rename-map の表から機械的に改名。`KL_EDIT_*`・`KL_KEYBOARD_INSET_*` を一本化。
2. 互換の macro の block（`KL_COMPAT` が無い時、p023 で除く）。
3. exports.map を header から作り直す。
4. 利用者（app・xserver・probe）は互換の macro で無変更。

## 受け入れ

- 3 OS の build（zedBSD の amd64 は exit 0・自前の warning 0、`make keiland-linux` の gcc と clang、FreeBSD の native build と `native-build-audit.py`）と `plan/tools/keiland-os-boundary/check.sh` PASS。
- `nm -D libkeiland.so` に `keiland_` が無い（protocol の識別子は D15 で p021）。B4 PASS。
- p012 と同じ回帰に、xserver・menu-probe・titlebar-probe の build と、titlebar の probe の手順（`plan/tools/titlebar/`）。

## 検証の方法

[zedbsd-commands.md](../../tools/keiland-linux/zedbsd-commands.md)、[keiland-linux/README.md](../../tools/keiland-linux/README.md)、[keiland-freebsd/README.md](../../tools/keiland-freebsd/README.md)。QEMU の console・serial の log で判定しない（不具合は gdbstub・monitor・QMP）。build は自分の `BUILD=build/ws131-pNNN/`、共有の `build/` を消さない。FreeBSD の起動の確認は判断 D11 の範囲（passthrough なし）。QEMU と実機の証拠を分ける。

## 衝突・危険・rollback

- 衝突: 全 app の compile に触れる（source は無変更）。D8 の単独走行の間は他の担当が動かない。 順は Q1 が決める（design.md §7.2）。
- rollback: 前へ直すのを基本にし、後ろに同じ file の変更が無い時だけ統合の commit を revert する（design.md §7.4）。

## Resume

依存の Phase の cleared と main への統合、関係する判断の決定の後に、Q1 が Queue を作る。

## 実施（q675、P2 generation10、2026-10-04、edb4a69 の上）

- 先に p013 の残り: `rename-map.py public` が改名の後の header（互換の macro）から表を作るように（`compat_of`・`public_rows`）。表は前と同じ内容（52409ab）。
- 変換は道具で一度だけ: `rename-map.py apply-keiland` が keiland.h の公開の名前（202、`paths.h` の install の path の macro と include guard を除く）を `kl_`・`KL_` にし、keiland.h の最後（`#include <keiland-ui.h>` の後）に互換の block（`#ifndef KL_COMPAT` … `#endif /* KL_COMPAT */`、旧名 202 の `#define`）を置き、libkeiland（ui を含む）の source と keiland-ui.h の中の keiland の名前（`struct kl_scroller` など）を改名する。`KL_EDIT_*`・`KL_KEYBOARD_INSET_*` は keiland.h に一つ（値が同じことを確かめて keiland-ui.h の定義を除き、keiland-ui.h の comment から keiland.h を指す）。`KL_VERSION` は 23 のまま。protocol の識別子（`keiland_*_v1`）は対象外（p021）。
- 共有の source の内部の名前（D16）: `keiland_picture*`・`keiland_color_glyph*`・`keiland_mark*` → `kl_*`（`apply_shared`、picture・artwork と使う所: files・imageview・settings・textedit・compositor の glass.c・ui/text.c）。これらは互換の macro が無いので利用者の source も変えた（app の公開 API の利用は互換の macro で無変更）。
- `rename-map.py check-keiland`（block の行き先が全て keiland.h・keiland-ui.h にあり、keiland.h の code に旧名が無い）PASS 202。`check-ui` を keiland.h の定義も見るように直した（PASS 339）。exports.map を再生成（`keiland_` 0）。

### 確かめ（ここまで）
- zedBSD: CI の clang 抜きの config の rootfs の build exit 0・自前の warning 0。`llvm-nm -D libkeiland.so` 270 で `keiland_`・`kui_` 0、header の一覧と一致。
- Linux: `make keiland-linux`（gcc）exit 0・warning 0。
- `keiland-os-boundary/check.sh` PASS。host: host-draw 12/12・host-widgets 94/94・host-chooser 85/85・ws131 host-system・files host-default PASS。

## Resume（2026-10-04、ユーザーの指示のラップアップで中断）

残り: (1) Linux の clang の build と install・elf-check・makefile-sync・header-check（p013 で通った手順）、(2) FreeBSD は `make -n`、(3) 受け入れの xserver・menu-probe・titlebar-probe の build（CI の config に入らない test の probe は `ZEDBSD_USER_PROGRAMS=` で個別に）と、p013 で流した残りの host 試験（ws081 の 6 本・ws089・ws100・ws128・ws102・textedit・imageview・scroll-bar）、(4) 全文規約の確かめ（`plan/tools/style-check.py` を変えた C に）。QEMU（p012 と同じ組に titlebar の probe の手順 `plan/tools/titlebar/` を足す）・Linux の PNG・FreeBSD の native build と audit は T1 に依頼済み（p013 と p014 をまとめて、このラップアップの時点の SHA で）。
