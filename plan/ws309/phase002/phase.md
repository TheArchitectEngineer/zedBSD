<!-- awesome-plan project=zedbsd record=ws309-p002 -->

# ws309-p002 共通の部品: thread の台帳・lane（常駐と pool）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §3。Queue: q1662-i01（P1）。
- 状態: cleared（2026-10-29 P1: host 試験と build。lane は p003 の QEMU で io-device として動いた）。
- 依存: p001（設計）。

## 範囲

- `userland/desktop/wayland/threads.c`・`threads.h`: thread の作成（全 signal を block して作る）と名前の台帳、`KWL THREADS` の行、CPU の数、wake pipe への 1 byte。
- `userland/desktop/wayland/lane.c`・`lane.h`: 常駐の thread（1 本か N 本の pool）と FIFO の job（run は thread、done・drop は main）、key つきの置き換え、`main_woken` で wake を 1 byte に、
  終わりの finish（終了の時だけ待つ）と stop（未着手・未取得は drop）。
- build の file の一覧（Makefile・Makefile.linux・Makefile.freebsd）に足す。
- 既存の thread（render・cursor・input・glyph）の台帳への登録は、それぞれの Phase で（今は触らない。`plan/tools/compositor/list-replay-host-test.sh` が render-thread.c を単体で link するため）。

## 受け入れ

- host 試験 `sh plan/ws309/tests/lane/run.sh`（ASan+UBSan と TSan）が PASS: 1 本の順、wake の byte の後の taking、key の置き換え（未着手の 2 つが drop、最新だけ run）、finish、
  stop（実行中は終わる、未着手と未取得は drop）、pool 4 本で同時に 4、名前 hand-1〜hand-4、全 signal の block、範囲外の本数を EINVAL。
- `build/amd64/bin/wayland` が warning 0。

## 記録

- 2026-10-29 01:5x P1: host 試験 2 回とも `host-lane: PASS`（ASan+UBSan、TSan）。`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/wayland` exit 0、warning 0。
