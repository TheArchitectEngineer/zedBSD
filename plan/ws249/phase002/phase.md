<!-- awesome-plan project=zedbsd record=ws249-p002 -->

# ws249-p002: BCM43455 の driver の規約の全文の見直し

Status: cleared（2026-10-15、q1205-i01、P2。実機の確かめは WS の最後に T へ）
Parent: [WS249](../ws.md)
Approval: 2026-10-15 ユーザー「bcm43455も同じガードレールで修正してほしいです。WSを立ててください。」（WS の作成。実行は担当の割り当ての時に Queue に入れる）

## 範囲

ws.md の「完了の条件」のうち、この Phase の分。動作は変えない。手順は WS248 の同じ Phase に倣う。

## 結果（2026-10-15、P2）

対象: `src/drivers/wifi/bcm43455/` の 21 file（変更は 14 file）。書き方だけを変え、動作は変えていない。

- style-check.py（`python3 plan/tools/style-check.py src/drivers/wifi/bcm43455/*.[ch] --summary`）: 120（p002 開始、c6a80da4a）→ 24。残る 24 は全部 goto で、`bcm43455.c` の `bcm43455_device_create()`・`bcm43455_device_start()` の `goto fail`（14）と `bcm43455-files.c` の `bcm43455_files_load()` の `goto fail`・`read_exact()` の `goto out`（10）。どれも 1 つの共有の後始末の label への前向きの jump で、§14 の「`goto` is used only for a single forward jump to a shared cleanup label」の形なので残した。このため `fail:` の 3 関数は後始末の `return error;` が最後の文になる（§11 の「最後は成功の return」と食い違うが、成功の return は label の前に別にある）。
- 直した物:
  - blank-after-brace（約 59）と paragraph-comment（約 34）: 閉じ括弧の後に空行を入れて段落の comment を足す、関数の先頭の `device = context;` などに comment を足す。lifecycle_lock を手放してから取り直す間（`close_locked()` の 2 箇所、`discard_unpublished()`）は 1 つの comment の下の一つながりの段落にした。
  - call-in-condition（3）: `sched_ticks()` を `now` に、`runtime_usable_locked()` を `usable` に受けてから判定する（呼ぶ順は同じ）。
  - `(void)x;` を `UNUSED_PARAMETER(x);` に（3 箇所）。`bcm43455.c`・`bcm43455-station.c` に `#ifndef` で守った局所の定義を置いた（arm64 の HAL の defs.h には無く、rpi4-bt.c と同じ形）。
  - 入れ子の呼び出し（`interrupt_wait(..., deadline_after(...))`）を `poll_deadline` に分けた。
  - 3 つ以上の節が 1 行の条件（6）を節ごとの行に分けた。
  - 中身の無い comment（「Sends it.」「Requests it.」「Waits for it.」「Reports them.」「Splits the fields.」「Samples the slot.」「Any other refusal.」「Reports the id.」「Reports the tick.」など）を、段落の意味を言う文に書き直した。
  - 80 桁を超える 1 行の comment（約 190）を複数行の形に折り返した。
  - header の不完全型 `struct bcm43455_device;` に役割の comment を足した。
- 調べて問題の無かった物:
  - `?:` は 0。
  - Boolean を式で作る代入は 0。
  - 呼び出しの結果を直に返す return は 0。
  - 前方宣言: static 関数は全部 1 つずつあり、定義の無い宣言も無い。header の公開の宣言は全部定義がある。
  - 公開の関数の複数行の comment、static 関数の 1 行の header、型・file scope の表の comment は揃っている。
  - 定型の comment（banned.sh の文言）は 0。
- 読んで残した物:
  - lint2 の if-without-comment 39: どれも critical section の先頭（comment は lock を取る行の上）か、入れ子の block の最初の文（外の段落の comment が説明）。
  - 80 桁を超える code の行（約 290、長い定数名の式と 1 行の前方宣言）は WS248 と同じく範囲外。
- 出典の表記の追記: Linux brcmfmac の 2 つの値の citation に版と file を入れた（p001 の「出典の不明」の解決、p001 の結果に記録）。
- 確認:
  - `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk vmunix`: rc 0、warning 0。
  - `sh plan/ws206/tests/bcm43455-host-test.sh build/ws249/host`: PASS。
  - comment を除いた比較（`gcc -E -fpreprocessed -dD -P`、空白を正規化、c6a80da4a との比較）の差は 4 種類だけで、全部読んだ。
    - `poll_deadline`・`now`・`usable` の変数の追加と、呼び出しを先に受ける形への変更（評価の順は同じ）。
    - `(void)` から `UNUSED_PARAMETER` への置き換え。
    - `UNUSED_PARAMETER` の定義。
    - 条件の行の分割（空白だけ）。
  - `git diff --check`: 問題なし。
- 未実施:
  - 実機（Pi 4、10.0.30.2）の WiFi。WS の最後に T へ依頼の案を出す。
  - QEMU（実装の担当は起動しない）。
