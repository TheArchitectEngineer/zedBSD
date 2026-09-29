<!-- awesome-plan project=zedbsd record=ws074p076 -->

# ws074-p076: JS の `Date`

Phase ID: `ws074-p076`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p026（cleared。Object・Function・Number・Error の組み込み）、p046（JSON の `toJSON`）
由来: amazon-goal.md §4 の 12「ws074-p027・p065 の一部」の `Date` の部分。p027 の後、Amazon の script の最初の blocker が
`Date is not defined`（script のある top の capture で 24 件）だった。p065（Google の challenge の JS の環境、planned のまま）の
`Date` の項をこの Phase に分けた（p065 の残りは Promise・Symbol・Map・Set・`navigator` 等）。

## 範囲

ECMAScript の `Date`: 構築子（引数なしで現在、1 つで Date・文字列・time value、2〜7 で local の年月日時分秒 ms）と関数としての
呼び出し（現在の `toString`）、`Date.now`・`Date.parse`・`Date.UTC`、prototype の getter と setter（local と UTC の全て）、`getTime`・
`valueOf`・`setTime`・`getTimezoneOffset`、`toString`・`toDateString`・`toTimeString`・`toUTCString`（`toGMTString` は同じ関数、
Annex B）・`toISOString`・`toJSON`・`toLocale*String`（en-US の形。Intl は無い）、Annex B の `getYear`・`setYear`。
この Phase に無いもの: `Date.prototype[Symbol.toPrimitive]`（Symbol が未実装。VM の ToPrimitive が Date の default の hint を
string にして代える）、Temporal（`toTemporalInstant`）、Intl による locale の形。

## 設計と実装

- `js/builtin_date.c`（新）: Date は kind `VM_KIND_DATE` の普通の object で、internal が time value（1970-01-01T00:00:00Z からの ms、
  不正な日付は NaN）。暦は仕様の先発グレゴリオ暦で、epoch からの日数を Howard Hinnant の civil の算法（double）で相互に変換する。
  MakeTime・MakeDay・MakeDate・TimeClip は仕様の通り（非有限の部分は NaN、±8.64e15 ms を超えれば NaN）。local time は libc
  （`localtime_r` の `tm_gmtoff`。夏時間は system の zone に従う）。UTC(t) は「local を UTC とみなした時の offset」で一度引き、
  その瞬間の offset で引き直す。getter と setter は local と UTC の族ごとに 1 つの native 関数で、関数の data が field（`DATE_UTC` で
  UTC の族）を示す。`toString` の zone の名前は libc の略称から Chromium の書く長い名前へ（UTC・GMT・BST・JST・KST・CET・CEST・
  EST・EDT・MST・MDT・PST・PDT。CST・IST のように複数の zone を指す略称は略称のまま）。
- `Date.parse`: 仕様の ISO の形（`YYYY`・`YYYY-MM`・`YYYY-MM-DD`、符号付き 6 桁の年、`THH:mm[:ss[.sss]]`、`Z`・`±HH:mm`。日付だけは
  UTC、時刻付きで zone なしは local、`-000000` は NaN、24:00 は日の終わりだけ）。月末を過ぎた日（2 月 30 日）は Chromium と同じく
  翌月へ繰り越す。ISO でなければ、`toString`・`toUTCString` の形と一般の形（月・曜日の名前、`m/d/y`・`y/m/d`、`h:mm[:ss[.sss]]`
  と AM/PM、GMT・UTC・Z と `+hhmm`・`+hh:mm`、括弧の comment）を読む（`struct date_legacy` と token ごとの関数）。
- VM（`vm/operation.c`）: `vm_to_primitive` は Date の default の hint を string に（`@@toPrimitive` の代わり）。`vm/vm.h` に
  `VM_INTRINSIC_DATE_PROTOTYPE`。`js/builtin.c`・`builtin.h` に `js_builtin_install_date`、`libbrowser/Makefile` に source。
- 前の枠の WIP（2026-09-29 の commit 17542b96）を仕上げた: style-check の 42 件（閉じ括弧の後の空行、条件の中の呼び出し）を直し、
  1 つの引数の構築（`date_from_value`）・ISO の小数（`date_fraction`）・legacy の parse（token ごとの 7 つの関数）を分けた。

## 試験

- `plan/ws074/tests/js/date.js`（新）と `date.expected`（Chromium 153 の出力、`run-js-tests.py --reference`、他の expected は不変）:
  構築・getter・setter の繰り上がり（`setHours(25, 61, 61, 1001)`、月末の `setMonth`）・不正な日付・ISO と UTC の文字列・範囲の端
  （±8.64e15、年 -1・10000）・`Date.parse` の ISO と legacy の形・Date の比較と `+`・`-`・誤りの種類・length。どの行も time zone に
  依らない出力（UTC の形か、local で作って local で読む値）にした。

## 確認（host は Debian の cc。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。zedBSD の amd64 の build（`make ZEDBSD_CONFIG=plan/ws074/tests/config-amd64-browser.mk
  build/amd64/bin/browser`）warning 0。style-check（`builtin_date.c`・`builtin.c`・`builtin.h`・`operation.c`）: 指摘 0（vm.h の 3 件は前から）。
- run-js-tests **9/9**（date を追加）を TZ=UTC・Asia/Tokyo・America/New_York・Europe/London・Australia/Lord_Howe（30 分の夏時間）で、
  ASan の program でも 9/9。
- test262（`test262-7ab7fafa`）: **15762 → 16393 / 47792**、ES5 の範囲 **7592 → 7732 / 8087**。新しく落ちた試験 0（Date の前の engine
  を一時の worktree で build して failures の一覧を比べた）。`built-ins/Date` 480/618（UTC・Tokyo・New York・Lord Howe で同じ、
  London は 477: 下の制限）、`annexB/built-ins/Date` 20/24。Date の残りの失敗は arrow・let/const・Symbol・Reflect・`$262`・BigInt・
  Temporal が要る試験で、Date 自体の失敗は無い。ASan の driver でも 480/618。
- 回帰（plain）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、
  host-relayout 147/147（全 page）、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。
- Amazon（2026-09-28 23:52 の capture、host、ASan の `--render`）: 報告 0。script のある top-local の Uncaught のうち
  `Date is not defined` **24 → 0**。残り: `P is not defined` 37、let/const 3、`Cannot read properties of undefined or null` 3、arrow 2、
  `navigator is not defined` 1、`Image is not defined` 1、template 1、`value is not a function` 1。search-local も Date 0、
  `P` 26・let/const 10・`navigator` 1・`Image` 1 等。AUI の `P` を定義する script は、今は `navigator`（と `Image`）で止まる。
- guest（QEMU、worktree の image、headless の guest）: `plan/ws074/tests/js/*.js` を guest の `/bin/browser --js` で走らせ、
  `run-js-tests.py --outputs` で 9/9。guest には zoneinfo が無く（`/etc/localtime` 無し）、既定と `TZ=Asia/Tokyo` は UTC、POSIX の
  `TZ=JST-9` で `GMT+0900 (Japan Standard Time)`・offset -540 を確かめた。
- `plan/tools/boot-test.sh`（`plan/ws074/tests/boot-check.sh p076`）: PASS、login prompt を画面で確認
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p076-20260929-boot-login.png`）。

## 未実施・制限・残り

- 実機は未実施。guest の窓（zdesktop）で live の Amazon を開く確認は未実施（Date は配置に関わらず、headless の guest で JS を確かめた）。
- Europe/London の 3 件（`toISOString/15.9.5.43-0-8〜10`）: 試験は epoch の offset（1970 年の London は +1 時間）の符号で
  ±8.64e15 の外に出る値を作るが、libc はその極端な過去に LMT（-0:01:15）を返すので範囲の内に入る。V8 も LMT を使うので、
  同じ zone では Chromium も同じ結果になると見られる（未確認）。
- `Date.prototype[Symbol.toPrimitive]`（Symbol の後、p028）、Temporal、Intl の locale の形は残り。
- guest の既定の zone は UTC（zoneinfo が無い）。日本の時刻で表示するには system の zone の設定が要る（WS074 の外）。
- Amazon の script を先へ進める次の blocker: `navigator`・`Image`（p065 の残りの DOM の環境）、let/const・arrow・template（p028）。
