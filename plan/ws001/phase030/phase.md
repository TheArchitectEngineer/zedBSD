# ws001-p030: date・sleep・uname・kill・pathchk・strings ほかの小さな utility

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #30 date、#111 sleep、#134 uname、#64 kill、#93 pathchk、#114 strings、#66 link、#139 unlink、#129 tty、
#71 logname を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。

## 範囲と結果

| utility | 変更 |
| --- | --- |
| date | 書き直し。`-u`、`+format` を libc の `strftime()` に渡す（`E`・`O` の修飾子は POSIX locale では意味が無いので除く）、既定の書式 `%a %b %e %H:%M:%S %Z %Y`、TZ の時間帯、`mmddhhmm[[cc]yy]` で時計を設定（範囲の検査、月末を越える日の拒否、`yy` の 69〜99 は 1900 年代）、権限の無い設定は失敗。以前は常に UTC で、書式も一部だけだった |
| kill | 書き直し。`-s name`・`-name`・`-number`・`-l`・`-l status`（128 を超える状態は signal の番号に直す）、名前は大小を問わず `SIG` の接頭辞の有無も問わない、`0` は存在の確認、`--` の後の負の pid（process group） |
| pathchk | 書き直し。`PATH_MAX` と、実在する directory の `pathconf(_PC_NAME_MAX)`、途中の directory が directory でない・検索できない場合、`-p`（`_POSIX_PATH_MAX`・`_POSIX_NAME_MAX`・移植できる文字・空）、`-P`（空と `-` で始まる成分） |
| strings | 書き直し。`-a`、`-n`（正の数）、`-t d/o/x`（7 桁に右寄せ）、tab を印字可能に数える、file の全体を探す |
| uname | 書き直し（規約）。PC-98 の `pc98` の追加は保つ。operand は誤り |
| tty | 書き直し。状態 0/1/2、書き込みの失敗は 3、歴史的な `-s` を受ける（以前は 2 を返した） |
| logname | 書き直し。operand は誤り（以前は無視した） |
| link・unlink | 規約の書き直し（振る舞いは同じ。`plan/ws001/tests/link-unlink-test.sh` も PASS） |
| sleep | 変更なし（規約の違反 0、case は一致） |

## 受け入れ条件

1. host の差分試験 `plan/tools/utils/cases/{date,small}.sh`（small.sh は sleep・uname・kill・pathchk・strings・link・unlink・tty・logname）が全件一致。
2. GNU が POSIX と違う点、またはここが意図して違う点は `plan/ws001/tests/pinned/*.sh` に期待値を書き、`plan/ws001/tests/pinned-cases.py` で確かめる。
3. 触れた source が style 違反 0。
4. amd64 guest で case が一致し、guest で date の時計の設定を確かめる。

## 記録

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 | date 12/12、small 32/32（最初は date 1/12、small 20/32） |
| 期待値を書いた case `python3 plan/ws001/tests/pinned-cases.py` | 5/5（`kill -l 137` → KILL（procps は答えない）、`mkdir -m +t`、pr の header の書式、`pr -d` の page の長さ、`chgrp -R -H` の中の link） |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p030.out date small pinned` | 49/49 |
| guest の date の設定（serial） | `date 010203042030` → `Wed Jan  2 03:04:00 UTC 2030`・状態 0、`date 13010000` → 状態 1、`TZ=JST-9 date 010203042030` → UTC で `2030 01 01 18 04` |
| `sh plan/ws001/tests/link-unlink-test.sh` | PASS |
| style（date・kill・pathchk・strings・uname・tty・logname・link・unlink・sleep） | 違反 0 |
| 実機 | 未実施 |

### 残り

- date: `strftime()` の Issue 8 の field width と flag（`%+4Y` など）は libc に無い。LC_TIME の locale は LIBC-LOCALE-01。
- kill: 実時間 signal（RTMIN+n）の名前は扱わない（番号で送れる）。
- strings: 多 byte の印字可能文字は LIBC-CTYPE-01。
