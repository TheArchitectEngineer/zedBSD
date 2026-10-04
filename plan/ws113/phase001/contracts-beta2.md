# WS113 p001: 残りの契約の確定（2026-10-05、q702、ベータ2）

Revision: 2026-10-05 / q702-i01 / P2 / source baseline main `41633f4`。
Status: 設計（文書だけ。製品の source・UAPI・実機は変えていない）。[contracts.md](contracts.md)（2026-10-02）の上に重ね、食い違う所はこの文書が優先する。
入力: 2026-10-02 ユーザーの D-ATOMIC の回答「推奨でよい」（(a)）、2026-10-04 夜のユーザーの指示（GOP の scanout の規則・Vulkan の Display の拡張での抜き差しの通知・compositor の動的な出力の変更・zedBSD を優先し Linux と FreeBSD の KMS は p010 へ・内蔵の LCD の明るさ）、Guardrail「GPU の driver の scanout の規則」（2026-10-04 ユーザー、補いを含む）、WS131 の構成（libkeiland-backend・`kl_system_manager_v1`）、WS051 の決定。

## 1. 決まったこと（この文書で確定、後続 Phase の入力）

| ID | 確定した内容 | 根拠 | 影響する Phase |
| --- | --- | --- | --- |
| D-ATOMIC | **(a) logical owner の同時の更新**。pointer が共有の辺を越えた motion の dispatch の中で、窓の木全体の owner と位置を一度に変える。合成する各 frame で、窓は owner の出力の render list にだけ入る（owner でない出力の frame に窓の pixel を一つも描かない）。物理の表示で最大 1 frame ほど両方の画面に見える・どちらにも見えないことは許す。present_wait・present_id の追加、2 つの head の同時の latch は要求しない。contracts.md §7.1 の厳格な案（消去の表示完了を待つ）は採らない | 2026-10-02 ユーザー「WS113は推奨でよいです。」 | p003（present_wait を足さない）、p004、p007（試験は合成の render list で判定）、p008 |
| D-GOP | **driver の scanout の規則**: 起動の時に driver が自分で scanout するのは firmware（GOP）が出していた出力先だけ。GOP の出力先なら interface（eDP・HDMI・DP・USB-C の DP-alt）を問わず初期化を試み、対応していない interface なら log を出して firmware の画面を保つ。他の出力先は、Keiland が libvulkan の Display の拡張の経路で明示に求めた時（native の `GPU_DISPLAY_CLAIM` と最初の `PRESENT`）だけ scanout を始める。driver は接続を検出しても自分で出力を足さない・移さない。今の i915 の外部の display の優先（`output.c` の `display=` の auto・hdmi）は廃止する | Guardrail（2026-10-04 ユーザー） | p002（i915）、p011（2 つ目の出力）、Venus も同じ規則（scanout 0 が firmware の出力先に当たる。今も自分では他に出さない、p003 で確かめる） |
| D-GOP-INV | native の列挙（`GPU_DISPLAY_QUERY`）は、GOP の出力先が scanout 中でも、接続している全ての出力を `GPU_DISPLAY_CONNECTED` で返す。scanout 中の出力にだけ `GPU_DISPLAY_ACTIVE`。列挙・mode の問い合わせは scanout を始めない（副作用なし） | D-GOP の帰結 | p002、p003 |
| D-RELEASE | 出力の lease の `GPU_DISPLAY_RELEASE` の後: GOP の出力先は driver の console の scanout（今の振る舞い、compositor が終わった時の画面）に戻る。それ以外の出力は pipe を止めて消灯する（driver が自分で点け直さない）。compositor が出力を使わない（消す）時は、その出力の swapchain を壊して lease を返す | D-GOP、WS051 の決定（出力 off は Keiland が決める） | p002、p011、p004 |
| D-BOOT2 | 保存した設定が無い最初の session では、Keiland が**接続している全ての出力を拡張**で使う（内蔵の panel が anchor で左、外部は右へ辺で付ける）。これは Keiland の明示の指示なので D-GOP に反しない。保存した設定があればそれを使う。kernel の起動の行の `display=hdmi`・`display=edp` は scanout の選択に使わなくなり、driver は「無視した」と log に出す（Keiland の anchor の hint にも使わない: 保存した設定と D-BOOT2 で足りる） | 2026-10-02 main の技術採択 D-BOOT の改訂（D-GOP の後）。**Q1 の確認項目 C1** | p002、p004、p005 |
| D-HOTPLUG | 動いている間に出力が足されたら、今のモードに加える（拡張: 保存した位置があればそこ、無ければ一番右の出力の右に辺で付ける。mirror: 複製）。抜かれたら、その出力の窓を残る出力へ退避し（D-REC）、0 台になったら窓を park する。Settings には snapshot の変更が届く | contracts.md §5・§7、D-REC | p004、p005、p007 |
| D-MODES | 製品の選択は今のとおり**全拡張と全 mirror の二択**。出力ごとの off・解像度・refresh・回転の選択は Settings に出さない（範囲外）。native の power の操作（下の D-EXT）は標準の拡張の全 entry の実装の責務として持つが、Settings の選択肢にはしない | 2026-10-02 ユーザー（二択）、native-contract.md §3 | p006 |
| D-EXT | `VK_EXT_display_control` の 4 entry と、依存の `VK_EXT_display_surface_counter` を**全部**実装してから広告する。抜き差しの通知は `vkRegisterDeviceEventEXT`（native の `GPU_DISPLAY_EVENTS` の topology の sequence）。`vkDisplayPowerControlEXT` は新しい native の power の操作、`vkRegisterDisplayEventEXT`（FIRST_PIXEL_OUT）は新しい native の refresh の境界の wait（i915 は pipe の vblank の割り込み、Venus は今の仮想の clock の境界で、仮想の adapter だと log と capability で明示）。surface の counter は**持たない**（`supportedSurfaceCounters = 0` は合法）ので `vkGetSwapchainCounterEXT` が成功する場合は無い | contracts.md §4・native-contract.md、2026-10-04 ユーザー「Vulkan display拡張でHDMIの挿抜の通知」 | p012（native）、p003 |
| D-UAPI | native に足す操作は `include/uapi/gpu-display.h` の 2 つ: `GPU_DISPLAY_POWER`（display_id・generation・ON/OFF/SUSPEND、副作用の前に検査）と `GPU_DISPLAY_REFRESH`（display_id・generation・登録時の cursor より後の実の refresh の境界を待つ、有限の timeout、lease 不要）。HAL（`include/hal/hal.h`）は変えない | native-contract.md §2・§3 | p012。**共有の UAPI の追加なので Q1 の許可が要る（C2）** |
| D-PROTO | Settings の経路は WS131 の規則に合わせ、**`kl_system_manager_v1` の version 4** の `get_displays`（新しい `kl_system_displays_v1`）にする（version 2 は WS134 の monitor、3 は ws089-p021 の network の set_scanning が使った）。contracts.md §8 の「専用の Wayland の管理の拡張」の内容（snapshot の begin/output/done、expected の serial、request_id、applied と saved の別、64bit 値の hi/lo、bounded な文字列・数）をこの object の request・event にする。libkeiland の公開 API は `kl_system_displays_*`（`KL_VERSION` の次の値） | Guardrail「app は kl_system_* でだけ」、2026-10-04 Q1 の記録 | p005、p006 |
| D-AUTH2 | 変更の request は今の `kl_system_*` の他の変更（WiFi・音量）と同じ扱い: compositor の socket は利用者の runtime の directory（0700）にあり、同じ UID の client だけが繋げる。active な session でない時（greeter・lock）は変更を拒む。D-AUTH の peer の credential の検査は、`kl_system_manager_v1` 全体に入れる時に一緒に（WS113 だけの仕組みは作らない） | D-AUTH（2026-10-02 main）、WS131 の構成 | p005 |
| D-STORE | 表示の設定は compositor が `~/.config/keiland/displays.conf`（version 付き、desktop.conf とは別）に書く: モード、persistent connector key（D-ID A2）ごとの拡張の位置、mirror の anchor、内蔵の panel の明るさ。書き方は tmp → fsync → rename。generation・token・lease は書かない | contracts.md §9 | p004、p005 |
| D-BRIGHT | **内蔵の panel（eDP）の明るさ**の経路: Settings → libkeiland（`kl_system_displays_set_brightness`）→ compositor（`kl_system_displays_v1`）→ libkeiland-backend（新しい `kl_backend_backlight_*`）→ kernel の**新しい backlight の device**（FreeBSD の backlight(9) と同じ形: `/dev/backlight/backlight0`、ioctl の BACKLIGHTGETSTATUS・BACKLIGHTUPDATESTATUS・BACKLIGHTGETINFO、明るさは 0〜100）。i915 は eDP の panel の backlight（`panel-backlight.c` の `drv_i915_lcd_modeset_brightness`、今は試験だけが呼ぶ）をこの device の provider として登録する。GPU の display の UAPI には足さない（compositor は GPU を libvulkan だけで扱うので、Vulkan に無い明るさを GPU の UAPI に置くと私有の Vulkan の拡張が要る。main は私有の拡張を採らない）。ACPI の `_BCM`（WS049）は後で別の provider として足せる（5330 の DSDT は BUG-165 で今は読めない）。Linux（sysfs の `/sys/class/backlight`）・FreeBSD（backlight(9) そのもの）は p010 | 2026-10-04 ユーザー「SettingsのDisplayには、内蔵LCDの場合、明るさ調節がほしい」 | p013（kernel の device と i915 の provider、backend）、p005、p006。**新しい UAPI（`include/uapi/backlight.h`）なので Q1 の許可が要る（C2）** |
| D-BRIGHT-KEY | Fn の明るさの key: compositor が evdev の `KEY_BRIGHTNESSDOWN`（224）・`KEY_BRIGHTNESSUP`（225）を受けたら 5 % ずつ変え、Settings の slider と同じ経路で backend に送り、変更を Settings に通知する。5330 で key がどこから来るか（ACPI の video の Notify 0x86・0x87、EC、PS/2 の scancode）は実機で確かめる（p008）。ACPI の Notify を input の key にする kernel の部分は WS049 の後続（BUG-165 の後）で、WS113 では作らない | 2026-10-04 ユーザー（明るさ）、p001 で決めるとされた点 | p005（compositor の key）、p008（実機）。ACPI の部分は WS049 へ（Q1 の調整 C3） |
| D-BRIGHT-BOOT | 起動の時の明るさは firmware の値のまま（driver は変えない）。session の始めに compositor が displays.conf の値を適用する。保存していなければ今の値を読んで使う | — | p005 |
| D-QEMU | QEMU の証拠: Venus（virtio-gpu）は 16 までの scanout と topology の event を持つ。`max_outputs=2` の guest で、列挙・2 つの swapchain・拡張と mirror・窓の移動・Settings を試す（T1）。抜き差しを QEMU で起こせるかは p003 で確かめ、起こせなければ抜き差しは実機（p008）だけで証明する。i915 の HPD・明るさ・2 つの物理出力は実機だけ | fixtures.md | p003〜p007、p008 |

## 2. 確認が要る点（Q1・ユーザー）

| # | 何を | 推奨 | 待つ物 |
| --- | --- | --- | --- |
| C1 | D-BOOT2: 保存した設定の無い最初の session で、GOP の出力先以外の接続済みの出力も Keiland が拡張で点けてよいか（D-GOP は driver の規則なので反しない）。代わりの案は「最初は GOP の出力先だけ、他は Settings で選ぶまで消したまま」 | 全て拡張（Windows・macOS と同じ。2026-10-02 main の D-BOOT のまま） | p004 の既定の動作 |
| C2 | 共有の UAPI の追加 2 件: `gpu-display.h` の `GPU_DISPLAY_POWER`・`GPU_DISPLAY_REFRESH`（D-UAPI）と、新しい `include/uapi/backlight.h`（D-BRIGHT、FreeBSD の backlight(9) と同じ ioctl）。HAL は変えない | 許可 | p012・p013 の開始 |
| C3 | Fn の明るさの key の kernel の部分（ACPI の video の Notify → input の key）を WS049 の後続に置くこと | WS049 へ（BUG-165 の後） | p008 の key の確認 |
| C4 | WS051 p002 との分担: GOP の出力先の引き継ぎと外部の優先の廃止（`takeover.c`・`output.c`）は一度だけ書く。WS113 p002 の part A で行い、WS051 p002 は VBT の DVO の code の修正と、part A を使う USB-C の分だけにする案 | WS113 p002 で行う（Q1 が順を決める） | p002 の開始 |

## 3. 契約の変更点（contracts.md への差分）

- §3: 「複数 output 化」は p011 に分けた（p002 は inventory・HPD・規則まで、2 つ目の出力を同時に出すのは p011）。
- §4: present completion（present_wait・present_id）は D-ATOMIC (a) で不要。§4.1〜4.3 の device event fence の契約はそのまま。
- §5: 0 台の時の振る舞いはそのまま。「1 台」は D-RELEASE に従い、使わない出力は lease を返して消す。
- §7.1: 厳格な案は採らない（D-ATOMIC）。p007 の判定は「合成した各 frame の render list で、owner でない出力に窓が入らない」こと。
- §8: 専用の拡張ではなく `kl_system_displays_v1`（D-PROTO）。payload と検査の表はそのまま使う。brightness の request（set_brightness(output_token, level 0〜100)）と event（output の行に brightness と has_backlight）を足す。
- §9: 保存に内蔵の panel の明るさを足す（D-STORE）。
- §10: D-ATOMIC は (a) で決定。

## 4. 実出力の照合（2026-10-05 の source、main `41633f4`）

- native: `gpu-display.h` に QUERY・MODE・CLAIM・RELEASE・PRESENT・WAIT・EVENTS はあり、power・refresh の操作は無い（D-UAPI で足す）。
- i915: `output.c` は 1 つの出力（`display=` の auto・hdmi で HDMI を優先、2026-09-29 の決定）で、起動の時に一度だけ選ぶ。D-GOP で置き換える。backlight の関数（`panel-backlight.c`）はあるが UAPI の口は無い。
- Venus: 16 までの protocol の scanout、topology の sequence（`display_events`）はある。
- libvulkan: `VK_KHR_display` の 7 entry と `vkCreateSharedSwapchainsKHR` はある。`VK_EXT_display_control`・`VK_EXT_display_surface_counter` は無い。
- compositor: `compose_display()` が最初の display だけを開く（`wayland/compose.c`）。出力の表・hotplug は無い。
- libkeiland-backend-zedbsd: `display-zedbsd.c` は何もしない（libvulkan が display に届く）。backlight の module は無い。
- `kl_system_manager_v1` は version 3（request 6 まで）。peer の UID の検査は無い。
- Settings の Display の頁（`page-*.c` の `se_display_draw`）は読むだけ。


## ユーザーの決定（2026-10-05 未明）

- C1（D-BOOT2）: **全部拡張で点ける**（保存の設定が無い最初の session は、接続された全部の display を拡張の mode で使う）。
- C4: Q1 が案のとおり決定（GOP の引き継ぎと外部の優先の削除は WS113 p002 part A、WS051 p002 は VBT の DVO と USB-C）。
- C2: **両方許可**（gpu-display.h の GPU_DISPLAY_POWER・GPU_DISPLAY_REFRESH と新しい include/uapi/backlight.h。HAL は不変）。差分は p012・p013 の記録に示す。
- C3: Fn の明るさのキーの kernel 側（ACPI video の Notify 0x86/0x87 → input の key）は **WS049 に入れる**（BUG-165 の後）。compositor の KEY_BRIGHTNESS* の扱いは WS113。
- 出力の数の制限（2026-10-05 未明 ユーザー「GOP以外のディスプレイについて、マシンが同時に表示できる画面数の制限で、有効化できないケースが存在するので、その場合は可能な限りエラーではなく制限であることを返してください。Vulkan Display拡張にそういうエラーがないなら、失敗しても致命的でなく一時的なものとして処理できるようにしてください。」）: GOP 以外の出力が machine の同時表示の数（pipe・transcoder・PLL など）の制限で点けられない時、driver と UAPI は一般の error でなく「制限」と分かる結果を返す。Vulkan の Display の拡張にそれに当たる error が無ければ、失敗を致命的でなく一時的な物として扱う（compositor はその出力を使わずに続け、Settings はその display を制限で使えないと示し、hotplug や他の出力の解放の後に再び試す）。p002・p003・p004・p011 の設計に入れる。

### 反映（2026-10-05、P2、q702-i02）: D-LIMIT（出力の数の制限）

| ID | 内容 | 影響 |
| --- | --- | --- |
| D-LIMIT | **native**: GOP の出力先でない出力を、同時に出せる数（pipe・transcoder・PLL・帯域、driver が今出せる数を含む）の制限で点けられない時、`GPU_DISPLAY_CLAIM` は `ENOSPC` を返す（他の失敗の errno と区別する。interface を driver が扱えない時は `EOPNOTSUPP`、持ち主の衝突は `EBUSY`）。p002 の i915 は一度に 1 つしか点けないので、resident でない出力の claim は `ENOSPC`（p011 の後は資源が本当に足りない時だけ）。Venus も host の scanout の数を超えたら `ENOSPC`（p003 で確かめる）。**libvulkan**: Vulkan の Display の拡張に「制限」の error は無いので、`ENOSPC` は swapchain の作成（display の claim）で `VK_ERROR_INITIALIZATION_FAILED` にする（device は失わない、致命的でない。今の `display_error()` は未分類の errno を `SURFACE_LOST` にしているので p003 で分ける）。**compositor**: anchor でない出力の swapchain の作成が `INITIALIZATION_FAILED` なら、その出力を「limited」の状態にして使わずに続ける（他の出力と server は保つ）。topology の変化（hotplug）と、他の出力の swapchain の解放の後に再び試す。**protocol と Settings**: snapshot の output の flags に `limited` を足し、Settings はその display を「同時に表示できる数の制限で使えません」と示す（モードの選択は残る出力に効く）。native の QUERY に「制限」の flag を足すかは p012 の UAPI の差分で検討（今は claim の結果だけで分かる） | p002（済み）、p003、p004、p005、p006、p011、p012 |
- firmware の画面が全く無い時（点いた pipe が無い）: ユーザー「この場合、内蔵のPanelと判断できるものを点灯してください。すべて点灯できなかった場合も、起動に影響させません。ただ、そんなケースはまず存在しない気がします。」→ 内蔵の panel と判断できる出力を点ける。どれも点けられなくても起動に影響させない。
- protocol の版（2026-10-05 Q1）: kl_system_manager_v1 の version 4 は ws160-p002（Settings の Users の password、account の object）が先に使う。WS113 の get_displays と kl_system_displays_v1 は **version 5** にする（D-PROTO の改訂）。
- protocol の版（2026-10-05 Q1、改訂）: version 5 は ws132-p004 の devices（mount と new）。WS113 の get_displays と kl_system_displays_v1 は **version 6**。
- protocol の版（2026-10-05 Q1、再改訂）: version 6 は ws089-p022 の network の configure_wired。WS113 の get_displays と kl_system_displays_v1 は **version 7**。
