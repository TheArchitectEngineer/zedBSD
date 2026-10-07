<!-- awesome-plan project=zedbsd record=ws052p007 -->

# ws052-p007: Keiland の 3 つの契機と、中止の理由の表示（設計）

Phase ID: `ws052-p007`
Parent: [WS052](../ws.md)
Status: planning（2026-10-07 P2: 設計の第 2 版。p006 の今の main（`KERN_SYSTEM_SLEEP` の S0 idle の mode、未対応は ioctl の `EOPNOTSUPP`、`KERN_SYSTEM_SLEEP_INFO` は無い）に合わせて直した。design-reviewer の後、人の判断 N1〜N7 を Q1 へ。code は ACK の後）（旧: 2026-10-05 P1 generation17 の第 1 版、§7 はユーザーが案のとおり決定）
Phase disposition: normal
Queue: 2026-10-07 Q1 の P2 への指示（設計の直し → ACK の後に実装）

## 第 2 版の変更（2026-10-07）

- **§6 を書き直した**: 第 1 版は p006 に無い `KERN_SYSTEM_SLEEP_INFO` で「sleep が使えるか」を読む前提だった。今の main では ioctl そのものが
  支えの無い platform で `EOPNOTSUPP`（何も触らない）を返すだけで、事前に問う口は無い → 案 A（`KERN_SYSTEM_GET_POWER` の `known` に 1 bit、
  UAPI の追加で承認が要る）と案 B（UAPI を変えず、最初の試みの `EOPNOTSUPP` を sessiond と backend が覚える）を並べ、A を勧める（N1）。
- 今の main に合わせた: sessiond の `POWER` は session からも受けている（ws131-p027、root・wheel だけ）、通知（WS156 p002、`kwl_notify_post_system`）が
  在る、無操作の lock（`--lock-idle`、既定 10 分、`shell.c` の tick）が在る、sleep button の事象が在る（`KL_BACKEND_BUTTON_SLEEP`）、networkd の
  socket は binary の opcode（`userland/base/net/protocol.h`）、kernel の事象は class POWER・action CHANGE・subject `sleep.begin`・`end`・`failed`。
- 加えた: sessiond は ioctl を子 process で行い待ちの間も loop を止めない（§1）、起こした電源ボタンの押下で直ぐ眠り直さない（§3.2）、lock の画面を
  描いてから頼む（§3.2）、中止の理由は lock の画面の card の文の行と system の通知（§4）、Settings の頁（§8）、変える file の一覧（§9）。

## 範囲

- Keiland（compositor）が S0i3 に入る 3 つの契機: **蓋を閉じた時**、**電源ボタンの短押し**、**一定時間の無操作**（§10 の決定、2026-10-05 ユーザー）。
- 入る前の準備と戻った後: session の lock、**networkd が sleep の前に Wi-Fi の radio を切り、戻ったら元に戻す流れ**（p005 の AX211 は radio が on だと
  中止するため）。
- **中止の理由の表示**: kernel が返す原因の device と error を、利用者の言葉にして見せる。
- **Settings の Power の頁**: 無操作で sleep するまでの時間（§8）。
- 範囲外: kernel の S0i3 の入口・出口（p006）、通知の仕組みそのもの（WS156 p002 で在る物を使う）、HDA・Wi-Fi の本当の suspend（後）、idle-inhibit の protocol（§3.1）。

## 今の形（2026-10-07 の main を読んだ）

| 部分 | 今 | p007 で変えること |
| --- | --- | --- |
| kernel の口（p006、`include/uapi/system.h`） | `KERN_SYSTEM_SLEEP`（`struct system_sleep_request`、64 byte）の mode `KERN_SYSTEM_SLEEP_S0IDLE`。root だけ（EPERM）、支えが無ければ ioctl が `EOPNOTSUPP`（何も触らない）、進行中なら `EBUSY`。それ以外は成功で `result`（0 = 眠った、EBUSY = 拒んだ device、EOPNOTSUPP = suspend の口の無い driver）・`resume_result`・`wake`（`KERN_SYSTEM_WAKE_*`）・`device`（`"pci 0000:00:14.3 intel-ax211"`）。事前に「眠れるか」を問う口は無い | 変えない（案 A を選べば `KERN_SYSTEM_GET_POWER` に 1 bit、§6） |
| 蓋（`wayland/lid.c`・`backend-host.c` の `kwl_backend_lid_changed`、ws132-p008） | 閉じると lock して画面を消す（`KWL_LID_LOCK`・`KWL_LID_SCREEN_OFF`）、15 分以内に開けると password 無しで unlock（D2 の猶予） | sleep が使える時は「lock → 画面を消す → sleep を頼む」。外部の monitor に出している時は今のまま（sleep しない）。開けた時の規則は今の猶予のまま |
| 電源ボタン・sleep button（`kwl_backend_power_button`、ws132-p003） | log だけ（`KWL EVENT power button`・`sleep button`） | 短押しで lock して sleep（D1: dialog 無し）、sleep button も同じ（N5）。起こした押下は捨てる（§3.2）。長押しは firmware の強制断で触らない |
| 無操作 | `--lock-idle`（既定 10 分）で lock だけ（`shell.c` の tick、lock の間は数えない）。最後の入力は `server->lock_input_ms` | 新しい `idle.c` が lock の間も数え、設定の時間の半分で画面を消し、時間で lock して sleep。今の 10 分の lock は残す |
| 電源の操作（`kl_backend_power_action`、`power-zedbsd.c`） | `POWER poweroff|reboot` を greeter か session の descriptor で送る。`SUSPEND` は定義だけ（`power_actions` は poweroff・reboot の bit だけ）。session は root・wheel だけ | `POWER suspend` を足す（greeter と、session の利用者は誰でも、N2）。答えの詳しさ（起床の理由、拒んだ device）を backend が保ち、compositor が読む（§1.2） |
| sessiond（`power.c`・`power-rules.c`・`session.c`・`greeter.c`） | `POWER` は `sessiond_power_run` で `/sbin/poweroff` などを fork して直ぐ `OK` | 新しい `sleep.c`: 子 process で networkd の準備 → ioctl → networkd の終わり、結果を pipe で親へ。親は session と greeter の loop の poll に pipe を足し、答えを返す（§1.1） |
| networkd（`userland/base/networkd/main.c`、protocol は `userland/base/net/protocol.h` の opcode） | Wi-Fi を止める `stop_wlan_radios(radios, count, lower)` が在る（disconnect・search-stop・down、状態を確かめる）。sleep を知る口は無い | 新しい opcode `NETWORKD_OP_SLEEP_PREPARE`・`NETWORKD_OP_SLEEP_END`（root だけ、§2） |
| 通知（WS156 p002、`notify-shell.c` の `kwl_notify_post_system`） | compositor 自身の通知（client 0）を出せる。lock の画面と greeter の上には出ない（`kwl_glass_draw` は lock の間 `kwl_greeter_draw` だけ） | 中止の理由を lock・greeter の card の文の行（`greeter_message`）に出し、同じ文を system の通知にも残す（§4、N3） |
| Settings（`userland/desktop/settings/pages.c`） | 「Battery」の頁は `se_soon_draw`（未実装の印） | 「Power」の頁にして、sleep までの時間（電源・電池）を選ぶ（§8、N4） |

## 設計

### 1. 経路（誰が何をするか）

```
Keiland（compositor、利用者の session か greeter）
  契機（蓋・電源ボタン/sleep button・無操作）→ lock（session だけ）→ lock の画面が 1 frame 出た後
  → kl_backend_power_action(SUSPEND) → "POWER suspend\n"（session か greeter の descriptor）
sessiond（root、session か greeter の loop）
  sleep.c: pipe と子 process を作り、loop は poll を続ける
  子: 1. networkd に SLEEP_PREPARE（上限 8 秒で答えを待つ）
      2. ioctl(/dev/system, KERN_SYSTEM_SLEEP, S0IDLE) … S0i3 … wake
      3. networkd に SLEEP_END（答えを待たない）
      4. 1 行を pipe へ書いて終わる
  親: 1 行を読み、session（か greeter）に答える
Keiland
  OK: 起きた。画面を点け（蓋が閉じていなければ）、lock の画面のまま（蓋の wake なら lid.c の猶予）
  FAIL: lock の画面の文の行と通知に理由（§4）、§5 の抑止
```

- **sessiond が仲介する理由**: `KERN_SYSTEM_SLEEP` は root だけ。compositor は電源の操作を sessiond に頼む今の形（ws131）に揃える。sessiond は
  自分が作った session の control の socket（descriptor 3）と greeter の socket からだけ受ける（今の `POWER` と同じ認証）。
- **子 process にする理由**: networkd の答えを待つ間（最大 8 秒）と眠っている間、sessiond の loop（greeter の login、passkey の exchange、
  session の終わりの検出）を止めない。眠っている間は kernel が user を止めるので、子の ioctl が戻るまで親も止まる（問題ない）。
- **networkd を同期で呼ぶ理由**: radio を切り終える前に kernel が device を suspend すると AX211 が `EBUSY`（`net_opened`）で中止する。kernel の
  事象（`sleep.begin`）で networkd に知らせる形は順序を保証できない。kernel の事象は表示と記録のためだけ。
- **audiod は何もしない**: HDA は p005 で driver が stream を止めて保ち、resume で再開する（`pci-hda.c`）。docs/architecture/power-management.md の
  「audiod closes streams」は今の実装と違う（Q1 へ: docs を直すか、audiod に準備を足すか）。

#### 1.1 sessiond の答え（1 行）

| 答え | 意味 | backend の `session_answer(POWER, error)` |
| --- | --- | --- |
| `OK woke=<理由>` | 眠って起きた。理由は `power-button`・`lid`・`keyboard`・`usb`・`ac`・`timer`・`spurious`・`other`（kernel の事象の名前と同じ） | 0 |
| `FAIL unsupported` | ioctl が `EOPNOTSUPP`（platform が眠れない） | `EOPNOTSUPP` |
| `FAIL device error=<errno の名前> device=<device>` | 眠らずに戻った（`result` が 0 でない）。device は kernel の文字列（空白を含むので行の最後） | `EBUSY` か `EOPNOTSUPP`（`result` のまま） |
| `FAIL network radio=<if> error=<errno の名前>` | networkd が radio を止められなかった（ioctl はしない） | `EBUSY` |
| `FAIL busy` | 他の sleep が進行中（ioctl の `EBUSY`、または sessiond が既に子を持つ） | `EBUSY` |
| `ERROR` | それ以外（pipe・fork の失敗、子の異常な終わり） | `EIO` |

- `resume_result` が 0 でない時は `OK` のまま、sessiond の log と syslog に `resume_result` と device を残す（利用者に見せる手段が無いので log だけ）。
- 子の終わりの待ちの上限は無い（眠っている時間は利用者が決める）。子が死んだら（`waitpid`）`ERROR`。

#### 1.2 backend（`libkeiland-backend`）

- `power_actions` に `KL_BACKEND_POWER_SUSPEND` の bit を足す: greeter の descriptor か session の descriptor があり、sleep が使える時（§6）。
  session の利用者が wheel でなくてもよい（N2）。poweroff・reboot は今のまま。
- `kl_backend_power_action(SUSPEND)` は `POWER suspend` を送る。答えの行の詳しさを `struct kl_backend_power_outcome`（error・wake の理由の番号・
  device の文字列・radio の名前）として backend が保ち、新しい `kl_backend_power_outcome(backend, &outcome)` で compositor が読む
  （`session_answer` の引数は error だけなので）。答えが来たら成功でも `power_asked` を 0 に戻す（poweroff・reboot は戻らないので今は失敗の時だけ）。
- Linux・FreeBSD の backend: Linux は logind の Suspend を今のまま（outcome は error だけ）、FreeBSD は ENOTSUP。

### 2. networkd の SLEEP_PREPARE・SLEEP_END

- 新しい opcode（`userland/base/net/protocol.h`、空いている番号）: `NETWORKD_OP_SLEEP_PREPARE`・`NETWORKD_OP_SLEEP_END`。`operation_allowed` の
  member の一覧に入れない（root だけ = sessiond）。
- `SLEEP_PREPARE`: 有効な Wi-Fi の radio を記録し、`stop_wlan_radios(radios, count, 1)`（disconnect・search-stop・down、状態の確かめ）で止める。
  答えは OK と止めた数、失敗は ERROR と errno と最初の interface の名前。有線は触らない（suspend の口の無い有線の NIC の driver は kernel が
  `EOPNOTSUPP` で中止し、§4 の言葉になる）。
- `SLEEP_END`: PREPARE で止めた radio を元の状態（up と自動接続の方針）に戻す。止めていなければ何もしない。
- PREPARE から END まで自動の仕事（`run_automatic_work`、自動接続・scan）を止め、利用者の Wi-Fi の要求（`WIFI_*`）は `EBUSY` で断る。
- END が来ないまま 10 分経ったら（sessiond の子が死んだ等）、自分で END と同じことをする（安全側）。
- 進行中の Wi-Fi の仕事（`wifi_pending`）がある時の PREPARE は、その仕事の終わりを待ってから止める（待ちの上限は sessiond の 8 秒の内）。

### 3. 3 つの契機（compositor）

| 契機 | 条件 | 動作 |
| --- | --- | --- |
| 蓋を閉じた | session（greeter でない）で、sleep が使え（§6）、外部の monitor に出していない | lock（今の `KWL_LID_LOCK`）→ 画面を消す → sleep を頼む。開けて起きたら今の猶予（15 分以内なら password 無しで unlock） |
| 電源ボタン・sleep button の短押し | session でも greeter でも、sleep が使える | session なら lock → sleep。greeter なら sleep だけ |
| 無操作 | 最後の入力（`lock_input_ms`: key・pointer・touch・pen）から設定の時間（電源と電池で別、§7 の 2）。0 は「しない」。lock の間も数える | 時間の半分で画面を消す（入力で点く、lock しない）。時間で lock（まだなら）→ sleep |

- **sleep が使えない時**（§6）: 蓋は今の「lock して画面を消す」、電源ボタンは log だけ（今のまま）、無操作は半分で画面を消すだけ（今の 10 分の lock は残る）。
- 契機が重なった時: sleep の要求が 1 つ出ている間（答えを待つ間、`power_asked`）は次を出さない（`kl_backend_power_action` が `EBUSY`）。

#### 3.1 無操作の抑止

- 全画面の窓（`fullscreen`）が一番上にある間は数えない（動画）。
- 範囲外（backlog、`plan/ws177/backlog-p2.md`）: Wayland の idle-inhibit の protocol（compositor に無い）、音を出している間（compositor は audiod の
  stream を知らない）。N7。

#### 3.2 順序と起床

- **lock の画面を描いてから頼む**: lock した後、lock の画面の 1 frame が出た（`kwl_schedule` の次の present の後）か 200 ms 経ってから
  `POWER suspend` を送る。起きた時の最初の画面が desktop にならないため。
- **起こした押下を捨てる**: 電源ボタンで起きると、その押下が kernel の事象（PRESS）として後から届き得る。答え（`OK`）を受けてから 2 秒の間の
  電源ボタン・sleep button の押下は log だけにする（`KWL SLEEP ignore button`）。
- 起きた後: 画面を点ける（蓋が閉じていれば点けない）。最後の入力の時刻を今にする（直ぐ無操作で眠り直さない）。蓋の wake は lid の open の事象が
  別に来て lid.c が猶予を決める。

### 4. 中止の理由の表示

- backend の outcome から compositor が言葉を選ぶ（翻訳の対象、`wayland.keys` と `ja/wayland.tr`）:

| 原因 | 言葉（英語の基） |
| --- | --- |
| `FAIL device`、device が `intel-ax211`・`rtl8822b` などの Wi-Fi で EBUSY | "Sleep was cancelled: Wi-Fi could not be turned off." |
| `FAIL network` | "Sleep was cancelled: Wi-Fi could not be turned off." |
| device が `nvme` で EBUSY | "Sleep was cancelled: the disk was busy." |
| device が `xhci` で EBUSY | "Sleep was cancelled: a USB device was busy." |
| device が `i915` で EBUSY | "Sleep was cancelled: the display was busy." |
| `result` が EOPNOTSUPP（suspend の口の無い driver） | "Sleep was cancelled: DRIVER cannot sleep yet."（DRIVER は device の文字列の最後の語） |
| `FAIL unsupported` | "This computer cannot sleep." |
| `FAIL busy` | 何も出さない（他の sleep が進行中） |
| それ以外 | "Sleep was cancelled (DEVICE, error E)." |

- 出し方（N3）: lock の画面か greeter が出ていれば、その card の文の行（`greeter_message`、今の「The login failed.」と同じ所）に出す（新しい
  `kwl_greeter_say`）。同じ文を system の通知（`kwl_notify_post_system`、title "Sleep"）にも出し、unlock の後に通知の log で読める。lock の画面も
  greeter も出ていない時（§6 の案 B の最初の電源ボタンなど）は通知だけ。
- 中止の記録は sessiond の log（`SESSIOND SLEEP …`）と syslog、kernel の dmesg（`system: sleep …`）。

### 5. 再試行と連続の抑止

- 中止の後、蓋と無操作は 30 秒は試さない（蓋を閉じたままの繰り返し、無操作の繰り返しを防ぐ）。電源ボタンは押すたびに試す（利用者の明示の操作）。
- 蓋を閉じたまま中止が続く時は、今の「lock して画面を消した」状態に留まる。
- `FAIL unsupported` を受けたら、その compositor の process の間は sleep が使えないとみなす（§6）。

### 6. sleep が使えるかの判断（N1）

p006 には「眠れるか」を問う口が無く、ioctl が支えの無い platform で `EOPNOTSUPP` を返すだけ。

- **案 A（勧める、UAPI の追加で承認が要る）**: `KERN_SYSTEM_GET_POWER` の `known` に `KERN_SYSTEM_POWER_HAS_SLEEP 0x8U` を足し、kernel が
  `kern_sleep_supported()` の時に立てる（構造体は変えない、`system-device.c` の数行と `system.h` の 1 行）。backend の `power_read` が読み、
  `power_actions` の SUSPEND の bit に載せる。Keiland は起動時から分かり、§3 の「使えない時」の動きを最初から選べる。Settings の Power の頁も
  「This computer cannot sleep.」を出せる。
- **案 B（UAPI を変えない）**: 最初は「使える」とみなし、最初の試みの `FAIL unsupported` で backend が覚えて SUSPEND の bit を落とす。sessiond も
  ioctl の `EOPNOTSUPP` を覚え、次からは networkd に頼まずに直ぐ `FAIL unsupported`。欠点: 支えの無い機械（QEMU を含む）で、最初の電源ボタンで
  session が lock され、通知「This computer cannot sleep.」が 1 度出る（今は log だけ）。Wi-Fi のある機械では最初の 1 度だけ radio が切れて戻る。
  ws132 の QEMU の試験（電源ボタンが log だけ）は最初の 1 度で変わる。

### 7. 人間の判断

#### 7.1 決定済み（2026-10-05 朝、ユーザーが案のとおり、Q1 の中継）

1. 外部の monitor に出している時に蓋を閉じた: **sleep しない**（内蔵の panel だけ消し、外部に出し続ける）。**AC の有無に依らない**。
2. 無操作の時間の既定値: **AC 30 分・電池 15 分**、画面はその**半分**で消す。0 は「しない」。Settings の Power の頁で変える。
3. greeter で電源ボタン: **sleep**（lock は無い）。
4. sessiond が **session からも suspend を受ける**（ws131 の D12 の改訂。poweroff・reboot は今の規則のまま）。

#### 7.2 新しい判断（第 2 版、既定の案）

- **N1**（§6）: sleep が使えるかの判断。**案 A**（`KERN_SYSTEM_GET_POWER` の `known` に `KERN_SYSTEM_POWER_HAS_SLEEP`、UAPI の 1 bit の追加）を勧める。
  案 B は UAPI を変えないが、支えの無い機械の最初の電源ボタンで lock と通知が 1 度出る。
- **N2**: suspend を頼めるのは、greeter と、**session の利用者なら誰でも**（wheel でなくてよい。蓋を閉じた利用者の機械が眠らないのは困る）。
  poweroff・reboot は今のまま root・wheel だけ。
- **N3**: 中止の理由は lock の画面・greeter の card の文の行と system の通知に出す（第 1 版の「画面の下の中央の 5 秒の toast」の代わりに、在る
  部品を使う）。
- **N4**: Settings の「Battery」の頁（今は未実装の印）を「Power」の頁にし、sleep までの時間を電源・電池で選ぶ（Never・5・10・15・30・60・120 分、
  既定 30・15）。画面を消す時間はその半分で、選ばない（文で書く）。電池の残りの表示は後（backlog）。
- **N5**: sleep button（`KL_BACKEND_BUTTON_SLEEP`）も電源ボタンの短押しと同じ（lock して sleep）。
- **N6**: 今の無操作の lock（`--lock-idle`、10 分）は残す。電池の既定（15 分）だと画面を消すのが 7.5 分で lock の 10 分より早いが、画面を消すのは
  lock しない（入力で点く）。
- **N7**: 無操作の抑止は全画面の窓だけ（idle-inhibit の protocol と音の再生中は backlog）。

#### 7.3 第 1 版の判断の案の記録（決定の前、2026-10-05）

1. 外部の monitor に出している時に蓋を閉じた: 案は sleep しない。AC の有無で変えるか。
2. 無操作の時間の既定値: 案は AC で 30 分、電池で 15 分（画面を消すのはその半分）。0 で「しない」。
3. greeter で電源ボタン: 案は sleep。Shut Down の確認を出す案（ws132 の D1 の元の案）もある。
4. sessiond が session から suspend を受けること。

### 8. Settings の Power の頁（N4）

- 鍵（`settings-keys.c`、resolver compositor、KEPT）: `power.sleep.ac`（INT、0〜240 分、既定 30）、`power.sleep.battery`（INT、0〜240、既定 15）。
  0 は「Never」。compositor の `settings.c` が読み、`idle.c` に渡す（`KWL PREFERENCES key=power.sleep.ac applied value=N` の log）。
- 頁（`settings/pages.c` の `SE_PAGE_BATTERY` の項を title "Power"・説明 "Sleep and the screen." に、新しい `page-power.c`）: 2 つの選択
  （"Sleep after, on the power adapter"・"Sleep after, on battery"）と、文 "The screen turns off after half that time."。sleep が使えない時
  （`kl_system_power_get_state` の actions に SUSPEND が無い、案 A）は文 "This computer cannot sleep." と、選択は画面を消す時間として効く
  （文を "Turn the screen off after" に）。
- 翻訳: `settings` の keys・ja。

### 9. 変える file（実装の目安）

| 部分 | file |
| --- | --- |
| （案 A のみ）kernel・UAPI | `include/uapi/system.h`（1 行）、`src/drivers/generic/system-device.c`（GET_POWER で bit） |
| networkd | `userland/base/net/protocol.h`（2 opcode）、`userland/base/networkd/main.c`（PREPARE・END・自動の仕事の停止・10 分の安全） |
| sessiond | 新しい `sleep.c`（子・pipe・答えの行）、`sleep-rules.c`（答えの行を作る純粋な関数、host 試験）、`session.c`・`greeter.c`（`POWER suspend` と poll の pipe）、`power-rules.c`（suspend は wheel を問わない） |
| backend | `libkeiland-backend/keiland-backend.h`（outcome の struct と関数）、`libkeiland-backend-zedbsd/power-zedbsd.c`（SUSPEND の bit と送信、案 A の bit）、`session-zedbsd.c`（答えの行の解釈） |
| compositor | 新しい `sleep.c`・`sleep.h`（契機の判断・抑止・起こした押下の窓・理由の言葉、server を知らない純粋な部分は host 試験）、新しい `idle.c`・`idle.h`（無操作の時間・画面を消す・sleep、純粋）、`backend-host.c`（電源ボタン・蓋から呼ぶ）、`lid.c`（外部の monitor の時の判断に引数）、`shell.c`（tick で `kwl_idle_tick` を lock の判断の前に 1 行）、`greeter.c`（`kwl_greeter_say`）、`settings.c`（2 つの鍵）、`locale`（言葉） |
| Settings | `settings/pages.c`、新しい `page-power.c`、`settings-keys/settings-keys.c`（2 つの鍵） |

- WS181 で変えた `shell.c`・`home.c`・`arrange-shell.c` は最小限（`shell.c` の tick に 1 行だけ、他は新しい file）。
- KL_VERSION: `kl_system_power_get_state` は在るので、libkeiland の公開の API は変えない（上げない）。

## 依存

- p006: `KERN_SYSTEM_SLEEP` の S0 idle の mode と事象（main に在る、QEMU は T1-178 で拒否まで確認、5330 の UAT 待ち）。案 A は UAPI の承認。
- WS132 p008（蓋の lock の今の形）、WS089（Settings）、WS156 p002（通知、在る）。

## 確かめ方（code の後）

- host の試験: compositor の `sleep.c`・`idle.c`（蓋・電源ボタン・無操作・抑止・30 秒の抑止・起こした押下の 2 秒・使えない時・外部の monitor）、
  sessiond の答えの行（`sleep-rules.c`: result・wake・device・errno の組から行、行から outcome）、compositor の理由の言葉の選び方、networkd の
  PREPARE・END の状態（記録した radio、二重の PREPARE、END だけ、10 分の安全）を切り離した関数で。
- QEMU（T1）: sleep が使えない platform で、案 A なら電源ボタン（`system_powerdown`）が log だけ・蓋は無い・無操作で半分の時間に画面が消える
  （`--lock-idle` と同じく試験の短い時間の option で）。案 B なら最初の電源ボタンで `FAIL unsupported` と通知、2 回目は log だけ。sessiond の
  `POWER suspend` の答え（`FAIL unsupported`）と networkd の PREPARE・END の往復（Wi-Fi の無い guest では `OK` と 0）。
- 実機（5330、ユーザーの UAT）: 3 つの契機で入り、蓋・電源ボタンで戻る。Wi-Fi の接続が戻る。中止の理由（Wi-Fi を切れなくする等で起こす）。
