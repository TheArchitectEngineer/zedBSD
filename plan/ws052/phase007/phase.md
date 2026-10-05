<!-- awesome-plan project=zedbsd record=ws052p007 -->

# ws052-p007: Keiland の 3 つの契機と、中止の理由の表示（設計）

Phase ID: `ws052-p007`
Parent: [WS052](../ws.md)
Status: planning（2026-10-05 P1 generation17。設計の第 1 版、§7 はユーザーが案のとおり決定（2026-10-05 朝）。code は p006 の口（`KERN_SYSTEM_SLEEP` の S0i3 の mode と事象）が決まってから）
Phase disposition: normal
Queue: Q1 の 2026-10-05 の指示（p006 が H1〜H4 の承認待ちの間に、承認に依らない p007 の準備）

## 範囲

- Keiland（compositor）が S0i3 に入る 3 つの契機: **蓋を閉じた時**、**電源ボタンの短押し**、**一定時間の無操作**（§10 の決定、2026-10-05 ユーザー）。
- 入る前の準備と戻った後: session の lock、**networkd が sleep の前に Wi-Fi の radio を切り、戻ったら元に戻す流れ**（p005 の AX211 は radio が on だと
  中止するため）。
- **中止の理由の表示**: kernel が返す原因の device と error を、利用者の言葉にして見せる。
- 範囲外: kernel の S0i3 の入口・出口（p006）、通知の仕組みそのもの（WS156、未着手）、HDA・Wi-Fi の本当の suspend（後）。

## 今の形（2026-10-05 の main を読んだ）

| 部分 | 今 | p007 で変えること |
| --- | --- | --- |
| 蓋（`wayland/lid.c`・`backend-host.c` `zwl_backend_lid_changed`、ws132-p008） | 閉じると画面を消して lock（`ZWL_LID_SCREEN_OFF`・`ZWL_LID_LOCK`）、15 分以内に開けると password 無しで unlock（D2 の猶予） | sleep ができる時は「lock して sleep」。戻った時（開けた時）の unlock の規則は今の猶予のまま |
| 電源ボタン（`zwl_backend_power_button`、ws132-p003） | log だけ（`ZWL EVENT power button`） | 短押しで lock して sleep（D1 の訂正: dialog 無し）。長押しは firmware の強制断で触らない |
| 無操作の時間 | 無い（Settings に Power の頁も無い） | compositor が最後の入力からの時間を数え、設定の時間で lock して sleep。Settings に Power の頁（WS089 の頁の追加） |
| 電源の操作の経路（`kl_backend_power_action`、`power-zedbsd.c`） | `KL_BACKEND_POWER_SUSPEND` の定義はあるが、sessiond は greeter の descriptor からの poweroff・reboot だけを受ける（ws131 の D12） | sessiond が **session と greeter の両方から suspend** を受ける（poweroff・reboot は D12 のまま） |
| networkd | Wi-Fi の radio を止める `stop_wlan_radios` がある。sleep を知る口は無い | sessiond からの「sleep の準備」「sleep の終わり」の要求（networkd の socket の新しい要求） |
| 通知 | app の通知は無い（WS156 が planning） | 中止の理由は WS156 ができるまで compositor の簡単な toast（下の §4）で出す |

## 設計

### 1. 経路（誰が何をするか）

```
Keiland（compositor、利用者）
  契機（蓋・電源ボタン・無操作）→ lock → kl_backend_power_action(SUSPEND)
        │ sessiond の socket（POWER suspend）
sessiond（root）
  1. networkd に SLEEP-PREPARE（同期: radio を切り終えるまで待つ、上限 5 秒）
  2. ioctl(/dev/system, KERN_SYSTEM_SLEEP, mode = S0 idle)（p006）
     … S0i3 … wake（蓋・電源ボタン・キーボード・USB・AC）
     → result（0 / 中止の error）・device（中止の原因）・wake の理由
  3. networkd に SLEEP-END（切った radio を戻す。待たない）
  4. Keiland に session_answer(POWER, result, device, wake reason)
Keiland
  result が 0: 何もしない（lock の画面のまま。蓋なら猶予の規則）
  中止: lock の画面の上に toast で理由（§4）、30 秒は同じ契機で再び試さない（§5）
```

- **sessiond が仲介する理由**: `KERN_SYSTEM_SLEEP` は root だけ（p004 の devices の mode と同じ）にし、利用者の compositor は電源の操作を sessiond に
  頼む今の形（ws131）に揃える。sessiond は session の持ち主の要求だけを受ける（今の power の要求と同じ認証）。
- **networkd を同期で呼ぶ理由**: radio を切り終える前に kernel が device を suspend すると AX211 が EBUSY で中止になる。kernel の事象
  （`power.sleep.begin`）で networkd に知らせる形は順序を保証できないので、sessiond が先に networkd の終わりを待つ。kernel の事象は表示や記録の
  ためだけに使う。
- **audiod は何もしない**: HDA は p005 で driver が stream を止めて再開する。

### 2. networkd の SLEEP-PREPARE・SLEEP-END

- 新しい要求（networkd の socket、root と sessiond だけに許す: 今の `operation_allowed` の root-all の役割）:
  - `SLEEP-PREPARE`: 有効な Wi-Fi の radio を記録して止める（`stop_wlan_radios`。接続中なら切断してから）。有線は触らない（driver に suspend がある
    xHCI の usb-net、NVMe 以外の有線 NIC が増えたら別に考える）。答えは `OK radios=N` か `FAILED radio=IF error=E`。
  - `SLEEP-END`: SLEEP-PREPARE で止めた radio を元に戻す（自動接続の方針で再接続）。止めていなければ何もしない。
- sleep の間に networkd が自動の再接続を始めないよう、SLEEP-PREPARE から SLEEP-END まで自動の仕事（`run_automatic_work`）を止める。
- SLEEP-END が来ないまま sessiond が死んだ時のため、SLEEP-PREPARE から 10 分で自分で SLEEP-END と同じことをする（安全側）。

### 3. 3 つの契機（compositor）

| 契機 | 条件 | 動作 |
| --- | --- | --- |
| 蓋を閉じた | session（greeter でない）で、sleep が使える（§6） | lock（今の `ZWL_LID_LOCK`）→ 画面を消す → sleep を頼む。開けて wake したら今の猶予の規則（15 分以内なら password 無しで unlock） |
| 電源ボタンの短押し | session でも greeter でも | session なら lock → sleep。greeter なら sleep だけ（lock する物が無い） |
| 無操作の時間 | 最後の入力（key・pointer・touch・pen）から設定の時間。0 は「しない」 | lock → sleep。抑止（§3.1）がある間は数えない |

- **sleep が使えない時**（QEMU、kernel が「未対応の platform」と答える、§6）: 蓋は今の「画面を消して lock」、電源ボタンは log だけ（今のまま）、無操作は
  画面を消すだけ（設定があれば）。
- 契機が重なった時（蓋を閉じた直後の電源ボタンなど）: sleep の要求が 1 つ出ている間（sessiond の答えを待つ間）は次を出さない
  （`kl_backend_power_action` が EBUSY）。

#### 3.1 無操作の抑止

- 全画面の動画・音を出している app（audiod に流れている stream があり、音量が 0 でない）・Wayland の idle-inhibit の protocol を使う app がある間は数えない。
- 外部の monitor に出している時（蓋を閉じた clamshell の使い方）は、蓋の契機で sleep しない（§7 の判断 1）。

### 4. 中止の理由の表示

- sessiond は kernel の `device`（`pci SSSS:BB:DD.F DRIVER`）と error を、次の言葉にして Keiland に渡す（翻訳の対象。英語を基に日本語も）:

| 原因 | 言葉（英語の基） |
| --- | --- |
| `intel-ax211` で EBUSY | "Sleep was cancelled: Wi-Fi could not be turned off." |
| `nvme` で EBUSY | "Sleep was cancelled: the disk was busy." |
| `xhci` で EBUSY | "Sleep was cancelled: a USB device was busy." |
| `i915` で EBUSY | "Sleep was cancelled: the display was busy." |
| suspend の口の無い driver（EOPNOTSUPP） | "Sleep was cancelled: DRIVER cannot sleep yet." |
| 未対応の platform（p006 の答え） | "This computer cannot sleep." |
| それ以外 | "Sleep was cancelled (DEVICE, error E)." |

- Keiland は WS156 の通知ができるまで、**画面の下の中央に 5 秒の toast**（lock の画面の上にも出す、文字だけ）で出す。WS156 ができたら通知に
  置き換える（通知の種類「system」）。
- 中止の記録は sessiond の log と kernel の dmesg に残る（`system: sleep … result … device …`）。

### 5. 再試行と連続の抑止

- 中止の後、同じ契機で 30 秒は試さない（蓋を閉じたままの時の繰り返し、無操作の繰り返しを防ぐ）。電源ボタンは押すたびに試す（利用者の明示の操作）。
- 蓋を閉じたまま中止が続く時は、今の「画面を消して lock」の状態に留まる。

### 6. sleep が使えるかの判断

- p006 の `KERN_SYSTEM_SLEEP_INFO`（design §7）で「S0 idle に対応」が返る時だけ契機で sleep する。Keiland は sessiond 経由で起動時に 1 度読み、
  `kl_backend_power_state.actions` の `KL_BACKEND_POWER_SUSPEND` の bit に載せる（今の power の state の形）。

### 7. 人間の判断（2026-10-05 朝、ユーザーが案のとおり決定、Q1 の中継）

1. 外部の monitor に出している時に蓋を閉じた: **sleep しない**（内蔵の panel だけ消し、外部に出し続ける）。**AC の有無に依らない**。
2. 無操作の時間の既定値: **AC 30 分・電池 15 分**、画面はその**半分**で消す。0 は「しない」。Settings の Power の頁で変える。
3. greeter で電源ボタン: **sleep**（lock は無い）。
4. sessiond が **session からも suspend を受ける**（ws131 の D12 の改訂。poweroff・reboot は greeter だけのまま）。

以下は決定の前に書いた案の記録。

#### 7.0 判断の案（決定の前）

1. **外部の monitor に出している時に蓋を閉じた**: 案は sleep しない（内蔵の panel だけ消し、外部に出し続ける clamshell）。AC の有無で変えるか。
2. **無操作の時間の既定値**: 案は AC で 30 分、電池で 15 分（画面を消すのはその半分）。0 で「しない」。Settings の Power の頁で変える。
3. **greeter で電源ボタン**: 案は sleep（session が無いので lock は無い）。Shut Down の確認を出す案（ws132 の D1 の元の案）もある。
4. **sessiond が session から suspend を受けること**（ws131 の D12 の改訂: poweroff・reboot は greeter だけのまま、suspend だけ session からも受ける）。

## 依存

- p006: `KERN_SYSTEM_SLEEP` の S0 idle の mode、`KERN_SYSTEM_SLEEP_INFO`、事象（`power.sleep.begin`・`end reason=…`・`failed device=…`）。H1〜H4 の承認の後。
- WS132 p008（蓋の lock の今の形）、WS089（Settings の Power の頁）、WS156（通知、無ければ toast で代える）。

## 確かめ方（code の後）

- host の試験: 契機の判断（蓋・電源ボタン・無操作・抑止・再試行の抑止・sleep が使えない時）を `lid.c` と同じく compositor から切り離した純粋な関数に
  して試す。sessiond の理由の言葉の変換、networkd の SLEEP-PREPARE・END の状態機械。
- QEMU（T1）: sleep が使えない platform で、蓋（QMP で lid は無いので電源ボタンの `system_powerdown`）が今の動き（log）のまま、無操作で画面が消えるだけ。
- 実機（5330）: 3 つの契機で入り、蓋・電源ボタンで戻る。Wi-Fi の接続が戻る。中止の toast（Wi-Fi を切れなくする等で起こす）。
