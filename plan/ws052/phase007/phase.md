<!-- awesome-plan project=zedbsd record=ws052p007 -->

# ws052-p007: Keiland の 3 つの契機と、中止の理由の表示（設計）

Phase ID: `ws052-p007`
Parent: [WS052](../ws.md)
Status: planning（2026-10-07 P2: 設計の第 4 版。design-reviewer の 1 回目（blocking 10）を第 3 版で、2 回目（blocking 9）を第 4 版で反映。人の判断 N1〜N10 を Q1 へ。code は ACK の後、実装は §10 の 4 つの Phase に分ける案）（旧: 2026-10-05 P1 の第 1 版、2026-10-07 P2 の第 2・3 版）
Phase disposition: normal
Queue: 2026-10-07 Q1 の P2 への指示（設計の直し → ACK の後に実装）

## 版の変更

- **第 4 版（2026-10-07）**: 2 回目の review の NB1〜NB9 を反映。opcode を 80・81 に（64 は SUBSCRIBE）、lock の 2 frame は PENDING の間 毎 tick dirty にして数える（§0.4）、`POWER cancel` は答えの無い行で 2 本目の pipe（§1.1・§3.4）、蓋が閉じていれば lid 以外のどの理由でも眠り直し、蓋の level は backend の `lid` で合わせる（§3.1）、networkd は自動の仕事を中止させ END は断らず予算を 1 つの式に（§2）、END で戻す方針の表（§2）、`power_asked` は SUSPEND の答えの時だけ戻す（§1.2）、session_gone で WAITING を抜ける（§1.2）、N10 の材料を A1 と合わせて書き直し `none` を足した。non-blocking のうち EALREADY は SUSPEND だけ、outcome の kind、resume の device は無い、backend の定数、FreeBSD の file、PENDING・WAITING の鍵、END を答えの前に・SESSION_OPEN/CLOSE は受ける、confirmed の理由、速い起床の抑止、AC は時計だけ、handoff の間の 2 つの compositor、恒久の扱い、Linux は今のまま、A1 の名前、試験の抜け、§10 の依存、docs の食い違い、蓋の閉じた画面、os_paused も入れた。
- **第 3 版（2026-10-07）**: review の B1〜B10 を反映。答えの経路を login の答えと分けた（§1.2）、起こした押下と要求の間の押下を捨てる（§3.3）、要求の間に蓋が開いたら取り消し猶予の unlock は答えの後（§3.4）、送る予定の状態で session の要求の衝突を待つ（§3.2）、蓋は tick で level で判断し蓋以外の理由の起床で眠り直す（§3.1）、延びる間隔と恒久の失敗（§5）、networkd の SLEEPING の状態・冪等・上限・居ない時（§2）、app の SUSPEND は compositor を通す（§1.3）、外部の monitor の判定の手段が無いので clamshell は範囲外の案（N8）、無操作の tick は lock・greeter の return の前（§3.1）。non-blocking のうち、2 frame の規則・要求の間は frame を出さない・outcome の buffer・device の照合・AC と電池の切り替え・sessiond の子の持ち主と fd・案 A の別の形・答えが来ない時の回復・起こした鍵・opcode の番号・Phase の分割・QEMU の試験の mode・resume の失敗の通知も入れた。
- **第 2 版（2026-10-07）**: p006 に無い `KERN_SYSTEM_SLEEP_INFO` を前提にした §6 を書き直し（案 A・B）、今の main の事実（sessiond の `POWER` の session からの受け付け、通知、`--lock-idle`、sleep button、networkd の binary の opcode、kernel の事象）に合わせた。

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
| 蓋（`wayland/lid.c`・`backend-host.c` の `kwl_backend_lid_changed`、ws132-p008） | 閉じると lock して画面を消す（`KWL_LID_LOCK`・`KWL_LID_SCREEN_OFF`）、15 分以内に開けると password 無しで unlock（D2 の猶予） | sleep が使える時は「lock → 画面を消す → sleep を頼む」（tick で level で判断、§3.1）。外部の monitor の時は N8。開けた時の規則は今の猶予のまま（要求の間は §3.4） |
| 電源ボタン・sleep button（`kwl_backend_power_button`、ws132-p003） | log だけ（`KWL EVENT power button`・`sleep button`） | 短押しで lock して sleep（D1: dialog 無し）、sleep button も同じ（N5）。起こした押下は捨てる（§3.2）。長押しは firmware の強制断で触らない |
| 無操作 | `--lock-idle`（既定 10 分）で lock だけ（`shell.c` の tick、lock の間は数えない）。最後の入力は `server->lock_input_ms` | 新しい `idle.c` が lock の間も数え、設定の時間の半分で画面を消し、時間で lock して sleep。今の 10 分の lock は残す |
| 電源の操作（`kl_backend_power_action`、`power-zedbsd.c`） | `POWER poweroff|reboot` を greeter か session の descriptor で送る。`SUSPEND` は定義だけ（`power_actions` は poweroff・reboot の bit だけ）。session は root・wheel だけ | `POWER suspend` を足す（greeter と、session の利用者は誰でも、N2）。答えの詳しさ（起床の理由、拒んだ device）を backend が保ち、compositor が読む（§1.2） |
| sessiond（`power.c`・`power-rules.c`・`session.c`・`greeter.c`） | `POWER` は `sessiond_power_run` で `/sbin/poweroff` などを fork して直ぐ `OK` | 新しい `sleep.c`: 子 process で networkd の準備 → ioctl → networkd の終わり、結果を pipe で親へ。親は session と greeter の loop の poll に pipe を足し、答えを返す（§1.1） |
| networkd（`userland/base/networkd/main.c`、protocol は `userland/base/net/protocol.h` の opcode） | Wi-Fi を止める `stop_wlan_radios(radios, count, lower)` が在る（disconnect・search-stop・down、状態を確かめる）。sleep を知る口は無い | 新しい opcode `NETWORKD_OP_SLEEP_PREPARE`・`NETWORKD_OP_SLEEP_END`（root だけ、§2） |
| 通知（WS156 p002、`notify-shell.c` の `kwl_notify_post_system`） | compositor 自身の通知（client 0）を出せる。lock の画面と greeter の上には出ない（`kwl_glass_draw` は lock の間 `kwl_greeter_draw` だけ） | 中止の理由を lock・greeter の card の文の行（`greeter_message`）に出し、同じ文を system の通知にも残す（§4、N3） |
| Settings（`userland/desktop/settings/pages.c`） | 「Battery」の頁は `se_soon_draw`（未実装の印） | 「Power」の頁にして、sleep までの時間（電源・電池）を選ぶ（§8、N4） |

## 設計

### 0. 不変条件

1. **session では lock の画面が出てからしか眠らない**: 送る直前に `server->locked` を確かめる。`kwl_lock` が失敗（sessiond の管理でない session）なら眠らない（log `KWL SLEEP skip reason=not-locked`）。greeter は lock が無いので、そのまま。
2. **sleep の要求は同時に 1 つ**（compositor・backend・sessiond の 3 段とも）。
3. **要求から答えまで compositor は新しい frame を出さない**（`server->sleep_hold`）。i915 の park（`i915.c:359-372`）が進行中の present で EBUSY にならないため。答えの後は必ず再描画（`dirty`・`kwl_schedule`、i915 は次の present で出力を点ける、`i915.c:388-392`）。
4. **起きた時に利用者が最初に見るのは lock の画面（か蓋の黒）**: `PENDING` の間は毎 tick `server->dirty = 1` にし（lock の画面は分が変わる時しか自分で描かない、`greeter.c:579-584`）、lock（または `screen_off`）にしてから submit した frame（`server->frame` の増え）が 2 つになってから送る（今の greeter の電源と同じ規則、`greeter.c:1286-1288`・`607-609`）。蓋の時は黒の frame（`shell.c:539-547`）でよい。上限 `KWL_SLEEP_LOCK_MS`（500 ms）を過ぎても 2 frame が出なければ眠らない（失敗として §5）。
5. **契機は画面を持つ compositor だけ**: `handed_over` の前（login の後、greeter が画面に残る間）と LOGOUT の後（新しい greeter が先に起きる間）は契機を出さない（2 つの compositor が蓋・電源ボタンを受けるため、`sessiond.h:61-66`、`session.c` の `session_logout`）。
6. **zedBSD だけ**: 答えを返す backend（zedBSD）でだけ `sleep.c` を使う。Linux・FreeBSD は今のまま（Linux の app の SUSPEND は logind へ直に、`power-linux.c:17-18`）。

### 1. 経路（誰が何をするか）

```
Keiland（compositor、session か greeter）sleep.c
  契機（§3）→ lock（session）→ 2 frame → sleep_hold → kl_backend_power_action(SUSPEND)
    → "POWER suspend\n"（session か greeter の control の socket）
sessiond（root）sleep.c
  pipe（O_CLOEXEC）と子 process。子は control の socket・passkey の fd を閉じ（closefrom(3) の後に pipe の書き端だけ）、
  1. networkd に SLEEP_PREPARE（connect できなければ飛ばす、答えの上限は §2 の式）
  2. 取り消し（§3.4、2 本目の pipe の 1 byte）が来ていなければ ioctl(/dev/system, KERN_SYSTEM_SLEEP, S0IDLE) … S0i3 … wake
  3. PREPARE をしたなら networkd に SLEEP_END（答えを最大 2 秒待ち、来なければ 1 度送り直す）
  4. 結果の 1 行を pipe へ書いて終わる（END の後なので、答えを受けた利用者が直ぐ Wi-Fi を触っても SLEEPING でない）
  親（session と greeter の loop のどちらでも）: pipe の 1 行を読み、要求した socket に答える。子を waitpid で回収。
Keiland
  答え（§1.2）→ sleep_hold を解き再描画 → 起床の後の処理（§3.5）か中止の表示（§4）と §5
```

- **sessiond が仲介する理由**: `KERN_SYSTEM_SLEEP` は root だけ。compositor は電源の操作を sessiond に頼む今の形（ws131）に揃える。sessiond は自分が作った session の control の socket と greeter の socket からだけ受ける（今の `POWER` と同じ認証）。
- **子 process の理由**: networkd の答えを待つ間と眠っている間も、sessiond の loop（greeter の login、passkey の exchange、session の終わりの検出）を止めない。
- **子の持ち主**: 子の pid・pipe の読み端・要求した側（session か greeter）は `struct sessiond` に置き、session の loop と greeter の loop の両方の poll が pipe を見て waitpid で回収する（session の loop は compositor の終わりで抜け、greeter の loop は login で抜けるため。zombie と恒久の busy を残さない）。要求した socket が答えの前に閉じたら、答えは捨てて子だけ回収する。
- **networkd を同期で呼ぶ理由**: radio を切り終える前に kernel が device を suspend すると AX211 が `EBUSY`（`net_opened`、`intel-ax211.c:6578-6593`）で中止する。kernel の事象（`sleep.begin`）で知らせる形は順序を保証できない。
- **audiod は何もしない**: HDA は p005 で driver が stream を止めて保ち、resume で再開する（`pci-hda.c:485-518`）。docs/architecture/power-management.md の 52-54 行（口の無い device は止めて起きた後に再起動）と 91 行（audiod closes streams）は今の code（`pci-power.c:799-800` は口の無い driver で中止）と違う → Q1 へ（docs を直すか）。

#### 1.1 sessiond の答え（1 行、`session.c` と `greeter.c` の文書の一覧にも書く）

| 答え | 意味 | backend の outcome の error |
| --- | --- | --- |
| `SLEPT woke=<理由>` | 眠って起きた。理由は kernel の事象の名前（`power-button`・`lid`・`keyboard`・`usb`・`ac`・`timer`・`spurious`・`other`）と、wake の無い devices の mode（N10）の `none`。`resume_result` が 0 でなければ ` resume-error=<errno の名前>` が続く（kernel は resume の失敗の device を返さない、`sleep.c:191-197`） | 0（kind SLEPT） |
| `NOSLEEP unsupported` | ioctl が `EOPNOTSUPP`（platform が眠れない） | `EOPNOTSUPP`（kind UNSUPPORTED、恒久） |
| `NOSLEEP device error=<errno の名前> device=<device>` | 眠らずに戻った（`result` が 0 でない）。device は kernel の文字列（空白を含むので行の最後） | `result`（kind DEVICE。`EOPNOTSUPP` は恒久） |
| `NOSLEEP network reason=<radio|timeout|confirmed|busy> radio=<if> error=<errno の名前>` | networkd が radio を止められなかった・時間切れ・confirmed の transaction の最中・利用者の Wi-Fi の仕事の最中（ioctl はしない、END は送った） | `EBUSY`（kind NETWORK） |
| `NOSLEEP cancelled` | 取り消し（§3.4）で ioctl の前に止めた | `ECANCELED`（kind CANCELLED） |
| `ERROR busy` | 他の sleep が進行中（ioctl の `EBUSY`、sessiond が既に子を持つ）、または poweroff・reboot を走らせた後。今の慣行（`session-zedbsd.c:91-98`）に揃える | `EBUSY`（kind BUSY） |
| `ERROR` | pipe・fork の失敗、子の異常な終わり | `EIO`（kind ERROR） |

- `POWER cancel` には**何も答えない**（子が無い時も、答えの後も）。backend は `KL_BACKEND_SESSION_NONE` で送る（今の `CANCEL` と同じ型、`session-zedbsd.c:587-606`）。答えは要求の順で対応する（`session-zedbsd.c:740-765`）ので、答えの無い行でなければずれる。
- sessiond は poweroff・reboot を走らせた後の `POWER suspend` に `ERROR busy` を返す（OK は init が止める前に返るため、`sessiond/power.c:30-53`）。

- sessiond は `NOSLEEP unsupported` を boot の間覚え、次からは networkd に頼まずに直ぐ同じ答えを返す。`result` が `EOPNOTSUPP`（driver に口が無い）は覚えない（hot-plug の機器が抜ければ眠れるため。compositor の抑止は §5）。
- 子の終わりの待ちの上限は無い（眠っている時間は利用者が決める）。

#### 1.2 backend（`libkeiland-backend`）

- 新しい答えの語 `SLEPT`・`NOSLEEP` を `session_kind`（`session-zedbsd.c`）に足し、**SUSPEND の答えは login の答えの経路に流さない**: backend は SUSPEND の答えを `struct kl_backend_power_outcome`（kind、error、wake の理由、`device[KL_BACKEND_POWER_DEVICE_MAX]`（48、backend 自身の定数。header は OS に依らない、`keiland-backend.h:30-32`）、`radio[16]`、network の reason、resume の error）に解き、`session_answer(KL_BACKEND_SESSION_POWER, error)` を呼ぶ。`KL_BACKEND_SESSION_REASON`（24 byte）は使わない。
- compositor の `handoff.c` の `kwl_handoff_answer` は、POWER の答えで SUSPEND が出ている時（`kwl_sleep_waiting`）は greeter・lock の分岐（`handoff.c:194-203`）より**前に** `kwl_sleep_answer` へ回す（「Wrong password」などが出ないように）。
- `kl_backend_power_action(SUSPEND)` の `EBUSY` を分ける: 既に電源の要求が出ている時は `EALREADY`、他の session の要求（STYLES など）の答えを待つ時は `EBUSY`（§3.2 で送り直す）。poweroff・reboot は今のまま `EBUSY`（app への結果 `system_result_of` を変えない、`system.c:3054-3080`）。
- **SUSPEND の答えの時だけ** `power_asked` を 0 に戻す（poweroff・reboot の OK の後は今のまま残し、電源を切る途中に眠らない）。
- session・greeter の descriptor が閉じた（`session_gone`、`session-zedbsd.c:679-685`）時、SUSPEND が出ていれば `session_answer(POWER, EIO)`（kind ERROR）を呼び `power_asked` を戻す。`power_actions` は `session_gone` の時 0。
- 蓋の level: `struct kl_backend_power_state` に `lid`（-1 不明・0 閉・1 開、`KERN_SYSTEM_GET_POWER` の `lid_open`）を足す。compositor は起動時・事象の OVERFLOW の後・SUSPEND の答えの後に読み、`server->lid` を合わせる（§3.1）。
- `power_actions` の SUSPEND の bit: greeter の descriptor か session の descriptor があり、sleep が使える時（§6）。session の利用者が wheel でなくてもよい（N2）。
- 新しい関数 `kl_backend_power_outcome(backend, &outcome)`。Linux（`power-linux.c`）と unsupported（`libkeiland-backend/unsupported/power-unsupported.c`、FreeBSD もこれ、`Makefile.freebsd:11`）は ENOTSUP を返す（link のため）。

#### 1.3 app からの SUSPEND（`system.c`）

- zedBSD では、system extension の `KL_SYSTEM_POWER_ACTION`（`system.c:1319-1335`）の SUSPEND は `kl_backend_power_action` を直に呼ばず、compositor の `kwl_sleep_request(server, KWL_SLEEP_VIA_APP)` を通す（lock → 2 frame → 送信、§0）。結果は `system_result` で直ぐ「受け付けた」か「使えない」。poweroff・reboot は今のまま。Linux・FreeBSD は今のまま（§0.6）。

### 2. networkd の SLEEP_PREPARE・SLEEP_END

- 新しい opcode（`userland/base/net/protocol.h`）: `NETWORKD_OP_SLEEP_PREPARE = 80`・`NETWORKD_OP_SLEEP_END = 81`（今の番号は 1〜14・16〜19（CONFIRMED）・32〜41（WIFI・SCAN）・48〜50（LAN）・64（SUBSCRIBE、`protocol.h:149`、`main.c:3449`・`5588-5591` が特別に扱う）。80・81 はどの範囲の振り分け（`dispatch_request`、`main.c:5389-5420`）にも入らない）。`operation_name` の表に足す。`operation_allowed` の member の一覧には入れない（root だけ = sessiond）。
- **進行中の Wi-Fi の仕事との関係**（`wifi_work.deadline != 0` の間は入れ子の経路で受ける、`main.c:5389-5466`）:
  - PREPARE は**自動の仕事**（AUTO_SEARCHING・RECONNECTING の自動接続・scan）を中止させる: `interrupts_background_work`（`main.c:5479-5513`）に PREPARE を足し、`wifi_work.cancelled` と今の cleanup（最大 `NETWORKD_WIFI_CLEANUP_SECONDS` 10 秒、`main.c:5440-5452`）で終わらせてから進む。
  - **利用者が始めた仕事**（`WIFI_ENABLE`・`CONNECT`・`DISABLE` などの最中）の間の PREPARE は `EBUSY`（`NOSLEEP network reason=busy`）。confirmed の transaction の最中も `EBUSY`（`reason=confirmed`）。
  - **END はどの状態でも断らない**: 仕事の最中なら待たせ（`wifi_pending` と同じ入れ物の 2 つ目の枠ではなく、SLEEPING を抜ける印を立てるだけにして、仕事の終わりで再開する）、直ぐ OK を答える。
- **予算の式**: PREPARE の networkd の中の上限 = `NETWORKD_WIFI_CLEANUP_SECONDS`（自動の仕事の中止）+ radio ごとの `stop_wlan_radios`（`wifi` の子 3 回、各 10 秒まで、`main.c:6607-6619`）。radio が 1 つなら最大 40 秒。sessiond の子の待ちの上限は `NETWORKD_SLEEP_PREPARE_SECONDS`（protocol.h に置く、40）+ 5 秒。時間切れなら END を送ってから `NOSLEEP network reason=timeout`。
- **SLEEPING の状態**: PREPARE は今の `managed_wlan` の state・所有者・自動の方針を記録し、`retire_managed_connection` → radio の列挙 → `stop_wlan_radios(…, 1)` で退かせる（`wifi_request_stop` と同じ順。方針（`networkd_managed_wlan_disable`）は書き換えず保存もしない）。SLEEPING の間は自動の仕事・retirement の再試行（`wifi_disable_defer` の timer）・requested scan を止め、利用者の `WIFI_*` の変更と confirmed の開始は `EBUSY` で断る（SHOW・LIST、`WIFI_SESSION_OPEN`・`CLOSE` は受ける。CLOSE を断ると log out した人の network が候補に残るため、`sessiond/session.c:692-731`）。
- **END で戻す方針**（記録した state ごと）:

| 記録した state | END の後 |
| --- | --- |
| DISABLED | 何もしない（radio は down のまま） |
| AUTO_SEARCHING・CONNECTING・CONNECTED・RECONNECTING | radio を up して AUTO_SEARCHING から再開（保存した network へ自動接続） |
| MANUAL_DISCONNECTED | radio を up し、自分からは接続しない |
| RETIRING | 再試行の timer を止めた上で、RETIRING の前の state（retire の時に記録した値）として上の行に従う。分からなければ AUTO_SEARCHING |

- **冪等**: SLEEPING の間の PREPARE は最初の記録を保ち、下の 10 分の時計だけを更新して OK。SLEEPING でない時の END は何もせず OK。
- **安全**: END が来ないまま 10 分経ったら自分で END と同じことをする（sessiond の子が死んだ等）。
- **networkd が居ない**（connect が `ENOENT`・`ECONNREFUSED`）: PREPARE と END を飛ばして ioctl へ進む（Wi-Fi が on なら driver の EBUSY が守る）。
- 有線は触らない（suspend の口の無い有線の driver は kernel が `EOPNOTSUPP` で中止し、§4 の言葉になる）。

### 3. 契機（compositor の `sleep.c`）

| 契機 | 条件 | 動作 |
| --- | --- | --- |
| 蓋が閉じている | session（greeter は N9）、sleep が使える（§6）、要求が出ていない、§5 の抑止の時間を過ぎた | lock（`KWL_LID_LOCK`、まだなら）→ 画面を消す → 要求（§3.2）。**tick で level で判断する**（§3.1） |
| 電源ボタン・sleep button の短押し | session でも greeter でも、sleep が使える、要求が出ていない・予定が無い、§3.3 の窓の外 | session なら lock → 要求。greeter なら要求だけ |
| 無操作 | 最後の入力（`lock_input_ms`）から設定の時間（電源と電池で別、§7.1 の 2）。0 は「しない」。lock の間も数える。全画面の窓が一番上の間は数えない（N7） | 時間の半分で画面を消す（入力で点く、lock しない）。時間で lock（まだなら）→ 要求 |
| app の SUSPEND | §1.3 | 電源ボタンと同じ |

- **sleep が使えない時**（§6）: 蓋は今の「lock して画面を消す」、電源ボタンは log だけ（今のまま）、無操作は設定の時間の半分で画面を消すだけ（今の 10 分の lock は残る）。§8 の Settings の文も同じ規則で書く。

#### 3.1 tick と level の判断

- `kwl_sleep_tick(server)` を `kwl_glass_tick` の**先頭**（greeter・lock の return（`shell.c:2709-2723`）より前）で呼ぶ。無操作の時計、蓋の level、送る予定、§5 の抑止はここで評価する。
- 蓋: `server->lid.closed` が真で、上の条件を満たせば要求する（閉じた時の事象だけに頼らない）。`server->lid` は起動時・事象の OVERFLOW の後・SUSPEND の答えの後に backend の `lid` で合わせる（§1.2。今は事象でしか変わらず、起動時は開とみなしている、`lid.c:26-37`・`events-zedbsd.c:212-214`）。**蓋が閉じたままなら `lid` 以外のどの理由で起きても**（`keyboard` は EC の熱・fan の事象でもなる、`sleep.c:471-481`）次の tick で眠り直す。ただし §3.5 の速い起床の抑止に従う。
- 電源の切り替え（AC ↔ 電池）は無操作の時計を今に戻す（20 分の無操作で AC を抜いた瞬間に眠らない）が、**入力ではない**（§3.5 の 60 秒の眠り直しは消さない）。source が UNKNOWN（AC を知らない機械・QEMU）の時は AC の時間。
- seat が pause している間（`os_paused`、`display.c:117-122`）は tick が来ないので、無操作の時計も止まる（今の zedBSD で pause が起こるかは未確認）。

#### 3.2 送る予定（`KWL_SLEEP_PENDING`）

- 状態: `IDLE` → `PENDING`（lock し、2 frame か上限を待つ）→ `WAITING`（送った、`sleep_hold`）→ 答えで `IDLE`。
- `PENDING` で `kl_backend_power_action` が `EBUSY`（lock の直後の STYLES（`greeter.c:327`・`1484-1512`）や password の送信の答え待ち）なら、次の tick で送り直す（`greeter_styles_ask` と同じ型）。`EALREADY`（既に出ている）は送らず `WAITING` に移る（答えはその要求の物を受ける）。上限 `KWL_SLEEP_SEND_MS`（3 秒）を過ぎたら失敗（§5、通知は出さない）。

#### 3.3 押下を捨てる窓

- `PENDING`・`WAITING` の間の電源ボタン・sleep button の押下は捨てる（log `KWL SLEEP ignore button`）。起こした押下は thaw の直後（答えより前）に PRESS として届く（`acpi-kern.c:819-820`、`sleep.c:197-200`）ので、この規則で捨てられる。
- 答えを受けてから `KWL_SLEEP_BUTTON_QUIET_MS`（1 秒）の間の押下も捨てる（socket と事象の fd の処理の順に依らないため）。
- `PENDING`・`WAITING` の間と、起きた直後 `KWL_SLEEP_KEY_QUIET_MS`（500 ms）の間の鍵は lock の画面に渡さない（frame が出ないので見えないまま password の欄に入る、起こした打鍵で空の送信になる、`greeter_submit_pending` が起きた後に自動で送る（`greeter.c:1197-1232`）のを防ぐ）。

#### 3.4 要求の間に蓋が開いた

- `PENDING` の間に蓋が開いたら要求を取りやめる（送らない）。
- `WAITING` の間に蓋が開いたら `POWER cancel` を送る（`KL_BACKEND_SESSION_NONE`、答えは無い、§1.1）。sessiond は子が居れば 2 本目の pipe（親 → 子、`O_CLOEXEC`）に 1 byte 書く。子は ioctl の直前にその pipe を nonblocking で読み、byte があれば ioctl をせず END を送り `NOSLEEP cancelled`。ioctl に入った後なら間に合わない（眠り、蓋の open は wake の源なので直ぐ起きる）。蓋の open で起きた時もその事象は thaw の後・答えの前に届くので cancel が出るが、答えが無いので害は無い。
- 猶予の unlock（`lid.c` の `KWL_LID_UNLOCK`）は `WAITING` の間は行わず、答えの後に蓋の今の状態で `kwl_lid_open` の判断をやり直す（unlock のまま眠ることが無い）。

#### 3.5 起きた後

- `sleep_hold` を解き再描画。画面を点ける（蓋が閉じていれば点けない。無操作で消した画面を入力で点ける時も同じ）。最後の入力の時刻を今にする。
- 蓋が開いていて利用者の起床（`power-button`・`lid`・`keyboard`・`usb`）でなければ（`ac`・`timer`・`spurious`・`other`・`none`）、無操作の時間の代わりに `KWL_SLEEP_REST_MS`（60 秒）の無入力で眠り直す（devices の mode（N10）の `none` では眠り直さない）。
- **速い起床の抑止**: 眠ってから `KWL_SLEEP_SHORT_MS`（10 秒）以内に起きた事が 3 回続いたら、§5 の延びる間隔を掛ける（詰まった PME や EC の嵐で、成功と起床を繰り返し Wi-Fi が up と down を繰り返さない）。
- `resume-error` があれば通知（§4）。

### 4. 中止の理由の表示

- 言葉（翻訳の対象、`wayland.keys`・`ja/wayland.tr`）:

| 原因 | 言葉（英語の基） |
| --- | --- |
| `NOSLEEP network reason=radio|timeout`、または device が `pci ` で始まり driver の語が `intel-ax211` で EBUSY | "Sleep was cancelled: Wi-Fi could not be turned off." |
| device が `pci ` で始まり driver が `nvme` で EBUSY | "Sleep was cancelled: the disk was busy." |
| 同じく `xhci` で EBUSY | "Sleep was cancelled: a USB device was busy." |
| 同じく `i915` で EBUSY | "Sleep was cancelled: the display was busy." |
| 同じく、上以外の driver で EBUSY | "Sleep was cancelled: DRIVER was busy." |
| `result` が EOPNOTSUPP（suspend の口の無い driver、`pci-power.c:799-800`） | "Sleep was cancelled: DRIVER cannot sleep yet." |
| `NOSLEEP unsupported` | "This computer cannot sleep." |
| device が `pci ` で始まらない（`lps0`・`acpi events`・`interrupts`・空） | "Sleep was cancelled (DEVICE, error E)."（空なら "Sleep was cancelled (error E)."） |
| `resume-error`（眠って起きたが device が戻らない。kernel は device を返さない） | "A device did not come back after sleep (error E)." |
| `NOSLEEP network reason=confirmed` | "Sleep was cancelled: a network change is waiting to be confirmed." |
| `NOSLEEP network reason=busy` | "Sleep was cancelled: Wi-Fi was busy." |
| `ERROR busy`・`NOSLEEP cancelled` | 何も出さない |

- DRIVER は device の文字列の最後の語（`pci 0000:00:14.3 intel-ax211` → `intel-ax211`）。
- 出し方（N3）: lock の画面か greeter が出ていればその card の文の行（`greeter_message`、128 byte、新しい `kwl_greeter_say`、UTF-8 の文字の境で切る）と、system の通知（`kwl_notify_post_system`、title "Sleep"）。同じ理由の通知は compositor の process の間 1 度だけ（文の行は毎回）。

### 5. 再試行と抑止

- 失敗の後の抑止は延びる間隔: 30 秒 → 2 分 → 10 分（上限）。成功・新しい入力・蓋の開閉で 30 秒に戻す。
- 無操作の失敗は、新しい入力が来るまで試し直さない。蓋は抑止の時間の後に tick が試し直す。電源ボタン・app は押すたびに試す。
- **恒久の失敗**: `NOSLEEP unsupported` は compositor の process の間「使えない」とみなす（§3 の使えない時の動き）。backend は SUSPEND の bit を落として `power_changed` を出し、Settings の文も合う。`result` が `EOPNOTSUPP`（driver に口が無い）は 10 分の抑止（上限の間隔）にし、恒久にはしない（hot-plug の機器が抜ければ眠れる）。sessiond の覚え方は §1.1。
- 蓋を閉じたまま失敗が続く時は、今の「lock して画面を消した」状態に留まる。

### 6. sleep が使えるかの判断（N1）

p006 には「眠れるか」を問う口が無く、ioctl が支えの無い platform で `EOPNOTSUPP` を返すだけ。

- **案 A1（勧める、UAPI の追加で承認が要る）**: `struct system_power_info` の `reserved[0]` を `flags` にし、`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP 0x1U`（`known` の `KERN_SYSTEM_POWER_HAS_LID 0x1U` と紛れない名前）を kernel が `kern_sleep_supported()` の時に立てる（`KERN_SYSTEM_GET_POWER`、構造体の大きさと配置は変えない。古い kernel は 0 = 使えない、安全側）。`known`（どの部分の値が分かるか、`system.h:351-353`）に能力を混ぜない。
- **案 A2（同じく承認が要る）**: `known` に `KERN_SYSTEM_POWER_HAS_SLEEP 0x8U`。変更は最小だが `known` の意味がずれる。
- **案 B（UAPI を変えない）**: 最初は「使える」とみなし、最初の `NOSLEEP unsupported` で backend と sessiond が覚える。欠点: 支えの無い機械（QEMU を含む）で compositor の process ごと（login・logout のたび）に、最初の電源ボタンで lock と通知が 1 度出る。Settings は最初の試みまで「cannot sleep」を出せない。ws132 の QEMU の試験（電源ボタンが log だけ）が変わる。
- A の時: backend の `power_read` が flags を読み、`power_actions` の SUSPEND の bit に載せる。Settings は `kl_system_power_get_state` の actions で分かる。docs/architecture/power-management.md の UAPI の節も直す（Q1）。

### 7. 人間の判断

#### 7.1 決定済み（2026-10-05 朝、ユーザーが案のとおり、Q1 の中継）

1. 外部の monitor に出している時に蓋を閉じた: **sleep しない**（内蔵の panel だけ消し、外部に出し続ける）。**AC の有無に依らない**。→ 第 3 版の N8 で実現の範囲を確かめる。
2. 無操作の時間の既定値: **AC 30 分・電池 15 分**、画面はその**半分**で消す。0 は「しない」。Settings の Power の頁で変える。
3. greeter で電源ボタン: **sleep**（lock は無い）。
4. sessiond が **session からも suspend を受ける**（ws131 の D12 の改訂。poweroff・reboot は今の規則のまま）。

#### 7.2 新しい判断（第 3 版、既定の案）

- **N1**（§6）: sleep が使えるかの判断。**案 A1**（`system_power_info` の `reserved[0]` を `flags`、`KERN_SYSTEM_POWER_FLAG_CAN_SLEEP`）を勧める。A2・B は §6。
- **N2**: suspend を頼めるのは greeter と **session の利用者なら誰でも**（wheel でなくてよい）。poweroff・reboot は今のまま root・wheel。
- **N3**: 中止の理由は lock の画面・greeter の card の文の行と system の通知（第 1 版の「画面の下の 5 秒の toast」の代わりに在る部品）。
- **N4**: Settings の「Battery」の頁（未実装の印）を「Power」にし、sleep までの時間を電源・電池で選ぶ（Never・5・10・15・30・60・120 分、既定 30・15）。画面を消すのはその半分で選ばない。電池の残りの表示は後。
- **N5**: sleep button も電源ボタンの短押しと同じ。
- **N6**: 今の無操作の lock（`--lock-idle`、10 分）は残す（電池の既定だと画面を消す 7.5 分の方が早いが、画面を消すのは lock しない）。
- **N7**: 無操作の抑止は全画面の窓だけ（idle-inhibit の protocol と音の再生中は backlog）。ssh・Sharing の遠隔の利用中も局所の入力しか数えないので眠る（範囲外と明記）。
- **N8**（review の B9）: 今の compositor は最初の 1 つの display だけを使い（`compose.c:803-818`）、外部の monitor に出していると知る口も「内蔵だけ消す」口も無い。**案: p007 では蓋を閉じると常に眠る**（7.1 の 1 の clamshell は複数の出力を扱う WS の後に）。別案: Vulkan の display の数が 2 以上なら蓋で眠らず今の「lock して全体を黒」に留める（数は起動時の値、hot-plug は追わない）。
- **N9**: greeter で蓋を閉じた時・greeter の無操作でも眠る（無操作の時間は既定の AC 30・電池 15 分。利用者の設定は無い）。
- **N10**: QEMU で userland の流れ（PREPARE → ioctl → END → 答え → 起きた後の lock の画面）を通す試験の口を置くか。QEMU では N1 の A1 で `CAN_SLEEP` が 0 なので compositor は送らない。通すには 2 つが要る: sessiond の `--sleep-mode=devices`（ioctl を `KERN_SYSTEM_SLEEP_DEVICES` にし、答えは `SLEPT woke=none`）と、compositor の `--sleep-test`（使えるとみなす）。どちらも試験の config だけで渡す command line の option（規約の「試験だけの環境変数の switch」ではないが、「試験は本番の既定の道を通す」（`coding-style.md:788-799`）からは外れる）。置かなければ QEMU は電源ボタンが log だけ・無操作で画面が消えるまでで、残りは host 試験と 5330 の UAT。案: 置かない（host 試験を厚くする）。

#### 7.3 第 1 版の判断の案の記録（決定の前、2026-10-05）

1. 外部の monitor に出している時に蓋を閉じた: 案は sleep しない。AC の有無で変えるか。
2. 無操作の時間の既定値: 案は AC で 30 分、電池で 15 分（画面を消すのはその半分）。0 で「しない」。
3. greeter で電源ボタン: 案は sleep。Shut Down の確認を出す案（ws132 の D1 の元の案）もある。
4. sessiond が session から suspend を受けること。

### 8. Settings の Power の頁（N4）

- 鍵（`settings-keys.c`、resolver compositor、KEPT）: `power.sleep.ac`（INT、0〜240 分、既定 30）、`power.sleep.battery`（INT、0〜240、既定 15）。0 は Never。compositor の `settings.c` が読み `sleep.c` に渡す（`KWL PREFERENCES key=power.sleep.ac applied value=N`）。
- 頁（`settings/pages.c` の `SE_PAGE_BATTERY` を title "Power"・説明 "Sleep and the screen."、新しい `page-power.c`）: 2 つの選択（"Sleep after, on the power adapter"・"Sleep after, on battery"）と文 "The screen turns off after half that time."。sleep が使えない時（§6 の A、actions に SUSPEND が無い）は文 "This computer cannot sleep. The screen turns off after half that time." と、選択の見出しを "Turn the screen off after twice…" にはせず同じ鍵のまま（§3 の使えない時の動きと同じ: 時間の半分で画面を消す）。
- 翻訳: settings の keys・ja。

### 9. 変える file

| 部分 | file |
| --- | --- |
| （N1 が A の時）kernel・UAPI | `include/uapi/system.h`、`src/drivers/generic/system-device.c`（GET_POWER）、docs の UAPI の節（Q1） |
| networkd | `userland/base/net/protocol.h`、`userland/base/networkd/main.c` |
| sessiond | 新しい `sleep.c`（子・pipe・取り消し・覚える）、`sleep-rules.c`（答えの行を作る純粋な関数）、`sessiond.h`（`struct sessiond` に子）、`session.c`・`greeter.c`（`POWER suspend`・`POWER cancel` と poll の pipe、文書の一覧）、`power-rules.c`（suspend は wheel を問わない） |
| backend | `keiland-backend.h`（outcome・kind・`lid`）、`power-zedbsd.c`（SUSPEND の bit・送信・EALREADY・lid の読み・A1 の flags）、`session-zedbsd.c`（`SLEPT`・`NOSLEEP` の解釈、`POWER cancel` の送信、session_gone の答え）、`power-linux.c`・`unsupported/power-unsupported.c`（outcome の関数） |
| compositor | 新しい `sleep.c`・`sleep.h`（状態・契機・抑止・窓・言葉・速い起床、server を知らない純粋な部分は host 試験）、`lid.c`（level を合わせる口）、`shell.c`（`kwl_glass_tick` の先頭に 1 行、frame を出さない判断に 1 行）、`backend-host.c`（電源ボタン・蓋から呼ぶ）、`handoff.c`（POWER の答えを先に）、`system.c`（app の SUSPEND）、`greeter.c`（`kwl_greeter_say`、鍵の窓）、`settings.c`（2 つの鍵）、`kwl.h`（`sleep_hold`）、locale |
| Settings | `settings/pages.c`、新しい `page-power.c`、`settings-keys/settings-keys.c` |

- WS181 で変えた `shell.c`・`home.c`・`arrange-shell.c` は最小限（`shell.c` は 2 行）。
- KL_VERSION: libkeiland の公開の API は変えない（上げない）。

### 10. 実装の Phase の分け方（案、Q1 が WS052 に置く）

| Phase | 内容 | 依存 |
| --- | --- | --- |
| p010 | networkd の SLEEP_PREPARE・END（§2）と host 試験 | p007 の ACK |
| p011 | sessiond の sleep.c と backend（§1.1・§1.2）、N1 が A なら kernel の bit | p010、N1 の決定（A なら UAPI の承認）、p006 の UAPI が main に在ること（在る） |
| p012 | compositor の sleep.c（§0・§3〜§5・§1.3）と翻訳 | p011 |
| p013 | Settings の Power の頁（§8） | p012 |

- 実機の確認（ws052-p008）は p006 の 5330 の UAT の後。

## 依存

- p006: `KERN_SYSTEM_SLEEP` の S0 idle の mode と事象（main に在る、QEMU は T1-178 で拒否まで、5330 の UAT 待ち）。N1 が A なら UAPI の承認。
- WS132 p008（蓋の lock）、WS089（Settings）、WS156 p002（通知、在る）、WS131（sessiond、`POWER` の規則）、WS005 の networkd。

## 確かめ方（code の後）

- host の試験: sessiond の子の lifecycle（子が動いている間に session の loop が終わる、要求した socket が先に閉じる、greeter の loop が回収する、cancel の byte）、networkd の END の取り落としと時間切れと自動の仕事の中止、起動時の蓋の level、compositor の `sleep.c`（状態の遷移、蓋の level、押下の窓（PRESS → 答えの順と答え → PRESS の順の両方）、要求の間の蓋の open と取り消し、送る予定の EBUSY の送り直し、延びる間隔、恒久の失敗、AC と電池の切り替え、利用者でない起床の 60 秒、言葉の選び方）、sessiond の `sleep-rules.c`（result・wake・device・errno から行）と backend の解釈（行から outcome）、networkd の SLEEPING の状態（冪等の PREPARE、END だけ、10 分の安全、confirmed の間の拒否）。
- QEMU（T1）: N1 が A なら電源ボタン（`system_powerdown`）が log だけ、無操作で半分の時間に画面が消える（試験の短い時間の option で）。N10 を置くなら devices の mode で `POWER suspend` → `SLEPT woke=none` と networkd の PREPARE・END の往復、起きた後の lock の画面。
- 実機（5330、ユーザーの UAT）: 3 つの契機で入り、蓋・電源ボタンで戻る。Wi-Fi の接続が戻る（CONNECTED から）。中止の理由（Wi-Fi を切れなくする等で起こす）。15 分より長く眠った後に蓋を開けると password を求める（猶予は CLOCK_MONOTONIC が sleep の間も進むことに依る、p006 の時刻の補正）。

## design-reviewer の 1 回目（2026-10-07、第 2 版に対して、第 3 版で反映）

blocking 10（第 3 版で直す）:
- B1 sleep の答えが lock・greeter の「login の答え」に流れる（`handoff.c:194-203` → `greeter_answered` が EACCES を「Wrong password」に）→ SUSPEND の答えは handoff で先に sleep.c へ、`session_answered` で専用に解く、busy は `ERROR busy` に揃える。
- B2 起こした押下は thaw の直後（OK より前）に届く → kernel の事象 `sleep.begin`・`end`・`failed` を backend から host へ渡し、begin から end の後の数百 ms と SUSPEND が出ている間の押下を捨てる、予約の送信を取り消す。
- B3 PREPARE の最中に蓋を開けると猶予で unlock され unlock のまま眠る → SUSPEND の間は猶予の unlock を遅らせる（か `POWER cancel`）、「session では locked でなければ送らない」を不変条件に、`kwl_lock` の失敗では眠らない。
- B4 lock 直後の STYLES と衝突して `session_request` の EBUSY → 「送る予定」の状態を持ち後の tick で送り直す。
- B5 蓋が閉じたまま蓋以外の理由（AC・USB・spurious）で起きると眠り直さない → 蓋は level で tick で判断、蓋以外の理由の起床で蓋が閉じていれば直ぐ眠り直す。
- B6 失敗が続くと 30 秒ごとに Wi-Fi が落ち通知が積もる → 延びる間隔、無操作は新しい入力まで再試行しない、`result=EOPNOTSUPP` は恒久（sessiond も覚えて PREPARE を飛ばす）、同じ理由の通知は 1 度。
- B7 networkd の PREPARE・END の上限と状態の戻し → SLEEPING の状態（`retire_managed_connection` で退かせ END で前の方針から再開）、冪等、上限を networkd の予算に合わせ、時間切れでは END を送ってから失敗、networkd が居なければ飛ばす、進行中の仕事は中止させる。
- B8 system extension の `KL_SYSTEM_POWER_ACTION`（`system.c:1319-1335`）で app が lock なしで眠らせられる → compositor の sleep.c を通す（か受けない）。
- B9 外部の monitor を判定する手段が無い（compose.c は最初の 1 display だけ）→ 情報源を書くか、「内蔵だけ消す」は単一出力の今では不可として範囲を Q1・ユーザーに確かめる。
- B10 無操作の tick は `kwl_glass_tick` の greeter・lock の return より前に置く、greeter の無操作で眠るかも決める。

non-blocking 19（主な物）: 2 frame の規則に揃える、要求から答えまで新しい frame を出さない・OK の後は必ず再描画、outcome の buffer（REASON 24 byte に device は入らない）、device の照合は `pci ` で始まる時だけ、§3 と §8 の使えない時の画面を消す時間の矛盾、greeter で蓋・無操作、AC と電池の切り替えで時計を戻す、sessiond の子の持ち主は `struct sessiond` に・pipe は O_CLOEXEC・closefrom、案 A は `known` でなく `reserved[0]` を flags にする案も、案 B の欠点の補い、§9 の抜け（linux・unsupported の backend、handoff.c・system.c・events-zedbsd.c）、答えが来ない時の `power_asked` の回復、docs と code の食い違い（power-management.md:52-54・91）、起こした鍵が password の欄に入る、networkd の opcode の番号と confirmed transaction、範囲外（遠隔の利用中も眠る）、Phase の分割、QEMU で `KERN_SYSTEM_SLEEP_DEVICES` を使う試験の mode、resume の失敗の通知。

## design-reviewer の 2 回目（2026-10-07、第 3 版に対して、第 4 版で反映）

- 判定: B2・B4・B8（zedBSD）・B9・B10 は解消、B1・B3 は大筋で解消（cancel が穴）、B5・B7 は残り。
- blocking 9: NB1 opcode 64 は SUBSCRIBE（`protocol.h:149`）、NB2 lock の画面は分の変わりでしか描かないので 2 frame が出ない（`greeter.c:579-584`）、NB3 `POWER cancel` の答えが順の対応をずらす（`session-zedbsd.c:740-765`）、NB4 眠り直す理由に keyboard が無い・蓋が level で読まれていない（`lid.c:26-37`）、NB5 進行中の Wi-Fi の仕事と PREPARE・END（`main.c:5389-5513`）、NB6 END で戻す方針が 7 つの state の 2 つだけ（`managed-wlan.h:26-34`）、NB7 成功で `power_asked` を戻すと電源を切る途中に眠れる、NB8 WAITING から抜ける道が答えだけ（`session-zedbsd.c:679-685`）、NB9 N10 の材料が A1 と両立しない・`none` が無い。
- non-blocking 19（N1〜N19）: 第 4 版の「版の変更」の一覧のとおり取り込んだ。残り: N19 の `os_paused` が zedBSD で起こるか（未確認、§3.1 に注記）、i915 の resume が backlight を戻すか（未確認）。
