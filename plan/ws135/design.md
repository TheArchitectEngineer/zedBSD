# WS135 設計: 設定の読み書きを libkeiland に一本化し、desktop.conf を compositor の内部に

ws135-p001（q652-i01、P2、2026-10-04）。第 2 版（design-reviewer の review の 16 項目を反映、対応は §11）。source は main a21e0fd を読んだ。build・試験はまだ流していない。

## 0. 要約

- app の設定の読み・変更・監視は新しい libkeiland の API `kl_settings_*` だけを通る。app は設定の file を開かない（app の data との区別は §1.4、D3）。
- 項目ごとに解決の先が決まっている（§2）。
  - **compositor の項目**（壁紙・窓の透明度・pointer・keyboard の repeat・音量・消音）: libkeiland が compositor の拡張 `kl_system_settings_v1` で頼み、
    compositor が session の間メモリに持ち、適用する。音量・消音は compositor が backend の audio で audiod に頼む（今の system bar と同じ link。D2）。
  - **libkeiland が直接解決する項目**（app だけの設定、例 Terminal の `terminal.ambiguous-wide`）: libkeiland が app の file を読み書きする（D3）。
  - app からは区別が見えない（同じ get・set・watch）。
- 監視: compositor は値が変わるたびに（どの client・system bar の変更でも）全ての settings の object に `value` を送る。libkeiland は変化を貯め、
  app が呼ぶ `kl_settings_dispatch()` の中で watch の callback を呼ぶ。毎秒の file の poll は無い。
- `desktop.conf` は compositor だけの file: session の開始で一度読み、session の終わりで一度書く（この session で変えた key だけを、その時の file に merge）。
  他の process は読まない・書かない。compositor の毎秒の監視の thread と libkeiland の `keiland_preferences_*` の公開 API を除く。
- 拡張の manager `kl_system_manager_v1` は WS131 p010 の予定の物。WS135 が先に v1（`get_settings` と `capabilities`）を作り、WS131 p010 が network・audio・power の
  request を足す案を推す（D1）。

## 1. 棚卸し（main a21e0fd）

### 1.1 file と API

| 所在 | 役割 |
| --- | --- |
| `keiland/keiland.h:1074-1138` | 公開 API `keiland_preferences_open/close/reload/get/get_int/set/unset`（KEILAND_VERSION 13 で入った。今の版は 21、`keiland.h:49`）。key 64・value 256 byte |
| `libkeiland/preferences.c`（922 行） | `~/.config/keiland/desktop.conf` の key=value。書き（`preferences_change`、`:560-620` 付近）は毎回 flock・読み直し・自分の 1 key だけ変える・fsync・rename、だから 2 つの書き手が互いの変更を失わない。file は 64 KiB まで（`:41`） |
| `wayland/preferences.c`（482 行） | compositor。session（greeter でない時）の開始で読み（`main.c:138`）、watcher の thread が 1 秒ごとに stat と読み（`:237-360`）、変わった key を適用（`:364-482`）。範囲の定数は `:45-56`。BUG-125 の後に thread になった |
| `wayland/volume.c:200-250` | `zwl_volume_keep`: session の終わり（`handoff.c:89` の Log Out、`main.c:1002` の終了）に `sound.volume`・`sound.muted` を書く（BUG-161） |
| `wayland/volume.c:702-750` | audiod に最初に届いた時に `sound.*` を一度読んで audiod へ渡す |
| `wayland/seat.c:1076-1085`・`input-method.c:1393-1396` | keyboard の repeat は keyboard の bind（と IME の grab の作成）の時だけ送る。動いている app には今の変更が効かない |
| `wayland/glass.c:326-357`・`:429-435`・`:1045` | 壁紙の読みと decode は event loop で、`vkDeviceWaitIdle` の後。読めない時は log を出して風景を描き 0 を返す。`file_read` は通常の file か確かめずに `open` する（FIFO で止まる） |
| `wayland/main.c:142-149` | SIGINT・SIGTERM は `stop_service`（終了の経路）。SIGHUP の handler は無い（既定の動作で書かずに終わる） |
| `settings/look.c:106-247, 440-500` | Settings の外観・入力の頁。open で読み、1 秒ごとに `keiland_preferences_reload`（`:144`）、選んだ時に `set`・`unset`（`:485-487`）。`se_look_set_number`・`se_look_set_opacity`（`:216-247`）は「既定の値なら unset」 |
| `settings/page-input.c:253, 290-296`・`page-look.c:245, 273-279`・`settings.h:624` | 上の頁の操作の呼び手 |
| `settings/sound.c:55-63, 149-157` | Sound の頁。初めの表示に `sound.*` を desktop.conf から読み、その後は audiod（`keiland_audio_*`）。頁の表示は audiod の `reachable`・`device` で決まる |
| `libkeiland/audio-compat.c` | `keiland_audio_*` は `kl_backend_audio_*` への forward（backend の source を libkeiland に build する暫定。WS131 p011 で除く予定、WS131 D4 (b) に反する暫定） |
| `terminal/settings.c` | Terminal だけの `~/.config/keiland/terminal.conf`（`ambiguous-wide`）。他の terminal は起動の時に読む |
| `wayland/zwl.h:757-769, 1160-1162` | server の `preferences`・`preferences_checked_ms`・`window_opacity_started`・`wallpaper_started`、`zwl_preferences_*` |

### 1.2 desktop.conf の項目（今の key と範囲）

| key | 型・範囲 | 既定 | 書き手（今） | 読み手（今） |
| --- | --- | --- | --- | --- |
| `wallpaper` | 絶対 path、他は既定 | compositor の `--wallpaper` | Settings | compositor・Settings |
| `window.opacity` | 85〜100（%） | compositor の `--window-opacity`（1〜100、`main.c:467-472`。範囲の外もある） | Settings（100 で unset） | compositor・Settings |
| `pointer.speed` | 25〜300 | 100 | Settings | compositor・Settings |
| `pointer.natural` | 0/1 | 0 | Settings | compositor・Settings |
| `keyboard.repeat.rate` | 5〜60 | 25 | Settings | compositor・Settings |
| `keyboard.repeat.delay` | 150〜1000 | 400 | Settings | compositor・Settings |
| `sound.volume` | 0〜100 | audiod のまま | compositor（session の終わり） | compositor（audiod に届いた時）・Settings（初めの表示） |
| `sound.muted` | 0/1 | 0 | 同上 | 同上 |
| 未知の key・comment | — | — | 手での編集 | 誰も（file に残る） |

### 1.3 試験と道具

- `plan/ws089/tests/settings-p004.sh`〜`p012.sh`・`settings-pages.sh`: guest の `desktop.conf` を消す・書く・読む。
- `plan/ws089/tests/host-preferences.c`・`host-preferences.sh`・`host-build.sh:32`（Settings の host 試験が `libkeiland/preferences.c` を compile する）。
- `plan/ws100/tests/volume-p004.sh`・`p005.sh`: session の間に desktop.conf が変わらないことと Log Out の後の値を読む。`volume-bug153.sh:114` は session の間に
  desktop.conf を手で書く（「手での編集を追わない」の確認）。

### 1.4 app の file（設定か data か）

| file | source | 分類（案） |
| --- | --- | --- |
| `~/.config/keiland/terminal.conf` | `terminal/settings.c` | app の設定（D3） |
| `~/.config/keiland/open-with` | `files/apps.c:56, 391-418` | app の設定（種類ごとの app の選択。D3） |
| `~/.config/keiland/desktop-layout` | `files/desktop-layout.c:48, 250-265` | app の data（desktop の icon の位置） |
| `~/.config/keiland/tags` | `files/tags.c:89` | app の data（file の tag） |
| `~/.config/files`（places） | `files/places.c:531-557` | app の data（場所の一覧） |
| `~/.config/kei/ime/ja-user.dict` | `ime/main.c:318-333` | app の data（学んだ変換） |
| Wi-Fi の鍵 | networkd の wifi-store | system の data（WS131 の network の領域、WS135 の外） |

## 2. 項目ごとの解決の先

### 2.1 表（key の表。compositor と libkeiland が同じ表を使う、§3.5）

| key | 解決の先 | 型 | 検査（`set`） |
| --- | --- | --- | --- |
| `wallpaper` | compositor | path | 絶対 path、長さ。compositor が通常の file・大きさを確かめ、読み・decode の結果で `result`（§4.4） |
| `window.opacity` | compositor | int | 85〜100 |
| `pointer.speed` | compositor | int | 25〜300 |
| `pointer.natural` | compositor | bool | 0/1 |
| `keyboard.repeat.rate` | compositor | int | 5〜60 |
| `keyboard.repeat.delay` | compositor | int | 150〜1000 |
| `sound.volume` | compositor → backend の audio → audiod | int | 0〜100（左右同じ値）。compositor が audiod に最初の値を渡す前は `busy`（§4.5） |
| `sound.muted` | 同上 | bool | 0/1 |
| `sound.available` | compositor（読むだけ） | bool | `set` は `denied`。audiod に届き device がある時 1 |
| `terminal.ambiguous-wide` | libkeiland（app の file、D3） | bool | 0/1 |
| `files.open-with.<type>`（§2.4 の prefix の行） | libkeiland（app の file `files.conf`） | opener | §2.4 |

- 未知の key の `set` は `unsupported`（D5）。表に無い key は保存しない。手での編集の未知の key と comment は desktop.conf に残る（§4.3）。新しい設定は表に行を足す。
- 範囲の外の値の `set` は `invalid`（丸めない）。file の手での編集の値は読みで範囲に丸める。
- 既定の値は解決の先が決め（`--wallpaper`・`--window-opacity` の command line）、**丸めずに** `flags=default` で渡す（範囲の外の既定もそのまま見える）。

### 2.2 音量（D2）

- 2026-10-03 user「デスクトップから変更する音量は。audiodに依頼をするのであって、あとのことはaudiodに任せてください。」と WS131 の確認1（Settings → libkeiland →
  compositor の拡張 → compositor → backend → audiod）の両方に合わせ、`sound.*` は compositor の項目にする。compositor は今の system bar の link（`volume.c` の
  `kl_backend_audio_*`）で audiod に頼み、audiod の報告を store の値として全ての client に送る。値を持つのは audiod（compositor の store は audiod の写し）。
- session をまたぐ保存は今のまま（BUG-161）: compositor が session の終わりに audiod の値を desktop.conf に書き、次の session で audiod に最初に届いた時に渡す。
- Settings の Sound の頁の状態の表示（audiod に届いたか・device）は `sound.available` で読む。feedback の短い音は設定ではない操作なので、WS131 p011 で
  `kl_system_audio_v1` の `feedback` ができるまで今の `keiland_audio_feedback` のまま（Settings の audiod の link はこの 1 本だけ）。

### 2.3 app だけの設定（D3）

libkeiland が `~/.config/keiland/<app>.conf` を直接読み書きする（今の `terminal/settings.c` の書き方: 1 key を置き換え、fsync、rename）。監視は同じ process の
中の変更だけを通知し、別の process（別の Terminal）の変更は**通知しない**（file の poll をしないため）。他の process は起動の時に読む。
これは WS135 の完了の条件 3（別の process の変更も届く）を app だけの設定では満たさない。満たすには compositor の store に入れる（D3 (c)）。判断は D3。

### 2.4 prefix の行: Files の open-with（2026-10-04 追加、Q1「open-with は WS135 で行って」）

Files の「Always Open With」（種類ごとに既定の app を選ぶ）は種類ごとの動的な key で、固定の key の表に入らない。表に **prefix の行**を足す。

| 行 | 解決の先 | 型 | key の検査 | 値の検査 |
| --- | --- | --- | --- | --- |
| `files.open-with.`（flag `KL_SETTINGS_KEY_PREFIX`） | libkeiland → `~/.config/keiland/files.conf` の行 `open-with.<type>=<NAME><TAB><COMMAND>` | opener | prefix の後ろが MIME の型: 小文字・数字・`.`・`+`・`-`・`_` と、先頭でも末尾でもない `/` がちょうど一つ。key 全体は `KL_SETTINGS_KEY_MAX` 未満 | NAME と COMMAND が空でなく、制御文字が無く、その間の TAB がちょうど一つ。全体は `KL_SETTINGS_VALUE_MAX`（256）未満（Files の command の上限 512 より狭い: 長い command の選択は `EINVAL` で残らない） |

- 既定（選ばれていない）は「値が無い」（`get` は `EAGAIN`）。`reset` は file の行を除く。
- `kl_settings_key_find` は固定の行の名前の一致、無ければ prefix の行で suffix を検査して返す。`kl_settings_name_valid`（一般の key の書式）は prefix の key には使わない。
- cache は固定の行の entry に加え、prefix の key の entry を値の来た時に作る（上限 64）。watch は prefix の key にも効く（prefix の一致）。
- Files（`files/apps.c`）: 選んだ既定は `kl_settings_open(NULL, "files")` で読み書き（開くたびに読む、今と同じ）。`fm_apps_set_default` は `set`、`fm_apps_clear_default` は `reset`、
  `fm_apps_has_default` は `get`。`fm_apps_for` は選んだ既定を最初に、続けて利用者が手で書く list（`~/.config/keiland/open-with`、PATTERNS<TAB>NAME<TAB>COMMAND）、
  system の list、組み込み。**Files は利用者の list を書かない**（今までの「# set by Files」の行の書き換えを除く）。利用者の手で書く list は `/etc/keiland/open-with` と
  同じ「関連づけの list」（data）として読むだけにする（mailcap と同じ扱い。設定の file ではない）。その中に残る昔の「# set by Files」の行は読まない（この変更の前に
  選んだ既定は失われる。開発中の版なので移行はしない）。

## 3. libkeiland の API（`kl_settings_*`）

### 3.1 名前と置き場

WS131 §5.1 の規則（新しい名前は最初から `kl_`）。宣言は `keiland.h`、実装は `libkeiland/settings.c`（client の wire）と `libkeiland/settings-cache.c`
（値の cache・watch の表・通知の合流。wire から切り離し host で試す）。protocol の `wl_interface` の表は `settings.c` の中で static（WS131 review 17）。
`exports.map` は header の関数の一覧から作り、key の表の symbol は出さない（WS131 §5.4）。

### 3.2 関数（案）

```c
/* The longest key and value, with their NUL. */
#define KL_SETTINGS_KEY_MAX	64U
#define KL_SETTINGS_VALUE_MAX	256U

/* A value the user did not choose: its resolver's default. */
#define KL_SETTINGS_DEFAULT	0x1U

struct kl_settings;

/* Called from kl_settings_dispatch for a key whose value changed; value is NULL when the key has none now. */
typedef void (*kl_settings_watch_fn)(void *data, const char *key, const char *value, unsigned flags);

struct kl_settings *kl_settings_open(struct wl_display *display, const char *app);	/* app names the app's own settings (D3); NULL for none */
void kl_settings_close(struct kl_settings *settings);

int kl_settings_get(const struct kl_settings *settings, const char *key, char *value, size_t size, unsigned *flags);	/* 0, ENOENT, ENOTSUP, EAGAIN, ERANGE */
int kl_settings_get_int(const struct kl_settings *settings, const char *key, int fallback);
int kl_settings_set(struct kl_settings *settings, const char *key, const char *value, uint32_t *request);	/* 0 when sent */
int kl_settings_set_int(struct kl_settings *settings, const char *key, int value, uint32_t *request);
int kl_settings_reset(struct kl_settings *settings, const char *key, uint32_t *request);	/* back to the resolver's default */

int kl_settings_watch(struct kl_settings *settings, const char *prefix, kl_settings_watch_fn fn, void *data, unsigned *watch);	/* "" is every key */
void kl_settings_unwatch(struct kl_settings *settings, unsigned watch);

int kl_settings_dispatch(struct kl_settings *settings);	/* never waits: the settings' own queue, then the callbacks */
int kl_settings_take_result(struct kl_settings *settings, uint32_t *request, int *error);	/* 1 with a finished request, 0 when none */
```

`unset` は `reset` と呼ぶ（「既定に戻す」の操作）。WS131 §4.4 の `kl_system_settings_*` と `kl_system_take_result` の settings の部分はこの API に置き換わる。
WS131 p015 の `kl_app` は `kl_app_settings(app)` でこの object を持つ（§8）。

### 3.3 event loop と queue

- `kl_settings_open` は libkeiland の**専用の queue** を作り、global の探しも manager・settings の object もその queue に置いたままにする（queue を移さない）。
  zedBSD の libwayland の `wl_proxy_set_queue`（`libwayland/proxy.c:343-368`）は既に queue に入った event を元の queue に残すので、移すと snapshot の後の
  `value` を失いうる。専用の queue なら失わない。
- open は `get_settings` の snapshot（全ての compositor の項目の `value` ＋ `done`）までを `wl_display_roundtrip_queue` で一度だけ待つ（起動の時）。以後は待たない。
- 読みは app の event loop がする: app は display の fd を今どおり poll し、`wl_display_prepare_read`・`read_events`（全ての queue に振り分けられる）の後に
  `kl_settings_dispatch()` を呼ぶ。dispatch は `wl_display_dispatch_queue_pending(display, queue)` で専用の queue を処理し、その後に callback を呼ぶ。
  app の loop の `wl_display_dispatch_pending` は既定の queue だけなので、settings の event を処理しない。送りは app の loop の `wl_display_flush`。
- libkeiland の Wayland の他の部品（titlebar・glass）と同じ display を共有する。thread は一つ（app の event loop の thread）。

### 3.4 約束

| 約束 | 内容 |
| --- | --- |
| callback の時 | `kl_settings_dispatch()` の中だけ（listener は cache を直し「変わった key」の印を付けるだけ）。WS131 §3.4 の「callback の前に状態を確定させる」と同じく、callback の時の `get` は確定した値 |
| 合流 | 一つの dispatch で同じ key が何度変わっても callback は一度、最後の値で。最後の値が前に app へ渡した値と同じなら呼ばない（A→B→A は呼ばない） |
| 再入 | callback の中で `get`・`set`・`reset`・`watch`・`unwatch` は呼んでよい。`close`・`dispatch` は呼ばない。callback の中で足した watch は次の dispatch の変化から受ける。外した watch はその dispatch の残りでも呼ばない（印を付け、走査の後に除く） |
| cache | `get` は compositor が最後に送った確定の値（`done` まで見せない）。`set` の直後の `get` は前の値（楽観的に変えない）。値は compositor の `value` で返り、watch が呼ばれる（自分の変更も）。slider の drag の間は app が自分の値を表示する（今の `look->dragging` と同じ） |
| 結果 | `set`・`reset` は送ったら 0 と `request`。compositor は必ず一度 `result` を返し、`take_result` で取れる。app の file の項目は即座に結果が出る |
| compositor の喪失 | 接続が切れたら compositor の項目の `get` は `ENOTSUP`、watch へ `value=NULL`。他の compositor（global が無い）も `ENOTSUP`。Settings は該当の行を「この desktop では変えられない」と出す（WS131 §4.1-6） |

### 3.5 key の表の置き場

compositor は libkeiland の client の部分を使わない（WS131 D4 (a)・(c)）。key の表（key・解決の先・型・最小・最大・既定の決め方）は client でも server でも無い
小さな共有の source `userland/desktop/settings-keys/settings-keys.c`・`.h`（`picture/` と同じ形の中立の dir）に置き、libkeiland と compositor がそれぞれ compile する。
表は `static const` の配列と探す関数（macro の列にしない）。protocol の opcode・列挙の定数は WS131 D4 (c) のとおり `userland/desktop/include/` の共有の header。

## 4. compositor

### 4.1 store

`wayland/settings-store.c`（新）: session の間、全ての compositor の項目をメモリに持つ表（key・値・既定か・この session で変えたか）。読み・解釈・書き
（`libkeiland/preferences.c` の行の保存・lock・fsync・rename）をここへ移す（libkeiland から desktop.conf の書き手を除く、WS131 D4 (d)）。今の
`preferences_apply`（`wayland/preferences.c:364-482`）は key ごとの適用の関数になる。

### 4.2 protocol

- global `kl_system_manager_v1`（D1、名前は WS131 D15）: v1 は request `destroy`・`get_settings(new_id)`、event `capabilities(bits)`（WS131 §4.2 と同じ形。
  settings の bit だけを立てる）。WS131 p010 が `get_network` などを後ろに足す（ABI は版を上げず変えてよい、WS131 §5.5）。greeter の時は global を出さない。
  同じ uid の client にだけ見せる（WS131 D5、`zwl_ime_global_visible` と同じ仕組み、`protocol.c:433`・`:613`）。peer の uid は `kl_backend_peer_uid()`
  （WS131 p010 の範囲 1 の backend の interface、3 OS）を WS135 p002 で先に足す（§7、Q1 が WS131 と調整）。
- object `kl_system_settings_v1`:
  - request: `set(request, key, value)`・`reset(request, key)`・`destroy`。
  - event: `value(key, value, flags)`・`done(serial)`・`result(request, applied, saved)`。
  - 作った時に全ての compositor の項目の `value` ＋ `done`。変化は、変わった key の `value` を**全ての** settings の object に送り、`done`。
  - `result` は WS131 §4.1-2 の共通の形（`applied` は適用の結果の列挙、`saved` は記録の結果）。settings の `saved` は「session の終わりに書く store に入った」で、
    `applied` が ok なら `ok`（session の間は disk に書かないので、disk の失敗はここに出ない。session の終わりの書きの失敗は log）。列挙は WS131 §4.1-3
    （`ok`・`denied`・`unsupported`・`busy`・`invalid`・`unavailable`・`failed`・`not_saved`）。errno の数値は送らない。
- 一つの `set` の流れ: 検査（§2.1）→ store を変える → 適用 → 全員に `value`＋`done` → 頼んだ client に `result`。壁紙だけは §4.4 の非同期。全て event loop の中で
  disk を待たない。

### 4.3 desktop.conf の読みと書き

| 時 | 内容 |
| --- | --- |
| 読み | session（greeter でない時）の開始で一度、最初の frame の前（今の `main.c:138` の位置、今と同じ同期）。範囲の外の値は丸めて適用。読みが ENOENT 以外で失敗した（EIO、64 KiB を超える）session は、終わりに file を全体で書き直さない（下の merge だけ） |
| 書きの意味 | **merge**: lock → その時の file を読み直す → この session で変えた key（開始の値との差）だけを置き換えるか除く → 他の行（手での編集・未知の key・comment・他の compositor の変更）はそのまま → fsync → rename。今の `preferences_change` の約束を session の差の集合へ広げた物 |
| 書きの時（表） | 下の §4.3.1 |
| 書かない | session の間は書かない。差が無ければ書かない（今の `zwl_volume_keep` の `write=0` と同じ考え） |
| 手での編集 | 監視しない（WS131 D6）。session の間の手での編集は、その key をこの session で変えていなければ終わりの merge で残り、次の session の開始で読まれる |
| 失う物 | compositor が crash した時（SIGKILL を含む）は、その session の変更が残らない（ユーザーの方針「それ以外に書き込む必要はないです」の帰結。D4） |

#### 4.3.1 session の終わりの経路

| 経路 | OS | 書き |
| --- | --- | --- |
| Log Out（home の menu → `zwl_handoff_logout`、`handoff.c:71-95`） | zedBSD（sessiond が QUIT を返すまで表示を続ける） | event loop で store の差の text（不変の snapshot）を作り worker の thread へ。worker は lock・読み直し・merge・rename だけ。Log Out の後 QUIT までの `set` は受けて store に入れ、終了（下）でもう一度 merge する |
| Log Out | Linux・FreeBSD（session の manager が無い: `kl_backend_session_logout` の後に compositor がそのまま終わる、`handoff.c:82-85`） | 終了の経路で同期の merge |
| compositor の終了（`main.c:1002`、SIGINT・SIGTERM の `stop_service`） | 全て | worker を join し、Log Out の後に変わった物・Log Out を通らなかった時の差を同期で merge（終了の経路なので待ってよい） |
| SIGHUP | 全て | 今は handler が無く書かずに終わる。`stop_service` を SIGHUP にも付け、終了の経路へ（p002） |
| Shut Down・Restart | zedBSD の session には無い（WS131 D12。greeter だけ、`greeter.c:382-388`。session からは Log Out の後の greeter で、上の Log Out の経路）。Linux の logind の電源（WS131 p006 の後）は compositor の停止の SIGTERM か Log Out の経路 | 上と同じ |
| greeter | 全て | store を持たない（読まない・書かない） |
| crash・SIGKILL | 全て | 書かない（D4） |

### 4.4 壁紙（event loop で disk を待たない）

今の `zwl_glass_wallpaper` は event loop で読み・decode し、読めない時も 0 を返す（§1.1）。p002 で分ける:
1. 検査: `open(O_RDONLY | O_NONBLOCK)`・`fstat` で通常の file と大きさの上限（FIFO・device を拒む: `invalid`）。
2. 読みと decode は worker の thread。結果（画素か error）を event loop の次の回で受け、`vkDeviceWaitIdle` と image の更新を今の所で行う（ここは今と同じ同期）。
3. 成功で store を変え、`value` を送り、`result(applied=ok)`。失敗は store を変えず `result(applied=failed)`（今の「風景を描いて 0」をやめる）。
4. session の開始の読みの壁紙は今どおり最初の frame の前に同期で読む（起動の時）。

### 4.5 音量の順序

compositor は audiod に最初に届いた時に desktop.conf の値を渡す（`volume.c:702-750`）。その前の `set sound.*` は `busy`（Settings が先に届いて compositor の
復元に上書きされる取り合いを無くす）。届いた後の `set` は `kl_backend_audio_set_volume`、audiod の報告で store が変わり `value` を送る。system bar の変更も同じ。

### 4.6 keyboard の repeat の即時の反映

`keyboard.repeat.*` が変わったら、bind 済みの全ての `wl_keyboard`（version 4 以上、`seat.c:1081-1084`）と IME の grab（`input-method.c:1393-1396`）へ
`repeat_info` を送り直す（WS135 の完了の条件 5「即座に効く」）。pointer の速さ・向きは今も次の入力から効く。

### 4.7 除く物

- `wayland/preferences.c` の watcher の thread・`PREFERENCES_CHECK_MS`・`zwl_preferences_tick` の file の比較（BUG-125 の元）。
- libkeiland の `keiland_preferences_*` の公開 API と `libkeiland/preferences.c`、`exports.map` の行。
- `settings/look.c` の 1 秒ごとの reload、`settings/sound.c` の desktop.conf の読み。
- log の文字列（`ZWL PREFERENCES …`）は**変えない**（改名は WS131 p022 の log の接頭辞の改名と一緒に扱う）。

## 5. app の移行

| app | 変更 |
| --- | --- |
| Settings の外観・入力の頁（`look.c`・`page-input.c`・`page-look.c`・`settings.h`） | `kl_settings_open(display, NULL)`、表示は `kl_settings_get_int`・`get`（`flags` の default も）、選んだ時は**常に `set`**（「既定の値なら unset」をやめる: compositor の既定が 100 でない session で 100 が別の値になるため）、「既定に戻す」の操作だけ `reset`（頁に必要なら足す、p004 で決める）。`kl_settings_watch(settings, "", …)` で他の process・system bar の変更を表示へ。`take_result` の失敗を今の message の行に。main loop（`settings/window.c:262-309`）で display の読みの後に `kl_settings_dispatch` |
| Settings の Sound の頁（`sound.c`） | 音量・消音の表示と変更は `kl_settings`（`sound.volume`・`sound.muted`・`sound.available`）。feedback の音だけ今の `keiland_audio_feedback`（§2.2） |
| system bar（compositor の中） | compositor は store を直接使う（compositor は client の API を使えない、WS131 D4 (a)）。WS135 の完了の条件 1・5 の「system bar も libkeiland の API を通る」はこの形に読み替える（D6） |
| Terminal | D3 (a) なら `kl_settings_open(display, "terminal")` と `terminal.ambiguous-wide`（p005） |
| Files | D3 で open-with を設定とするなら p005 に入れる。他の file は app の data（§1.4） |
| 他の app | 今は desktop.conf を使っていない。今後の設定は `kl_settings_*` |

## 6. 試験

- host:
  - store（`settings-store.c` を host で compile）: 読みの解釈（未知の key・comment・範囲の外・壊れた行・64 KiB 超）、set の検査、**merge**（session の間の手での編集が
    残る・2 つの store の merge・読みが失敗した後に file を壊さない・差が無ければ書かない）、rename で半端な file を残さない。`host-preferences.c` を作り直し、
    `host-build.sh:32` の `libkeiland/preferences.c` を外す（p004）。
  - `settings-cache.c`（wire なし）: snapshot と `done` の段、合流（A→B→A）、callback の中の watch・unwatch、compositor の喪失。
  - `settings-keys.c` の表と検査、app の file の項目の読み書き（D3）。
- 静的な検査: `plan/tools/keiland-os-boundary/check.sh` に「`desktop.conf` の文字列は `wayland/settings-store.c` の中だけ（試験と plan を除く）」を足す。毎秒の
  監視が無いことは code の不在（`PREFERENCES_CHECK_MS`・watcher の symbol が無い）で確かめる。
- QEMU（T1・T2）:
  - probe `keiland-settings`（test 用の小さな client、`kl_settings_*` で get・set・reset・watch を行い 1 行ずつ出す。`userland/tests/` の WS106 の配置）。
  - 2 つの probe の間の通知（別の process の変更が届く）、system bar の音量の変更が probe の watch に届く。
  - Settings の頁（`settings-p004.sh`〜`p012.sh`・`settings-pages.sh`）を「desktop.conf を書く」から「probe か Settings の操作で変える」へ。適用は今の
    `ZWL PREFERENCES key=… applied` の log。keyboard の repeat は動いている app に効く（repeat_info の送り直しの log）。
  - session の間 desktop.conf の mtime が変わらない、Log Out の後に変わり値を持つ、次の login で効く。SIGTERM の停止でも書く。session の間の手での編集
    （`volume-bug153.sh:114` の形）が追われず、終わりの merge で残る（試験の目的を「追わない・消さない」に書き換え）。
  - 壁紙の失敗（存在しない path・FIFO）で `result` が failed、compositor が止まらない。
  - BUG-125 の menu の遅れの試験（ws099-p020）を回帰に。
  - 試験が session の前に desktop.conf を書く（種を置く）・session の後に読むのは compositor の内部の file の検証で、規則の対象外。
- 各 Phase で 3 OS の build（zedBSD・`make keiland-linux`・FreeBSD の native build）と `check.sh`（WS131 §7.3 と同じ）。全文の規約は各 Phase と p006。

## 7. Phase の分け方（案、ID は Q1 が付ける）

| Phase | 内容 | 依存 | 目安 |
| --- | --- | --- | --- |
| p002 | compositor: store と merge の書き（`settings-store.c`）、key の表（`settings-keys/`）、session の開始の読み・終わりの書き（§4.3.1、worker、SIGHUP）、`kl_system_manager_v1` v1 と `kl_system_settings_v1`、`kl_backend_peer_uid`（3 OS）、壁紙の非同期（§4.4）、音量の順序（§4.5）、repeat の送り直し（§4.6）。**移行の間は今の watcher を残し**、file の変化を store に取り込む（Settings が p004 まで desktop.conf を直接書くため。取り込んだ変化は「この session の差」に数えない: 書き手は Settings の lock と rename で既に file にある）。host の store の試験、3 OS の build | p001、D1、WS131 p006〜p009 の merge か path の予約（`wayland/main.c`・`handoff.c`・Makefile が重なる、Q1） | 5〜6 h |
| p003 | libkeiland: `kl_settings_*`（wire・cache・watch・dispatch・app の file の解決）、probe `keiland-settings`、host の cache と表の試験、3 OS の build。QEMU で probe の get・set・watch（2 つの probe）を T に依頼 | p002 | 4 h |
| p004 | Settings（look・input・sound）を `kl_settings_*` へ。同じ Phase で compositor の watcher・libkeiland の `keiland_preferences_*` と `preferences.c` を除く。試験の書き換え（settings-p004〜p012・pages・ws100 の volume・host-build・host-preferences）、`check.sh` の規則、3 OS の build。QEMU の回帰を T に | p003 | 4〜5 h |
| p005 | D3 で入れる物: Terminal の `terminal.ambiguous-wide`（と Files の open-with）を `kl_settings_*` の app の項目へ | p003、D3 | 2〜3 h |
| p006 | 全文の規約の確認と WS の完了の回帰（T）、3 OS | p004・p005 | 2〜3 h |

## 8. WS131 との分担（Q1 が WS131 の表と design に適用する案）

- p010: 「拡張の protocol `kl_system_manager_v1` と設定の記録（監視は残す）」→「`kl_system_manager_v1` に network・audio・power・devices の request と object を足す
  （manager v1・`capabilities`・settings・`kl_backend_peer_uid` は WS135 p002 で済み）。network の I/O（`save_key`・`get_saved`・`query_details`、
  `wayland/network.c:1164`・`:1283`）の worker（WS131 review 7）は p010 に残す」。依存に WS135 p002。
- p011: 「Settings を拡張へ、毎秒の監視の除去、libkeiland の OS を 0 に」→「Settings の network の操作と sound の feedback を拡張へ、`audio-compat.c`・
  `system-compat.c` を除き libkeiland の OS を 0 に」。監視の除去と preferences の API の除去は WS135 p004 で済み。
- design.md §4.2 の `kl_system_settings_v1` の行・§4.3 の表・§4.4 の `kl_system_settings_*` は WS135 の design への参照にする。§4.2 の「未知の key は書式が
  正しければ保存」は WS135 §2.1（`unsupported`）に改める。p015 の `kl_app_system(app)` は settings を `kl_app_settings(app)` で持つ。
- WS113 p005（displays.conf）: `result` の形（applied・saved）は WS135 と同じ共通の形。displays.conf も「session の終わりにだけ書く」かは WS113 の判断で、
  WS135 の store の merge の書きを使える。

## 9. 判断が要る点（Q1 → ユーザー）

| # | 問い | 選択肢 | 推奨と理由 |
| --- | --- | --- | --- |
| D1 | 拡張の manager を誰が先に作るか | (a) WS135 p002 が `kl_system_manager_v1` v1（get_settings・capabilities）と `kl_backend_peer_uid` を作り、WS131 p010 が足す ／ (b) WS131 p010 を待つ | (a): WS131 は単独走行（N=1）で p010 は p009 の後と遠い。名前・認可・result の形は WS131 の決定（D5・D15・§4.1）に従う |
| D2 | 音量の解決の先 | (a) compositor の項目（compositor が今の backend の audio で audiod に頼む） ／ (b) libkeiland → audiod の直（今の `audio-compat.c`） | (a): ユーザーの「audiodに依頼」と WS131 の確認1 の経路に合い、`audio-compat.c` は backend を libkeiland に build する暫定で WS131 D4 (b) に反し、WS131 p011 で除かれる。起動の時の取り合いも無くなる（§4.5） |
| D3 | app だけの設定（Terminal の `terminal.conf`、Files の `open-with`）| (a) libkeiland が app の file を直接解決（別の process への通知は無し、起動の時に読む） ／ (b) WS135 の外（app の内部の file のまま） ／ (c) compositor の store に入れる（別の process に通知、session の終わりに保存、Keiland の compositor の上だけ） | (a): ユーザーの「libkeilandが直接解決する設定もあれば」の具体例になり、Keiland 以外の compositor でも動く。ただし完了の条件 3（別の process の変更も届く）を app だけの設定では満たさない。条件 3 を優先するなら (c)。desktop-layout・tags・places・IME の辞書は data として対象外にしたい |
| D4 | crash の時の喪失 | 方針どおり session の終わりだけ書く（crash・SIGKILL で session の変更を失う） ／ 例外を設ける | 方針どおり（ユーザーの明示）。記録として示すだけ |
| D5 | 未知の key | `unsupported`（表に無い key を保存しない） ／ 書式が正しければ保存（WS131 §4.2） | `unsupported`: 書き手が compositor だけになり、表が型と範囲の正本になる。手での編集の未知の key は file に残す |
| D6 | system bar の「libkeiland を通る」 | compositor の中の system bar は store を直接使う（compositor は client の API を使えない、WS131 D4 (a)）と条件を読み替える ／ 別の形 | 読み替え: system bar は compositor の一部で、store が設定の正本。通知は同じ store から全ての client へ出る |

## 10. 未確認と限界

- source を読んだだけで、build・試験は流していない。
- zedBSD の compositor の socket で `getpeereid` が動くかは未確認（WS131 §10 と同じ、p002 で確かめる）。
- 壁紙の読みと decode の実際の時間は未計測（p002 で計測し、worker に移す効果を記録する）。
- Linux・FreeBSD で Keiland 以外の compositor の上の Keiland の app は、compositor の項目が `ENOTSUP` になる（今はそこで desktop.conf を書いて誰も読まない
  状態なので退行ではない）。

## 11. design-reviewer の review への対応（2026-10-04、16 項目）

| # | 指摘 | 対応 |
| --- | --- | --- |
| 1 | 終わりの書きの意味が未定で、全体の書き直しは手での編集・2 つ目の compositor・移行中の Settings の変更・読みの失敗の後の行を消す | §4.3 を merge（lock・読み直し・この session の差だけ・rename）に。読みが失敗した session は全体で書かない。host の試験 3 つ（§6） |
| 2 | Log Out の worker が変わり続ける store を読む | event loop で不変の text を作り worker は I/O だけ。Log Out の後の変更は終了でもう一度 merge（§4.3.1） |
| 3 | 私的な queue から既定の queue へ移す時に event を失う（`proxy.c:343-368`） | 専用の queue に置いたまま、`kl_settings_dispatch` で `dispatch_queue_pending`（§3.3） |
| 4 | 壁紙の「読めなければ failed」は今の API で実装できず、event loop で disk を待ち、FIFO で止まる | §4.4: `O_NONBLOCK`・`fstat` の検査、読みと decode を worker、結果で `result`。p002 の範囲に |
| 5 | WS131 の共通の約束・API・worker・`peer_uid`・`capabilities` との食い違い | result を `applied`・`saved` の共通の形に（§4.2）、v1 に `capabilities`、`kl_backend_peer_uid` を p002 に、WS131 §4.4・p010 の worker・p015・WS113 の扱いを §8 に |
| 6 | 完了の条件との食い違い（system bar、直接解決の項目、通知、repeat の即時、Shut Down・Restart・SIGHUP） | D6（system bar）、D2 を compositor の項目に改め直接解決は D3、D3 に条件 3 の欠けを明記、§4.6 repeat の送り直し、§4.3.1 の経路の表と SIGHUP |
| 7 | 棚卸しの取りこぼし（Files・IME の file、Settings の頁、「既定の値なら unset」） | §1.4 の設定と data の表、§1.1 に頁の file、§5 で常に `set` と `reset` |
| 8 | 3 OS の build が最後、WS131 の作業中の Phase と path が重なる、ws.md の依存が D1 と逆 | 各 Phase に 3 OS の build と `check.sh`、p002 の依存に WS131 p006〜p009 の merge か予約。ws.md の依存は D1 の後に Q1 が直す |
| 9 | 試験が誤りを検出できない（存在しない log、`volume-bug153.sh:114` の書き、host-build・host-preferences、経路の試験、静的な検査） | §6 を改訂: code の不在と `check.sh` の規則、`volume-bug153.sh` の目的の書き換え、host-build の修正、SIGTERM・Linux の Log Out・merge・手での編集・cache の層の試験。log の改名はしない |
| 10 | Sound の頁は `kl_settings` だけで移せない（reachable・device、二重の link） | `sound.available`（読むだけ）、feedback は今の link だけ（§2.2・§5） |
| 11 | callback の約束の不足 | §3.4 の合流・再入・喪失の行 |
| 12 | timeout が audiod の再接続を含まない | D2 (a) で libkeiland は settings のために audiod の link を持たないので `kl_settings_fd`・`timeout` を除いた |
| 13 | key の表の置き場 | 中立の dir `userland/desktop/settings-keys/`、exports.map は header から（§3.5・§3.1） |
| 14 | 範囲の外の既定の値 | 丸めず `flags=default`（§2.1） |
| 15 | 引用の誤り | `:45-56`、`KEILAND_VERSION` の書き方を直した |
| 16 | 起動の時の音量の取り合い | §4.5（復元の前の `set sound.*` は `busy`） |

## 承認（2026-10-04）

user「WS135の設計D1-D6を承認します。」D3 は (a)。
