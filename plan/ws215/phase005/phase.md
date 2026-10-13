<!-- awesome-plan project=zedbsd record=ws215-p005 -->

# ws215-p005: bluetoothd の Broadcom の始動（始めの reset、速さの切り替え、patchram、BD_ADDR）

Parent: [WS215](../ws.md)
Status: in-progress（2026-10-13 P2: 実装・build（arm64・amd64 warning 0）・host 試験 PASS まで。test-wait: Pi の実機（下の「実機で見る点」）の結果を Q1 が判定するまで cleared にしない。QEMU は BT を emulate しないので QEMU の試験は無い）
Phase disposition: normal
Queue: P2 の lane（2026-10-13 Q1 の割当。承認: 2026-10-12 ユーザー「RPi4のWiFiとBluetoothを実装し」、2026-10-13 朝の D1〜D4 の答え（D3「推しで OK」= 実機で読んでから決める））

## 範囲（ws.md の表の行）

bluetoothd の Broadcom の始動（[p001](../phase001/phase.md) の B14・B15・4 節、第 3 版の補いの中-B の daemon の部分、軽微の「notice の取り違え」「Broadcom の folder」「firmware の file の名前」）、HCD の解析と手順の host 試験（台本の controller）、実機で firmware・3 Mbaud・scan。

受け入れ条件:

1. `bus == BT_BUS_UART` の controller の始動が `BT_IOC_RESET` から始まり、自分の notice まで（と、その後に残る物）を読み捨てる（上限 1 s で notice が無ければ始動の失敗）。
2. Read Local Version の manufacturer が 15（Broadcom）か 305（Cypress）の時だけ、Read Local Name を表で引いて `/lib/firmware/brcm/<file>` を読み、file を全部検査してから、速さの切り替え → Download Minidriver → Write RAM の各 record → Launch RAM → host を 115200 へ → HCI_Reset → 速さ → 版の読み戻し → BD_ADDR の順で送る。速さの切り替えでは `0xFC18` の答えの後に `BT_IOC_SET_SPEED`。
3. file が無い・表に無い chip は `FIRMWARE_NEEDED`、file の形が壊れている・record を controller が断った時は `FIRMWARE_FAILED`、line の失敗（時間切れ、kernel の自己 reset）は `ERROR`（daemon が開き直す）。
4. BD_ADDR は bt_info の `board_address` を使う（B15）。
5. 求めていない RESET の notice が READY から 60 秒以内に来たら始動の失敗と数え、3 回で止まり、RECOVER の reopen で戻る（中-B）。
6. host 試験（偽の controller で始動の順と失敗の扱い、ASan・UBSan）PASS。既存の bluetoothd の host 試験が PASS。build（arm64・amd64 の bluetoothd・bt・bt-probe）warning 0。
7. 実機（T1・ユーザー）: firmware の読み込み、3 Mbaud、scan で周りの機器が見える、`bt show`、版の読み戻し。

## 入る条件の確かめ

| 条件 | 状態 |
| --- | --- |
| p003（kernel の UART の HCI、`BT_IOC_SET_SPEED`、`bt_info` の `speed_max`・`board_address`） | main に入っている（T1-566 PASS、Pi の実機は未確認）。この Phase は p003 の UAPI だけを使う。kernel は変えていない |
| p004（brcmbt-firmware、`/lib/firmware/brcm/BCM4345C0.hcd`） | main に入っている。host 試験は P3 の worktree の検証済みの cache（`/home/awe/zedBSD-worktrees/p3/build/sources/firmware/brcmbt/adb4d55c…-dc85cced…/BCM4345C0.hcd`）を読むだけで使った |
| 2a 節の p005 の行 | 下の「値の出典」で全部を出典つきにした（出典の無い時間は **自前の決め** と明記） |
| D3 | ユーザー「推しで OK」= 実機で読んでから決める。実装は「既定の address の表に当たる時だけ board の address を書く。board の address が無ければ既定のまま（log に出す）」。表の BCM4345C0 の既定は実機で確かめる（下） |

## 出典と写しの監査

### 読んだ物（permissive だけ、`build/tmp/ws215-p005-ref.SeHMcL/`、git に入れない）

| file | license | 取得元 | sha256 |
| --- | --- | --- | --- |
| `hci_uart_infineon.c`（(e1) の今の名前。`h4_ifx_cyw43xxx.c` は Zephyr の main に無く、Infineon の UART の HCI の拡張はこの file） | Apache-2.0 | `raw.githubusercontent.com/zephyrproject-rtos/zephyr/main/drivers/bluetooth/hci/hci_uart_infineon.c`（取得の時の main の先頭 `ca10f7fc7a5b`） | `07aa15b3…b8a288` |
| `brcm_patchram_plus.c`（(e2)） | Apache-2.0（Broadcom） | `raw.githubusercontent.com/aosp-mirror/platform_system_bluetooth/master/brcm_patchram_plus/brcm_patchram_plus.c`（mirror の master `2c5280fdee63`） | `d07c088d…14a938` |
| `init_bcm43xx.c`・`btattach.c`・`btattach.h`（NetBSD の btattach） | BSD-2-Clause | `raw.githubusercontent.com/NetBSD/src/trunk/usr.sbin/btattach/`（trunk `22df9983d631`） | `2c25439d…6f53` ほか |

GPL の code（Linux の btbcm・hci_bcm・hci_h4、BlueZ の hciattach）は読んでいない。NetBSD の `init_bcm43xx.c` の comment は「hciattach(8) から拾った」と書くが、file 自体は BSD-2-Clause で、使ったのは opcode・値・順序の事実だけ。

### 機械比較（`csim.py`・`cruns.py`・`ccom.py`、[ws218-p001](../../ws218/phase001/phase.md) の道具。接頭辞の表だけを Broadcom 用に替えた写し `scratchpad/ws215p005/`、`python3 -I`）

対象: zedBSD の `broadcom.c`（351 行）と `session.c` の Broadcom の関数（`session_broadcom` 〜 `session_pause`、524 行）。相手: 上の 3 file。対照: zedBSD の `intel.c`（Broadcom と無関係、同じ作り手の形）。

| 比較 | zedBSD Broadcom | 対照 intel.c |
| --- | --- | --- |
| ident、k=12 | 0.03%（1/2917） | 0.00% |
| ident、k=8 | 0.31%（9/2925） | 0.05% |
| ident、k=8、MIND=10 | 0% | 0% |
| norm、k=20、MIND=10（名前を付け替えた写し） | 0% | 0% |
| norm、k=12、MIND=6 | 1.92% | 0.78% |
| comment の 4・5・6 語の一致 | 0 箇所 | — |
| 接頭辞を外した識別子の一致 | 35 語（C の keyword・型と `launch_ram`・`ogf_vendor`・`opcode`・`reset`・`speed` などの仕様の語） | 31 語（同じ種類） |

- ident の 8 token 以上の一致は 5 つで、全部が数の並びか C の定型: 全部 0 の address の表 `{ 0x00, … }`（patchram の BD_ADDR の初期値と）、`session_step(…, SESSION_RESET, NULL, 0U);`（NetBSD の `uart_send_cmd(fd, HCI_CMD_RESET, NULL, 0)` と）、`return 0; } static int`、`&clock, sizeof(clock)`。
- norm の 15 token 以上の一致は 4 つ: `if (x) { y = f(a, b, c); if (y`、関数の終わりと次の関数の頭（`; return 0; } static int f(struct …`）。相手の関数は機能が違う（address の選び ↔ Zephyr の速さの更新など）。
- 構成: zedBSD は純粋な部分（file の検査・表・選び）と session の手順を分け、file を全部検査してから送り、速さの失敗で 115200 の始動をやり直し、kernel の遷移（`BT_IOC_SET_SPEED`）で line の受けを始め直す。Zephyr は lockstep の command の列、patchram は blocking の read の列で、どちらとも分け方が違う。

**結論**: 写しは無い。一致は仕様の値と C の定型だけ。

## 値の出典（2a 節の p005 の行の確定）

| 値 | 使う所 | 出典 | 確かめ |
| --- | --- | --- | --- |
| 製造者 15（Broadcom）・305（Cypress） | `btd_broadcom_manufacturer` | (d) Bluetooth SIG Assigned Numbers（Company Identifiers） | 実機: Read Local Version |
| `0xFC18` Update UART Baud Rate: 引数 6 byte = 0 の 2 byte + baud の 4 byte（LE）。答えは古い速さで来て、その後に host が変える。答えは 100 ms 以内 | `session_broadcom_speed` | (e1) `hci_uart_infineon.c` 57〜66・104〜128 行（datasheet の引用の comment に `01 18 FC 06 00 00 xx xx xx xx` と答え `04 0E 04 01 18 FC 00`）、(e2) `brcm_patchram_plus.c` 176・221〜232・667〜681 行、NetBSD `init_bcm43xx.c` 54・150〜158 行 | host: 台本（引数の byte、ioctl の順）。実機: 3 Mbaud で通る |
| `0xFC45` Set UART Clock: 引数 1 byte（1 = 48 MHz、2 = 24 MHz）、3,000,000 baud 以上で 48 MHz | `session_broadcom_speed`（`0xFC18` の前、断られても続ける） | NetBSD `init_bcm43xx.c` 60・68〜69・143〜149 行（だけ。(e1)(e2) は送らない） | host: 断られても速さが変わる。実機: 送らない時との比べは、3 Mbaud が通らない時だけ |
| `0xFC2E` Download Minidriver（引数なし）と、その後の待ち | `session_broadcom_load` | opcode: (e1) 58・139〜148 行、(e2) 174・630〜640 行、NetBSD 63・122〜125 行。待ち: (e1) は待たない、(e2) は既定で答えの後に 2 byte を読む（`--no2bytes` で読まない、638〜640 行）、NetBSD は 1 秒。**自前の決め**: 50 ms 待ち、続けて `BT_IOC_SET_SPEED`（今の速さのまま）で kernel の受けを始め直す（後に来うる 2 byte を捨て、HUNT が次の答えを anchor にする。来なければ害は無い） | host: 2 byte が残ったまま次の command を送ると kernel が自己 reset する台本で、始め直しが要ることを確かめた（突然変異で FAIL）。実機: 待ちを詰めるかどうか |
| `0xFC4C` Write RAM・`0xFC4E` Launch RAM、`.hcd` の record の形（opcode 2 byte LE + 長さ 1 byte + 引数）。各 record の答えを待つ | `broadcom.c` の検査・`session_broadcom_load` | (e1) 59〜60・150〜186 行、(e2) 642〜655 行、(f) p001 の 2 節（322 個の `0xFC4C` と最後の `0xFC4E`、最長 204 byte） | host: 実 file を検査して 323 record、台本の controller に全部を送り byte まで照合 |
| Launch RAM の答え: (e1) は答えを待つ（来る）。来ない時は **自前の決め** で 1 秒待って進む | `session_broadcom_load` | (e1) 165〜178 行（`bt_hci_lockstep_cmd_send_sync` で待つ） | host: 来ない台本でも READY。実機: `launch=answered` |
| Launch RAM の後に controller が 115200 に戻ること、待ち 270 ms（第 1 版の 250 ms は誤り）、その後に HCI_Reset | `session_broadcom_load` | (e1) 41・282〜298 行（待ち 270 ms → host を既定の速さへ → HCI_Reset）。NetBSD は 4 秒（139 行、115200 のまま読み込む） | 実機: `patched-reset` が通る |
| host を 115200 に戻すのは Launch RAM の答えの **直後**（(e1) は 270 ms の後） | `session_broadcom_load` | p001 の 4 節の 7（kernel の遷移で controller の再起動のゴミを HUNT が捨てる）。速さを変えなかった時も同じ遷移を通す（SYNC のまま再起動のゴミを受けると自己 reset になるため、**自前の決め**） | 実機 |
| HCI_Reset の後も controller の速さと BD_ADDR が残る | 手順（速さは patch の後にもう一度変え、BD_ADDR は core の HCI_Reset の前に書く） | (e1) 300〜320 行の後に Zephyr の host が HCI_Reset から set-up する | 実機: core の Read BD_ADDR が board の address |
| `0xFC01` Write BD_ADDR: 引数 6 byte、最下位の byte から（HCI の順） | `session_broadcom_address` | (e1) 57・130〜136 行（`addr->val` をそのまま）、(e2) 179・286〜302 行（文字列の最後の byte を引数の先頭に） | host: board の address が書かれ core が読み戻す。実機: `bt show` の address |
| NetBSD の `0xFC06`（SET_BDADDR） | 使わない | NetBSD 57 行。(e1)(e2) の 2 つの vendor の code が `0xFC01` で一致するので `0xFC01` | — |
| BCM4345C0 の ROM の既定の address 43:45:C0:00:1F:AC | `broadcom.c` の既定の表 | **推測**（board に address を入れない時の利用者の報告。実機で読む、D3）。全部 0 も既定とみなす | 実機: log の `had=` |
| `BT_IOC_RESET` の notice の待ち 1 s、Write RAM の答え 2 s、Launch RAM の答え 1 s | session の timing | **自前の決め**（2 s は既存の command の時間） | host・実機 |
| 求めていない notice を数える T = 60 秒、3 回で止まる | `btd_retry_*` | **自前の決め**（p001 の中-B） | host |
| 速さの失敗で 1 回だけ 115200 で始動をやり直す | `session_broadcom` | p001 の 4 節の 4 を広げた **自前の決め**（下の判断） | host |
| Read Local Name 0x0C14（名前 248 byte、NUL で終わる） | 表で引く | (d) Bluetooth Core 5.4 Vol 4 Part E 7.3.12 | 実機: log の `chip=` |
| Read Local Version の返り（HCI 版 1、HCI revision 2、LMP 版 1、manufacturer 2、LMP subversion 2） | 製造者と subversion | (d) Core 5.4 Vol 4 Part E 7.4.1 | 実機: `rom=`・`patched=` |

p001 の 2a 節の p005 の行（(e1)(e2) の file・行）はこの表で埋めた（p001 は P3 の記録なので書き換えていない。Q1 が畳むなら p001 の 2a 節から本表へ link を張る）。

## 手順（実装の形）

`btd_session_start`: `GET_INFO` →（Intel の bootloader の path の解除、Intel の始動は今のまま）→ **`bus == BT_BUS_UART` なら `session_broadcom`** → `session_core` → READY。

`session_broadcom`（1 回目は速く、速さを変えた後の失敗なら 1 回だけ 115200 でやり直す）→ `session_broadcom_attempt`:

1. `BT_IOC_RESET`（ENODEV は LOST、他の失敗は ERROR `power-reset …`）。line は 115200。
2. node を読み、notice を見るまで（上限 `reset_ms` 1 s、無ければ ERROR `no reset notice`）と、その後に空になるまで（待たない）全部捨てる。`session_dispatch` を通さないので `resets` に数えない。
3. HCI_Reset → Read Local Version。manufacturer が 15・305 でなければ何もせず `session_core` へ（controller は ROM・115200）。
4. Read Local Name → 表（`BCM4345C0` → `BCM4345C0.hcd`、名前全体の一致）。無ければ FIRMWARE_NEEDED `chip "NAME" has no patch file`（印字できない byte は `?`、31 文字まで）。
5. `<broadcom_folder>/<file>` を読む（無ければ FIRMWARE_NEEDED と path、読めなければ FIRMWARE_FAILED）。全部を検査（record が切れていない、全部 OGF 0x3F、最後が `0xFC4E` で途中に無い）。壊れていれば FIRMWARE_FAILED。ここまで line の速さは変えない。
6. 速い line（`BT_INFO_SPEED` かつ `speed_max` > 115200）: `0xFC45`（48 MHz、断られても続ける）→ `0xFC18`（`speed_max`）→ 答え → `BT_IOC_SET_SPEED speed_max`。
7. `0xFC2E` → 50 ms → `BT_IOC_SET_SPEED`（今の速さ、受けの始め直し）。
8. 各 `0xFC4C` を送り答えを待つ（status ≠ 0 は FIRMWARE_FAILED `record N status 0xSS`、時間切れ・自己 reset は ERROR `record N …`）。
9. `0xFC4E`（答えは 1 s 待つ、来なくても進む）→ 直後に `BT_IOC_SET_SPEED 115200` → 270 ms。
10. HCI_Reset → 6 と同じ速さの切り替え → Read Local Version（LMP subversion を log へ）→ `firmware_loaded`。
11. Read BD_ADDR → `btd_broadcom_address_choice`: 自分の address なら残す、既定（表）で board の address があれば `0xFC01`、board に無ければ既定のまま。
12. `session_core`（HCI_Reset、Read BD_ADDR … 以降は USB と同じ）。

daemon（main.c）: start の log の後に `bluetoothd: broadcom chip=… firmware=loaded|- rom=0x…… patched=0x…… speed=N slow=0|1 launch=answered|unanswered address=own|board|board-refused|default had=XX:…` を 1 行。失敗の数え方は `struct btd_retry`（session.c、下）。

## 判断（設計との違い・細部の決め）

| 項 | 決め | 理由 |
| --- | --- | --- |
| 置き場所 | 純粋な部分は新しい `broadcom.c`・`broadcom.h`（Intel の `intel.c` と同じ形）、手順は `session.c` の static の関数（Intel の手順と同じ所）。`broadcom.c` を Makefile の source に足した | 設計 B14。session の内部（`session_read`・`session_set`・時間）を使う |
| やり直し（4 節の 4 の拡張） | 速さを変えた後（`0xFC45` を送った後）の失敗は **何でも**（時間切れ、自己 reset、status の拒み）1 回だけ `BT_IOC_RESET` から 115200 のままでやり直す。2 回目も失敗すれば、その状態（ERROR か FIRMWARE_FAILED）で返す | `0xFC18` の時間切れだけでなく、3 Mbaud で line が壊れる（minidriver・record が届かない）場合も同じく速さが原因でありうる。始動の 1 回の中で直す |
| `0xFC45` | 送る（NetBSD だけの値）。答えの status ≠ 0・時間切れは無視、自己 reset（ECONNRESET）だけ失敗 | 48 MHz の UART clock を選ばないと 3 Mbaud が通らない chip がありうる（NetBSD の手順）。送らない (e1)(e2) と違うので、実機で `slow=1` になったら最初に疑う |
| minidriver の後 | 50 ms と受けの始め直し | 上の表。(e2) の 2 byte の可能性を kernel の SYNC の自己 reset にしない |
| Launch RAM の後 | 速さを変えなかった時も `BT_IOC_SET_SPEED 115200` の遷移を通す | 上の表 |
| LMP subversion | 前後を log に出すだけで、変わらなくても失敗にしない | 「patch で変わる」の出典が無い。実機の値で決める |
| 失敗の状態 | record の status ≠ 0 は FIRMWARE_FAILED（daemon は開いたまま、RECOVER まで）、line の失敗は ERROR（daemon が 2 秒後に開き直し、始めの `BT_IOC_RESET` から） | UART の時間切れは一時的でありうる。Intel と同じく controller が断った時は直らない |
| `load_sent` | UART では立てない | main の「load の後に node が消え、再び現れる」の待ち（USB の再列挙）は UART に当たらない。UART は毎回 reset から読む（B14） |
| 中-B の数え方 | `struct btd_retry`（session.h・session.c の `btd_retry_clear`・`ready`・`failed`・`reset`・`tick`）に移し、main の `btd_failures`・`btd_stopped` を置き換えた。**READY の始動はすぐには数を 0 にしない**: READY のまま 60 秒続いたら（`btd_retry_tick`）0 にする。READY から 60 秒以内の求めていない notice（`resets` が READY の時より増えた）は 1 つの失敗。3 つで止まる。RECOVER（reopen・reset）が `btd_retry_clear` | p001 の文のまま（「`btd_failures` に足す」）だと、今の main は READY の始動ごとに 0 にするので「1 秒ごとの notice」が永久に止まらない。**USB への影響**: 始動の失敗の後に READY になり、60 秒以内に再び始動が失敗すると、前の失敗も数に残る（前は READY で 0）。USB の controller が 60 秒以内に 3 回失敗する場面だけが変わる |
| 始動の中の自己 reset | 1 回目の始動の途中で kernel の notice が来て 2 回目で READY になった時、その notice は READY の前なので数えない（`btd_ready_resets`） | 中-B の対象は READY の後 |
| clang-format | 適用しない | p003 と同じ（tree の形と規約 §3） |

## 変えた所

| file | 変更 |
| --- | --- |
| `userland/base/bluetoothd/broadcom.{c,h}`（新） | 製造者、chip の表と path、`.hcd` の検査と record、速さの引数、address の選び、既定の address の表 |
| `userland/base/bluetoothd/session.h` | `broadcom.h` の include、`BTD_RETRY_FAILURES_MAX`・`BTD_RETRY_STEADY_MS`、timing の `reset_ms`・`minidriver_ms`・`launch_ms`・`settle_ms`、`struct btd_retry` と関数、session の Broadcom の欄（`broadcom_folder` ほか） |
| `userland/base/bluetoothd/session.c` | UART の分岐、`session_broadcom*`、`session_pause`、`btd_retry_*`、Read Local Name の opcode、timing の既定 |
| `userland/base/bluetoothd/main.c` | `btd_failures`・`btd_stopped` → `btd_retries`、`btd_count_reset`、`btd_log_broadcom`、READY の 60 秒の tick、header の comment |
| `userland/base/bluetoothd/Makefile` | `broadcom.c`、comment |
| `plan/ws215/tests/bt-broadcom-host-test.{c,sh}`（新） | host 試験（下） |

kernel・UAPI・HAL・toolchain・`bt`・`bt-probe` は変えていない（`bt`・`bt-probe` は build だけ）。

## 実行したコマンドと結果（agent/p2-ws215、main 0f4c63888 の上）

| コマンド | 結果 |
| --- | --- |
| `BRCMBT_CACHE=<p3 の worktree の検証済みの cache> sh plan/ws215/tests/bt-broadcom-host-test.sh` | `bt-broadcom-host-test: PASS (80 checks)`、`the real BCM4345C0.hcd loaded (323 records)`（ASan・UBSan、`-Wdeclaration-after-statement`） |
| 突然変異（scratchpad の写しで minidriver の後の受けの始め直しを外す） | load の 10 個の check が FAIL（`record 0 Connection reset by peer`、試験が効く） |
| `plan/ws143/tests/bt-daemon-host-test.sh` の写し（cc の行に `broadcom.c` を足しただけ、`build/tmp/ws215-p005-other.*`） | daemon 90・pair 161・link 56・hidhost 96・hog 24 の checks PASS（hid も FAIL 無し、Intel の実 file の段は cache が無く skip）。最後の版で bt-daemon-host-test を再び PASS（90） |
| `plan/ws197/tests/bt-phone-host-test.sh` の写し（同じ） | 20 本全部 0 failed（session 51・router 51・pair-phone 77・linkuse 38・phone-link 214 ほか） |
| `make -o <wt>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=plan/ws215/tests/config-rpi4-bt.mk build/arm64/bin/bt-probe build/arm64/bin/bluetoothd build/arm64/bin/bt` | rc 0、warning 0（`check-dynamic-elf.py` を通る） |
| `make -o <wt>/build/amd64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-amd64.mk build/amd64/bin/bt-probe build/amd64/bin/bluetoothd build/amd64/bin/bt` | rc 0、warning 0 |
| `git diff --check` | clean |

host 試験の内容（`bt-broadcom-host-test.c`）: 純粋な部分（製造者 3、chip の表 5（既知、長い名前、印字できない byte、満ちた名前の切り詰め、長すぎる path）、速さの引数、address の選び 5、`.hcd` の検査 8）、失敗の数え方 11（3 回の失敗で止まる、READY の 1 秒後の reset 3 回で止まる、RECOVER、60 秒の steady、tick、READY でない時の reset）、台本の controller（socket pair、kernel の ioctl の代わり: `BT_IOC_RESET` で ROM・両端 115200・notice、`SET_SPEED` で host の端、両端の速さが違えば controller は答えない）: 速い load（command の順、ioctl の順、5 record の byte の照合、board の address、48 MHz の clock、notice の前後の古い packet）、速さの無い transport、`0xFC18` の無応答 → 115200、3 Mbaud で答えない controller → 115200、`0xFC45` の拒み、Launch RAM の無応答、自分の address、board の address 無し、record の拒み（2 回試して FIRMWARE_FAILED）、load の途中の自己 reset → 2 回目で READY、`BT_IOC_RESET` の失敗、notice が来ない、file 無し、表に無い chip、壊れた file、manufacturer 2、Cypress 305、READY の後の notice（ERROR・`resets`・数え方）、実の BCM4345C0.hcd の 323 record の検査と 322 個の Write RAM の照合。

## 未実施

- Pi の実機（下の依頼）。QEMU は BT を emulate しないので QEMU の試験は無い（T1 の QEMU raspi4b の起動の回帰は p003 の T1-566 で PASS 済みで、この Phase は kernel を変えていない）。
- main.c の新しい数え方は host で動かしていない（main.c は host で build できない。数え方の純粋な部分 `btd_retry_*` と、READY の後の notice で session が ERROR になり `resets` が増えることを host で確かめた）。
- `0xFC45` を送る・送らないの実機の比べ、minidriver の後の待ちの詰め。

## 実機で見る点（Pi 4、10.0.30.2、T1・ユーザー）

前提: p003 の実機の smoke（電源・115200 の HCI）と同じ image（p003・p004・p005 が入った main、`config/ci/config-rpi4.mk` の image に `brcmbt-firmware` が入ること）。届け方は p003 の「実機で見る点」と同じ。

1. **firmware の file**: `ls -l /lib/firmware/brcm/BCM4345C0.hcd`（63806 byte）。
2. **daemon の log**（`/var/log` の bluetoothd、または service の log）に、始動のたびに次の 2 行:
   - `bluetoothd: broadcom chip=BCM4345C0 firmware=loaded rom=0xRRRR patched=0xPPPP speed=3000000 slow=0 launch=answered address=board had=XX:XX:XX:XX:XX:XX`
   - `bluetoothd: /dev/bluetooth0 state=ready  (error 0, …)`
   記録する値: `chip=`（ROM の名前。`BCM4345C0` でなければ FIRMWARE_NEEDED の理由に名前が出る → 表に足す）、`rom=`・`patched=`（LMP subversion の前後）、`had=`（patch の後・書く前の address。表の 43:45:C0:00:1F:AC と同じか → D3）、`slow=`（1 なら 3 Mbaud が通らなかった: `0xFC45` を疑う）、`launch=`、`address=`。
3. **kernel の log**（`dmesg | grep rpi4-bt`）: `in step, N bytes passed over` の N（始動の遷移ごと）、自己 reset の行が無いこと、`counts` の行の受けの誤り・捨てた byte。
4. **`bt show`**: `STATE ready`、`CONTROLLER node=/dev/bluetooth0 … name="Raspberry Pi 4 Bluetooth" address=<board の address> … manufacturer=15|305 … firmware=loaded`。address が DT の `local-bd-address`（firmware が入れる値、kernel の `bt-probe` の `board-address` の行で見られる）と同じこと。
5. **`bt scan 10`**: 周りの機器（phone、keyboard など）が名前つきで出る。
6. **始動の長さ**: daemon の log の時刻で、node の open から READY まで（3 Mbaud で 1〜2 秒の見込み）。
7. **やり直しの経路**: `bt reopen` と `bt reset` で再び READY（firmware を読み直す、log の 2 行がもう一度。`bt reset` は電源の入れ直しが 2 回、p001 の軽微の記録どおり）。bluetoothd の再起動でも同じ。
8. **版の読み戻し**: `bt show` の後に bluetoothd を止めて `bt-probe`（`-r` を付けない。HCI_Reset と Read Local Version、kernel の line は daemon が残した 3 Mbaud のまま）で LMP subversion が `patched=` と同じ（HCI_Reset では patch は消えない見込み、電源の入れ直し（`-r`）で ROM の `rom=` に戻る）。
9. 失敗した時に持ち帰る物: 上の log の行、`bt show` の STATE の理由、`dmesg | grep -E 'rpi4-bt|bt-hci'`。

## T1 への依頼（Q1 経由）

- **QEMU**: 無し（この Phase は userland だけ。QEMU raspi4b は BT を emulate しない。amd64 の USB の bluetoothd の経路は host 試験で確かめた）。
- **Pi の実機**: 上の 1〜9。p003 の実機の smoke と同じ機会にまとめてよい（p003 の smoke は bluetoothd を止めて bt-probe、この Phase は bluetoothd を動かしたまま）。合否: 2 の `firmware=loaded` と `state=ready`、4 の `firmware=loaded` と board の address、5 で 1 台以上の機器。`slow=1`・`launch=unanswered`・`address=default` は合否に入れず値を記録する（判断の材料）。

## 残り・知らせ（Q1 へ）

- **他の WS の host 試験の script**: `session.c` が `broadcom.c` を呼ぶので、`plan/ws143/tests/bt-daemon-host-test.sh`（bt-daemon・bt-link・bt-hidhost の cc の行）と `plan/ws197/tests/bt-phone-host-test.sh`（session・router・pair-phone・linkuse・phone-link の行）の cc の行に `userland/base/bluetoothd/broadcom.c` を足す必要がある（他の WS の plan なので Q1 に依頼。`sed 's#userland/base/bluetoothd/session.c#& userland/base/bluetoothd/broadcom.c#g'` で写した script で両方 PASS を確かめた）。
- p001 の 2a 節の p005 の行は、この Phase の「値の出典」で埋めた（第 1 版の 250 ms は 270 ms が正しい）。
- D3 の残り: 実機の `had=` を見て、既定の address の表を直す（Q1 が結果を返したら P2 が直す）。
- p006（実機の統合）・p008（UART の SCO）はこの Phase の後。
