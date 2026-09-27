<!-- awesome-plan project=zedbsd record=ws035p075 -->

# ws035-p075: libwayland の汎用の event dispatch と、event の new_id（server が作る object）

Phase ID: `ws035-p075`
Parent: [WS035](../ws.md)（p028 から 2026-09-27 に分割）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main への統合は main の session）
承認: 2026-09-27 ユーザーの指示（サブエージェントで WS を完了まで進める。アプリを動かす基盤を優先。main の session の伝達による要約）

## 背景

toolkit（GTK・Qt）や Chromium の Ozone は、使う Wayland の protocol（xdg-shell、`wl_data_device`、`wl_subsurface`、decoration、
cursor-shape 等）の client の code を **wayland-scanner** で作り、libwayland の generic な marshal と dispatch で動かす。
zedBSD の libwayland は request の marshal は signature から汎用に行えたが、event は libwayland が型付きの表を持つ interface
（wl_registry・wl_callback・wl_buffer・wl_shm・wl_surface・wl_output・wl_seat・wl_pointer・wl_keyboard・xdg-shell・System Menu）
だけを listener に渡し、それ以外は ENOTSUP で捨てていた。また event の `new_id`（server が作る object。`wl_data_device.data_offer`
等）は「選んだ protocol には無い」として event ごと拒んでいた。どちらも toolkit を動かす前提になる。

## 範囲

1. 型付きの表の無い interface の listener を、signature から引数を並べて呼ぶ汎用の dispatch（libffi を使わない）。
2. event の `new_id`: server の範囲（0xff000000 以上）の identity で proxy を作り、listener に渡す。client が destroy したら
   delete_id を待たずに map から外す（server は自分の作った object に delete_id を送らない）。listener に渡らなかった
   new object は event の破棄で destroy する。
3. host の試験: wayland-scanner の code だけを持つ client（zedBSD の libwayland）と、host の libwayland-server の試験 compositor。

## 受け入れ

1. host の試験で、全種類の引数（int・uint・fixed・string・null の string・array・fd）、12 個の引数（register を越える）、server が
   作る object（その listener、request と event、destroy、同じ identity の再利用）、null の object が正しく届く。
2. 変更前の libwayland では同じ試験が失敗する（試験が不足を見分ける）。
3. guest の回帰（WS070 の menu の試験、WS035 の zdesktop の試験）が通る。build warning 0、style-check（既存は悪化させない）。

## 結果（2026-09-27）

cleared。

### 実装

- `userland/base/libwayland/event.c`: `wlc_event_generic`。型付きの表の無い interface の listener を、各引数を 1 word
  （`uintptr_t`。int・fixed・fd は符号を拡張）にして、20 word を渡す 1 つの関数型（`wlc_generic_callback`）で呼ぶ。
  zedBSD が build する ABI（amd64、i386、AArch64 の AAPCS64、SPARC V9）では整数と pointer の引数は宣言の型に関わらず 1 つの
  register か word の大きさの stack の slot を占めるので、自分の引数だけを宣言した callback は正しく読み、余りの word は無視される
  （file の comment に理由を書いた）。引数 20 を越える event は EPROTO。`wlc_event_destroy` は listener に渡らなかった new object を destroy する。
- `userland/base/libwayland/wire.c`: event の `'n'` を decode し、protocol の型の interface で `wlc_proxy_insert_server` を呼ぶ
  （型が無い、server の範囲外、使用中の identity は malformed）。
- `userland/base/libwayland/proxy.c`: `wlc_proxy_insert_server`（map・listener・event の 3 つの hold、生んだ object の queue と版）。
  `wlc_proxy_destroy` は server の範囲の identity をすぐ map から外す。`internal.h` に宣言。

### 確認（host）

- `plan/ws035/tests/p075/run-host.sh`（新規。`generic-test.xml`・`server.c`・`client.c`）PASS（build/ws035-p075-host/）: 2 回の round で
  34 の check が ok、`CLIENT DONE failures=0`、`SERVER DONE children_destroyed=2 client_gone=1`。server は 2 回目の child に同じ
  identity（4278190080 = 0xff000000）を使い、client はそれを新しい proxy として受けた（map から外れていた証拠）。
- 変更前の libwayland（merge `41cec40e` の版、`LIBWAYLAND_ROOT` で指定）では FAIL（`round events`・`child`・`references` の 3 つ。
  汎用の listener が呼ばれない）。試験が不足を見分けることを確かめた。
- host の compile は zedBSD の Wayland の header だけを include path に置き（libc の header は置かない）、`-Wall -Wextra -Werror` で warning 0。

### 確認（QEMU・Venus、lean image）

libwayland は全 client が使うので広く回した（zedbsd7 の toolchain、merge `41cec40e` の後の tree）: `plan/tools/titlebar/menu-p002.sh` PASS、
`menu-p003.sh` PASS、`menu-regress.sh` で WS035 の p059・p062〜p065・p068〜p072 すべて PASS（mview・wltest・wlshm・zdesktop-terminal・
zdesktop-x11server の X の app を含む）。build warning 0（libwayland-client）。

### 規約

- 変えた既存の file の style-check は変更前と同数（event.c 3、proxy.c 37、wire.c 40、internal.h 0）。wire.c の new_id の失敗は
  `goto fail` を足さず、関数の先頭の確保の失敗と同じ `wlc_event_destroy` と return にした。
- 試験の program（plan/ws035/tests/p075/）は style-check 0。試験の検査の複合条件（`a0 == -1 && …`）は試験の読みやすさのため残した。

### 制限

- 汎用の呼び出しは C の規格では未定義の関数型の変換で、ABI に頼る（上の 4 つの ABI）。Apple の arm64 の ABI（stack の引数を詰める）
  では成り立たないが、zedBSD の対象ではない。
- 実際の toolkit（GTK・Qt）はまだ port されていない。server 側の protocol は p076〜p080。
