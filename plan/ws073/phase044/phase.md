<!-- awesome-plan project=zedbsd record=ws073-p044 -->

# ws073-p044: BUG-123（desktop-probe が `--timeout-s=3` の後に終わらない）の切り分けと修正

Status: cleared（2026-09-30、P1、worktree `wt/ws073`。kernel の poll は正しい。原因は libwayland-client の wl_display_dispatch_queue が queue の event が来るまで時間切れ無しで待ち続けたこと。標準と同じ 1 回の待ちと読みに。Venus の guest で restart の手順 修正前 7 回中 2 回 FAIL → 修正後 18 回 PASS。5330 の実機は未実施）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-123](../../bugs/BUG-123.md)
Queue: Q1 の依頼（2026-09-30「BUG-123 の切り分け」、ユーザーの了解済み）。Queue の ID は Q1 が記録する

## 範囲

poll の時間切れが戻らない kernel の不具合か、probe（とその library）の不具合かを切り分ける。小さな C の試験（`poll` の時間切れを
1000 回、戻りの時間の記録）を先に作り、QEMU の guest で回す。kernel の不具合なら直す。

## 1. kernel の poll（`plan/ws073/tests/poll-timeout.c`、guest の clang で build）

mode: pipe・unix（誰も書かない fd）、busy（相手が 5 ms ごとに 1 byte）、probe（desktop-probe の loop: 200 ms の poll を time() が 3 s
進むまで）、hup（相手が 0〜3 byte 書いて 0〜20 ms 後に exit: 1 回目の poll が close で終わり、2 回目の poll もすぐ終わり、read が 0
で終わること）。時間切れが timeout より 100 ms 以上遅い「late」、nothing ready で早い「early」、POLLIN なのに読めない「spurious」を数える。

guest: この tree の kernel（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-p044 vmunix`、warning 0）を
`tests/kernel-image.sh` で main の測定用 image に入れ、`plan/tools/guest/guest.sh start --cpus 4`（QEMU、KVM）。5 つを同時に:

| mode | 結果 |
| --- | --- |
| pipe 1000 回 × 200 ms | 1000 回とも時間切れ、200〜201 ms、late 0・early 0・spurious 0 |
| unix 1000 回 × 200 ms | 同じ（200〜201 ms） |
| pipe 100 回 × 3000 ms | 100 回とも 3000〜3001 ms |
| busy 20000 回 | late 0・spurious 0 |
| probe 100 回（3 s の loop） | 最長 3002 ms、1497 回の poll は 200〜201 ms |
| hup 1000 回（2000 ms） | 1000 回とも正しい（close で終わり、2 回目もすぐ、read 0） |

kernel の poll の時間切れと、相手の close での読めることの報告に誤りは見つからない。

## 2. 実物の再現と、止まった場所（QEMU の Venus の guest）

`plan/ws035/tests/zdesktop-guest.sh start`（main の `build/ws035-sq/hdd-image.img` の複写、`GUEST_RUNTIME=build/ws073-p044/venus`）に、
この tree の compositor・library・probe（`make ... BUILD=build/ws073-p044u .../bin/wayland .../dynamic/lib*.so`・`plan/ws094/tests/build-probe.sh`、
warning 0）を `plan/ws094/tests/desktop-guest.sh install restart` で入れた。最初の 1 回で再現（start が 3 回、4 回目の start-limit が来ない）。
止まった probe（pid 103）に guest の lldb を attach: `ppoll` ← `libwayland-client.so`・`wl_display_dispatch_queue` ← `main`（probe の
`wl_display_dispatch`）。kernel の poll が戻らないのではなく、library の中の時間切れ無しの poll で待っていた。

## 原因

zedBSD の libwayland-client（`userland/desktop/libwayland/client.c`）の `wl_display_dispatch_queue` は、その queue の event を 1 つ以上
dispatch するまで「待つ（poll、時間切れ無し）→ 読む」を繰り返していた。標準の libwayland の `wl_display_dispatch_queue` は 1 回待って
1 回読んだら、dispatch した数（0 もある）を返す。

probe（と多くの client）は「poll に 200 ms の時間切れ → 読めるなら `wl_display_dispatch`」の形で、時間切れは自分の loop で見る。
compositor からの data が default の queue の event を含まない時（`wl_display.delete_id` のように library が内部で処理するもの、別の queue や
壊した object の event、message の途中）、`wl_display_dispatch` は次の event が来るまで無期限に待ち、probe の loop に戻らない。restart の手順
では compositor は ack の後に probe へ何も送らないので、probe は `--timeout-s=3` の時間切れを見ないまま止まる。

## 直し

`wl_display_dispatch_queue` を標準と同じ 1 回の待ちと読みに（読みの後に dispatch した数を返す、0 もある）。`wl_display_roundtrip_queue` は
自分の loop で sync の callback を待つので変わらない。既存の呼び手（ime、wlshm、userland/base/tests の probe）は全て loop の中で呼ぶ。
WSI（libvulkan）は `_pending` だけを使う。

## 検証

| 確認 | 結果 |
| --- | --- |
| 新しい host の試験 `plan/ws073/tests/wayland-dispatch-once.sh`（socketpair の相手が private queue の callback の done だけを送る。default の dispatch は 0 を返さねばならない、3 s の alarm） | 修正の版 PASS（plain・ASan/UBSan）。修正の前の client.c では FAIL（`wl_display_dispatch waited for an event of its queue`） |
| libwayland の既存の host の試験（`plan/ws014/phase006/tests/run-wayland-client.sh` と同じ命令、include の path を今の tree（`include/libc`）に直して `build/ws073-p044/wlc.sh`） | 修正の前も後も PASS（plain・ASan/UBSan） |
| build（`libwayland-client.so`） | warning 0 |
| `plan/tools/style-check.py`（client.c、前との差分） | 新しい指摘 0 |
| Venus の guest で restart の手順（`desktop-guest.sh ... restart`、同じ guest で続けて） | 修正の前の library: 7 回中 2 回 FAIL（最初の 1 回と、6 回のうち 1 回）。修正の版: 18 回 PASS（6 回 + 12 回） |
| QEMU の boot test | 未実施（変えたのは libwayland-client.so だけで、kernel と image は変えていない。guest での起動は Venus の guest の restart の手順 25 回で通っている） |

QEMU と実機: 全て QEMU（KVM の SSH の guest と Venus の guest）。5330 の実機は未実施。

## 残課題

- plan/ws014/phase006/tests/run-wayland-client.sh の include の path が古い（`libc/include` → `include/libc`、protocol の header の場所）。WS014 の
  試験なので直していない（同じ命令を path を直して `build/ws073-p044/wlc.sh` で回した）。
- 試験の file（poll-timeout.c・wayland-dispatch-once.c）の規約の指摘（前方宣言・条件の中の呼出し）は、既存の guest の試験と同じ形で残した。
