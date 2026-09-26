<!-- awesome-plan project=zedbsd record=ws069p008 -->

# ws069-p008: zdesktop-x11server（単体の rootless の X server、組み込める形）

Phase ID: `ws069-p008`
Parent: [WS069](../ws.md)
Status: cleared（2026-09-27 の追記。q488-i01 は uncleared、p010 の後に x11-p005 と実機が PASS）
Phase disposition: normal
Queue: q488-i01
承認: 2026-09-27 ユーザー「Wayland用のXサーバは、単体のプログラムとして実装しましょう。userland/base/zdesktop-x11serverとします。」
「rootlessのみでOKです。…再利用できるモジュラリティを保っておくと、あとで組み込みが楽です。」「続けてください。」
設計: [design.md](../design.md) §0

## 方式

`userland/base/zdesktop-x11server/`（`/bin/zdesktop-x11server`、amd64）。Xzed（`userland/X11/xzed`、`6873440a` の時点）の X の
protocol の核と ws069-p002〜p005 の Wayland・rootless・glyph・GLX を移し、組み込める形に分ける:

| file | 役割 |
| --- | --- |
| `x11server.h` | 外への interface: `x11server_create`（設定）・`x11server_pollfds`（待つ fd の列）・`x11server_dispatch`（readiness を渡す）・`x11server_stopped`・`x11server_destroy`。global な状態を持たない |
| `internal.h` | 内部の型（`struct server`・client・窓・pixmap・GC・font）と module の間の関数 |
| `main.c` | 引数、signal、`poll` の loop（上の 5 つの関数だけを使う） |
| `server.c` | server の作成・破棄、listen socket、client の接続と読み（要求の分け方）、fd の列と dispatch |
| `protocol.c` | 要求の処理（`request` の switch）、返事・event・error |
| `window.c` | 窓の木・pixmap・GC・描画（fill・text）・合成、find・hit |
| `rootless.c` | X の top-level と Wayland の窓の対応、present、focus・pointer・key の配送 |
| `wayland.c` | Wayland の接続、xdg_toplevel、wl_seat、wl_shm の buffer（rootful の部分は持たない） |
| `keymap.c` | evdev の key → X の keycode（Xzed の input.c の対応の部分だけ） |
| `glyphs.c`・`glx.c` | libtruetype の glyph、GLX 拡張（Xzed から） |

持たないもの: `/dev/graphics`・`/dev/input`・rootful（x11-p002 の試験も移さない）。表示は p008 では wl_shm（標準の Wayland）の
まま、Vulkan での表示は ws069-p011。

切り替え: `zdesktop-x11`（App Home）は `zdesktop-x11server` を起動、試験（x11-p003〜p005、zdesktop-p070・p071）、config、demo image、
実機の scenario。Xzed はこの Phase では触らない（p009 で戻す）。

## 受け入れ

1. build warning 0、新しい code は coding-style の全文（`plan/tools/style-check.py` の指摘 0）。global な可変の状態は main.c の
   signal の flag だけ。
2. Venus: x11-p003（rootless の zterm）・x11-p004（glxtest）・x11-p005（zgears、回る）・zdesktop-p070（App Home の X terminal と
   Gears）が新しい server で PASS。
3. i915 実機の `zdesktop-x11` の run で desktop・Gears・X terminal が出る（gears_turns は BUG-057、p010）。boot test。

## 結果（2026-09-27、q488-i01）

- `userland/base/zdesktop-x11server/`: `x11server.h`（`x11server_create`・`x11server_pollfds`・`x11server_dispatch`・`x11server_stopped`・
  `x11server_destroy`、zdesktop に組み込める interface）、`internal.h`、`main.c`・`server.c`・`protocol.c`・`window.c`・`rootless.c`・
  `wayland.c`・`keymap.c`・`glyphs.c`・`glx.c`。global な可変の状態は `main.c` の signal の flag だけ（font の状態も server の中）。
- Xzed からの変更: rootless だけ（rootful・`/dev/graphics`・`/dev/input`・`-- command` は持たない）。client への出力は queue に貯め
  POLLOUT で送る（Xzed は EAGAIN で返事を捨てていた）。窓の位置は root の上の絶対座標で一貫させ、GetGeometry・ConfigureNotify・
  event の座標は親・窓からの相対（Xzed は CreateWindow だけ相対だった）。子を持たない top-level は行ごとの copy で合成（Gears が速い）。
  zwm・zshell（レトロ用）の私的な要求 129〜131 は持たない（BadRequest）。止まった client の報告（`X11SERVER STUCK`・`HISTORY`・`HELD`、
  2 秒ごと、止まっているときだけ）。
- 切り替え: `zdesktop-x11`（App Home）、試験（x11-p003〜p005、zdesktop-p070・p071）、config（zdesktop・実機）、demo、実機の scenario。
  x11-p003 は server と zterm を別々に起動する形に。

## 検証

- build（`plan/ws035/tests/build-zdesktop-image.sh`）warning 0。`plan/tools/style-check.py` の指摘 0（新しい 11 file）。
- Venus（QEMU）: x11-p003 PASS、x11-p004 PASS、zdesktop-p070 PASS。x11-p005: 回る（`turn: gears-later.png differs ok`、CHECK failures=0）が
  **frame 300 に届かず FAIL（BUG-057 が Venus でも再現）**。約 7.5 fps。
- BUG-057 の特定（Venus、server の報告）: server は frame B の要求（seq 18019〜18027）と次の frame の GetGeometry（18028）を処理し、次の
  PutImage（261016 byte）を 196616 byte（= 3×64 KiB + 8）だけ受け取ったまま、20 ms ごとの recv が何も返さない。zgears は大きな要求の
  `send()` の中で止まっている。**kernel の unix stream socket で、受け手が buffer を空けても送り手が起こされない（または送れない）**
  と見る。zgears の watchdog の `swap_step=5` は libGL の変数が executable と共有されていない（copy relocation）ためで当てにならない。
  続きは ws069-p010。
- i915 実機（capture、`build/ws069-p008-hw/`）: desktop・Gears・X terminal・デスクトップ 2 と 1 が PASS、gears_turns は FAIL（BUG-057、
  frame 541 で止まり）。約 37〜46 fps（Xzed のときの約 27 fps より速い）。実機の LCD の目視は未実施。
- boot test PASS（`build/ws069-p008-boot/login.png`）。

## 再開の条件

ws069-p010 で BUG-057 を直した後、Venus の x11-p005 と実機の run を流し、通れば日付を付けて clear する。

## 追記（2026-09-27、p010 の後）: cleared

ws069-p010 で BUG-057（kernel の socket の待ち）を直した後、Venus の x11-p005 が PASS（frame 300、DONE、回る）、実機の run3 で 6 検査 PASS。
受け入れ 2・3 を満たしたので clear とする（q488-i01 の uncleared の結果はそのまま履歴に残す）。
