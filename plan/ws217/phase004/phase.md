<!-- awesome-plan project=zedbsd record=ws217-p004 -->
# ws217-p004: protocol・libkeiland・Settings の回転

Status: cleared（2026-10-14 Q1: T1-611 で R1〜R6 PASS、T1-613・T1-614・T1-616 で窓が消えない・位置が戻る・選んだ絵が読める・回転 約 2 s・回転の後の崩れた絵が無い（late=0）。ms= の 2.1〜2.3 s は T1 の QEMU と並行の負荷の下で、目標 2 s を少し超えた値として記録。-4 は BUG-306 で tracking。実機は p007）
Disposition: normal
Parent: [WS217](../ws.md)
Queue: q983
依存: p002b・p003（cleared）。p010（scanout の 180°）は待たない: 今の compositor は 4 つの角度を turn pass で回せるので、Settings はそれを使う（p010 が入れば compositor の中で scanout に切り替わるだけ）

## 由来（2026-10-14 ユーザー）

Q1「画面の回転は、ベータ2 では Settings に操作が無く、displays.conf に rotate= の行を手で書く時だけ回ります。release notes で宣伝しますか？」→ ユーザー「ベータ2でSettingsに追加します。」

## 範囲

- [ws.md](../ws.md) の p004 の行と [p001](../phase001/phase.md) の設計（protocol の version 34 の wire、`rotation` は選択の値、`why` の 4 bit、KL_VERSION の次の API）に従う。ただし今日の範囲は Settings で回せるのに要る物に絞る: compositor の `set_rotation`・`rotation`・`rotations`（mask と why）、libkeiland の API、Settings の Display の頁（`page-display.c`）の回転の選択（0°・90°・180°・270°、選べない角度は理由の文）、保存（displays.conf、今の ROTATE と同じ）。`bind_touch`・`listen_touch` は UNSUPPORTED のまま。KL_VERSION は master.md の「次の KL_VERSION」の値を使い、Q1 に知らせる。
- 回転の後の窓の配置・touch・pen は p002b・p003 の経路のまま。
- `plan/coding-style.md` の全文を適用。

## 確かめ

- host 試験（protocol の encode/decode、Settings の頁の選択と理由の文）、amd64 の build warning 0、Linux の Keiland の build（Settings は Linux でも作る）warning 0。
- T1 への依頼の文: QEMU で Settings → Display の回転を 90° にして画面が回る PNG、0° に戻す PNG、保存されて再起動の後も回っていること。

## 実装（2026-10-14 P3、q983、base main `736eda0be`）

番号: `KL_SYSTEM_SINCE_ROTATION` = `KL_SYSTEM_MANAGER_VERSION` = 34、`KL_VERSION` 93（master.md の「次の KL_VERSION」。全 agent branch で 92・33 が最大を確かめた）。Q1 に知らせた。

| 所 | 変えた事 |
| --- | --- |
| `libkeiland/system/kl-system-protocol.h` | displays v34 の wire の全部: request 4 `set_rotation(uint, string, uint)`・5 `bind_touch(uint, string, string)`・6 `listen_touch(uint, string)`、event 3 `rotation(string, uint)`・4 `rotations(string, uint, uint)`・5 `touch(string, string, uint, string)`。`KL_SYSTEM_ROTATION_*`・`KL_SYSTEM_ROTATE_WHY_*`（SCANOUT 1・TURN 2・GAME 4・RESOURCE 8・**ALONE 16**）・`KL_SYSTEM_TOUCH_IDENTITY_MAX 96`。`rotation` は選択の値（p001 §7.1） |
| `libkeiland/system/system-protocol.c` | displays の interface の版 34、requests 7・events 6 |
| `wayland/displays-shell.c` | `set_rotation`: active でなければ DENIED、key が繋がっていなければ UNAVAILABLE、anchor なら `kwl_output_select_rotation`（0→OK、EINVAL→INVALID、ENOTSUP→UNSUPPORTED、他→FAILED、`saved`）、anchor 以外は UNSUPPORTED。`bind_touch`・`listen_touch` は引数を検査して UNSUPPORTED（p006）。snapshot の各 `output` の後に（版 34 の object にだけ）`rotation`・`rotations`。log `KWL DISPLAYS rotation client=… key=… rotation=… error=… applied=… saved=…` |
| `wayland/output-rotate.c` | `kwl_output_rotation_report()`（anchor は `rotation_selected`・`rotations`・`rotate_why`、head と並ぶ間は選択の角度だけと ALONE。anchor 以外は 0 と ALONE）。選択・保存・policy の変化の後に `kwl_displays_tell`（Settings が新しい角度と大きさを受ける） |
| `wayland/rotate.c`・`rotate.h` | **既定の policy の変更**（下の「設計からの差」1）: `turn_allowed = turn_default && turn_build`（hw180 を条件にしない）。`KWL_ROTATE_WHY_ALONE 16`、`kwl_rotate_report()`（純粋、host 試験） |
| `include/keiland/keiland.h`・`libkeiland/exports.map`・`system.c`・`system-view.c`・`system-private.h` | KL_VERSION 93: `KL_DISPLAY_ROTATE_0/90/180/270`、`KL_DISPLAY_ROTATION_*`、`KL_DISPLAY_ROTATE_WHY_*`（ALONE を含む）、`kl_system_displays_rotation()`・`_rotations()`（0・ENODEV・ENOTSUP）、`kl_system_displays_set_rotation()`（result は 0・ENOTSUP・ENODEV・EPERM・EINVAL・EIO）。`struct kl_display` は不変（ABI）、角度は view の別の配列（`struct system_view_rotation`、display と同じ index）。`touch` event の受け手は何もしない |
| `settings/rotate.c`・`rotate.h`（新、純粋） | 絵の順（0・90・180・270）、押せるか（mask にあり選択と違う）、理由の選び方（SCANOUT > ALONE > RESOURCE > GAME > TURN） |
| `settings/page-display.c`・`settings.h`・`pages.c`・Makefile 3 つ | Display の頁に「Rotation」の card（Displays の card と Brightness の間）: 表示中で角度を告げられた display ごとに 1 行（label、理由の 1 行、右に 4 つの絵）。絵は panel の輪郭（横置き・縦置き）と panel の上辺の印（0 は上、90 は左、180 は下、270 は右）と度数。選択中は accent、選べない角度は薄く押せない。押すとすぐ `kl_system_displays_set_rotation`、結果の失敗だけを文で出す（UNSUPPORTED「This display cannot be rotated that way.」など）。検索語に rotation・rotate・portrait・landscape・orientation |
| `locale/ja/settings.tr` | 新しい 10 の文の日本語（`tools/i18n/tr.py update`・`check` 0 problems） |
| `userland/tests/keiland-system/main.c` | `display-rotate KEY ANGLE`、`displays` に `KEILAND-SYSTEM rotation key=… rotation=… mask=… why=…` の行 |
| `plan/ws217/tests/config-amd64.mk` | 試験の image に keiland-system |

### 設計からの差（Q1 に報告済み、判断を仰ぐ）

1. **既定の policy で hw180 を要らなくした**。p001 §4.10 と D14 は「hardware が 180 を回せない display は回転を一切出さない」だが、p010 の前は全部の display で hw180 が 0 なので、このままだと実機（5330・5320）でも Settings の回転は全部無効表示になる。この phase の「p010 を待たない、turn pass で 4 つの角度」に合わせ、turn が既定で出せる GPU（Pi 4 = vendor 0x14e4 以外）では 0・90・180・270 を全部出す（180 は hw180 が無ければ turn）。結果として **Venus（QEMU）と Linux・FreeBSD の build でも回転が出る**（D14 からの逸脱。Venus での無効表示はユーザーの「できなくていい」の範囲で、出しても害は無い、と P3 は判断）。p010 が入れば hw180 のある display の 180 は scanout に切り替わる。戻すなら `rotate.c` の `kwl_rotate_mask` の 1 行。
2. **why に `ALONE 16` を足した**（wire は v34 で凍るので p004 で入れる）。head の回転（p005）が無い間、head と並ぶ（extended・mirror）anchor は選択の角度だけを出し、head と表示していない display は 0 だけを出す。Settings は「The rotation can be changed while this display is the only one shown.」。p005 の後は head 自身の mask を出す。
3. Settings の回転は display の row の中ではなく別の card にした（Displays の card の高さの計算と Apply の draft から離すため）。shown でない display は行を出さない（p001 §7.3 は mirror の row にも出す、とする。head の回転の p005 で見直す）。
4. `kl_system_touches_*`（`struct kl_touch_device` 等の公開 API）はまだ入れていない（wire の request・event は入れた）。p006 で API を足す時に KL_VERSION を上げる。

### 確かめ（host・build、P3）

- `sh plan/ws217/tests/host-rotate.sh` → PASS（plain、ASan/UBSan）。既定の policy の表を新しい規則に直し、`test_report`（ALONE）を足した。
- `sh plan/ws217/tests/host-rotation-settings.sh`（新）→ PASS（ASan/UBSan）: displays の table（版 34、requests 7・events 6、opcode と signature、前の request・event の位置）、wire・libkeiland・compositor の bit の一致と KL_VERSION 93、libkeiland の view（done の前は無い、done で反映、知らない key・告げられない display・snapshot の外の event は ENODEV）、Settings の絵の順・押せるか・理由の順。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p004/amd64 build/ws217-p004/amd64/bin/wayland build/ws217-p004/amd64/bin/settings`・`… bin/keiland-system` → exit 0、warning 0（libkeiland を含む）。
- `make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p004/linux keiland-linux` → exit 0、warning 0（Settings を含む、install はしていない）。
- `python3 tools/i18n/tr.py check userland/desktop/locale/ja/settings.tr` → 0 problems。`git diff --check` → 無し。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p004/arm64 …/bin/wayland …/bin/settings` → exit 0、warning 0。
- `sh plan/tools/keiland-os-boundary/check.sh` → PASS。
- 未実施: FreeBSD の build、QEMU・実機（T1 へ）、aggregate の `make check`（禁止）。

### T1 への依頼の文（Q1 が台帳へ）

> ws217-p004（Settings の Display の回転）の QEMU の確かめ。image: 依頼の tree で `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh BUILD`（AAT の image に wltest・tablet-probe・keiland-system。`--file` の複写は無い）。session は AAT の普通の起動（`--rotate-policy` も `POLICY any` も**使わない**: 既定の policy で Venus の display が 4 つの角度を出すことも確かめる）。
> - R1. `keiland-system displays`（key は空白を含むので `display-rotate` では引用する: `keiland-system display-rotate "Venus virtual display 0" 90`） → `KEILAND-SYSTEM rotation key=<Venus の key> rotation=0 mask=15 why=0`。
> - R2. `settings display` を開き撮影（keiland-shot）: Display の頁に「Rotation」の card、display の行に 4 つの絵（0° が accent、他は押せる色）。
> - R3. 90° の絵を `aat click X Y`（R2 の PNG で位置を読む）→ log に `KWL DISPLAYS rotation … rotation=90 error=0 applied=1 saved=0` と `KWL OUTPUT rotate key=… rotation=90 … steps=turn|rotated|resized|save by=user`。撮影: 画面が時計回りに 90° 回り（system bar が物理の右端に縦）、Settings の 90° の絵が accent。`keiland-system displays` → `rotation=90 mask=15` と display の `width=800 height=1280`（1280x800 の場合）。`$HOME/.config/keiland/displays.conf` に `rotate=<key> 90`。
> - R4. 0° の絵を押す（R3 の PNG で位置を読む）→ 回転が戻る PNG、`rotation=0`、displays.conf の `rotate=` の行が消える。
> - R5. もう一度 90° にして logout → login（または再起動）→ 縦のまま始まる PNG（log `KWL OUTPUT open width=800 height=1280`）、Settings で 90° が accent。最後に 0° に戻す。
> - R6（libkeiland の経路）: `keiland-system display-rotate <key> 270` → `KEILAND-SYSTEM result … error=0`、撮影（反時計回りに 90°、bar が物理の左端）。`display-rotate <key> 45` は呼び出しが EINVAL（request を送らない）。`display-rotate nosuch 90` → `error=ENODEV`。最後に `display-rotate <key> 0`。
> 合否: R1〜R6 の文字列と PNG。PNG は結果に添える。

## 残件

- T1 の R1〜R6（QEMU）。実機（5330 の i915 で turn pass の 90°・270°・180°）は p007（ユーザー／T1）。p002b・p003 も QEMU だけで、i915 での turn pass は未確かめ。
- 上の「設計からの差」1・2 の Q1 の判断（1 は D14 の逸脱、ユーザーへの報告が要るかもしれない）。
- head の回転（p005）、`bind_touch`・`listen_touch`・`touch` の中身と libkeiland の `kl_system_touches_*`（p006）、game の session（p012）。
- FreeBSD の build は未実施（Settings の rotate.c は Makefile.freebsd にも足した）。

## 回転の遅さと回転の直後の古い絵（2026-10-14 P3、T1-613 の解析と修正）

由来: [BUG-306](../../bugs/BUG-306.md) の T1-613 の節。Settings で回すと `KWL OUTPUT rotate … ms=` が 9830・8795・7285（大きさが変わる回転）、100（90°→270°）、2386。回転の約 3 s 後の最初の撮影は窓が崩れた位置。QEMU は起動していない（解析は T1 の `t1/build/review/t1-613/session.log`・PNG と source から）。

### 時間の内訳（T1-613 の log と source）

`ms=` は `kwl_output_apply_rotation` の全体。turn（`output_rotate_turn`）の分は `rotate times` の prepare・park・idle・swap の和で、残りは turn の後の pointer・fit・`kwl_outputs_changed`・`kwl_input_geometry`・`kwl_heads_config_save`。

| 回 | 回転 | ms= | turn（prepare+park+idle+swap） | turn の後 |
| --- | --- | --- | --- | --- |
| 1 | 0→90 | 9830 | 1106+0+40+1133 = 2279 | 7551 |
| 2 | 90→0 | 8795 | 881+0+41+1243 = 2165 | 6630 |
| 3 | 0→90 | 7285 | 955+0+40+1132 = 2127 | 5158 |
| 4 | 90→270（同じ大きさ） | 100 | 0+25+57+0 = 82 | 18 |
| 5 | 270→0 | 2386 | 1071+0+40+1243 = 2354 | 32 |

1. **turn の後の 5〜7.5 s は displays.conf の fsync**（source から特定、直接の計測は次の T1 で）。turn の後の段のうち I/O や他の thread を待つのは `kwl_heads_config_save` → `heads_save` の `fsync` だけ（pointer・fit・outputs_changed・input_geometry はメモリの上の操作と送信の queue）。zedBSD の UFS の fsync（`src/drivers/fs/ufs.c` の `ufs_file_sync`）は file の inode を書いた後に `ufs_sync(mount)` で**volume 全体**の遅延書き込み（最大 32 MB）と journal を書き出し、disk に flush を求める。QEMU（`-drive` の既定 cache=writeback）では guest の flush が host の image の fdatasync になり、host の負荷（他の担当の build）で秒単位に揺れる。32 ms〜7.5 s の揺れはこれと合う。回転の間 main thread が止まるので、frame も client も止まる（BUG-306 の -4 の候補の 12 s とも同じ経路）。
2. **prepare の約 0.9〜1.1 s のうち約 0.9 s は壁紙の作り直し**（`KWL GLASS size … by=turn ms=881〜1071`）。起動時の同じ仕事（2 つの host image と描画）は 361 ms（`KWL STARTUP step=wallpaper`）。違いは layout の移動: 起動時は batch で 1 回の submit と待ちだが、回転では 2 つの image がそれぞれ 2 回ずつ `kwl_render_wait_idle`（描画中の frame の終わりを待つ）・submit・`vkQueueWaitIdle` を単独で行っていた（Venus では 1 回ごとに往復）。壁紙の decode は無い（壁紙の path は空で、風景を描く。起動時 74 ms）。残りの約 0.05〜0.2 s は turn image（初回は turn の pipeline も作る）。
3. **swap の 1.1〜1.2 s** は device を止めた間の Vulkan の object の作り直しと破棄（古い turn image と壁紙の解放、scene の buffer、layer の 3 つの image と pass）。Venus では作成・解放の 1 回ごとに host と往復する。`vkDeviceWaitIdle` は 40 ms、park は 0〜25 ms で小さい。

### 回転の直後の古い絵

回転 1 の後の撮影（c1.png、回転の終わりの約 1.5 s 後）では、Settings の新しい buffer（776x800、`KWL IMPORT client=2 buffer=83`）はまだ届いておらず、1180x680 の古い buffer と古い glass の panel が縦の画面の x=12 に描かれて右が切れていた（c1b.png の時点では正常）。compositor は回転の直後の最初の frame から新しい配置で描くので、client が新しい大きさを描くまでの間（Venus では import も遅く 1〜2 s）、窓が崩れて見える。

### 直したこと（agent/p3 777da859b・e8762528e・e9d61dada）

| 所 | 変えた事 |
| --- | --- |
| `wayland/heads.c` | displays.conf を書く thread（`struct heads_writer`）。回転の選択の保存（`kwl_heads_config_save`）は main thread で text と path を作って渡すだけにし、thread が mkstemp・write・fsync・rename を行う（渡された最新の 1 つを書く。書いている間に来た物はその後に書く）。log `KWL DISPLAYS saved by=writer errno= wait_ms= ms=`（ms= が fsync を含む書き込みの時間）。他の保存（displays の mode、display の on/off、移動の失敗）は今までどおり同期で、先に thread の書き込みの終わりを待つ（file が最新の選択で終わる）。compositor の終わり（`kwl_compose_close`）で書き残しを待って thread を閉じる（`kwl_heads_config_close`）。thread を作れなければ今までどおりその場で書く |
| `wayland/glass.c`・`shm.c` | 回転の壁紙（`kwl_glass_size_prepare`）の 2 つの image の layout の移動を起動時と同じ batch で 1 回の submit と待ちにした（`glass_size_make` に分けた）。`kwl_host_image_batch_end` は render thread が動いている時は submit の前に idle を待つ |
| `wayland/output-rotate.c` | cursor の plane（同じ queue に submit する）を turn の最初に止める（今までは park の時。batch の submit の前に止める）。`KWL OUTPUT rotate times` を回転の最後に 1 行で出し、段を細かくした: `prepare_ms turn_ms glass_ms park_ms idle_ms swap_ms caches_ms fit_ms tell_ms save_ms`（prepare・park・idle・swap は今までと同じ意味） |
| `wayland/output-rotate.c`・`display.c`・`kwl.h`・`rotate.h` | **回転の後の古い frame の保持**: 大きさが変わる回転で、anchor の今の desktop の窓のうち新しい大きさを告げられる物（fullscreen・docked、xdg-shell 4 の浮いた窓は新しい bounds）を、回転の前に送った最後の configure の serial で覚え、それより新しい configure を ack した後の commit が来るまで（最長 `KWL_ROTATE_HOLD_MS` = 1000 ms）新しい frame を描かない（画面は回転の前の frame のまま）。待つ間も全部の surface の frame callback はすぐ返す（client の Vulkan の present は前の callback を待つので）。log `KWL OUTPUT rotate hold windows=N`・`KWL OUTPUT rotate drawn surface= client= after_ms=`・`KWL OUTPUT rotate shown held_ms= windows= late=`（late は時間切れまで描かなかった窓の数）。desktop の icon の surface（ZFILES）は待たない |

見込み（未計測）: 大きさが変わる回転の `ms=` は Venus で turn の分の約 1.7〜1.9 s（prepare が起動時の壁紙と同じ約 0.4〜0.5 s、swap は変わらず約 1.2 s）、fsync の分は 0。2 s を超えるなら残りは swap の Vulkan の object の作り直し（Venus の往復）で、次の手は回転の前の大きさの image（壁紙・layer・scene・turn）を 1 組残して戻る回転で使い回すこと（memory が出力 1 枚分の数倍増える）。

### 確かめ（host・build、P3）

- `sh plan/ws217/tests/host-rotate.sh` → PASS（plain、ASan/UBSan）。新しい `test_hold`: ack の serial が回転の前の serial より新しいか（wrap を含む）、保持の終わり（窓が 0、1000 ms の直前・ちょうど・後）。
- `sh plan/ws217/tests/host-rotation-settings.sh` → PASS。`sh plan/ws113/tests/host-output-switch.sh` → PASS（kwl.h の変更）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p004r/amd64 build/ws217-p004r/amd64/bin/wayland` → exit 0、warning 0。`make … ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p004r/arm64 …/bin/wayland` → exit 0、warning 0。`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p004r/linux keiland-linux` → exit 0、warning 0。`git diff --check` → 無し。
- 未実施: QEMU（T1 へ）、FreeBSD の build、実機。displays.conf の writer の thread の host 試験は無い（heads.c は依存が多く単独で build できない。仕組みは review で確かめた）。

### T1 への再試験の依頼の文（Q1 が台帳へ）

> ws217-p004 / BUG-306（回転の時間と回転の直後の絵）の QEMU Venus の確かめ。image: agent/p3 e9d61dada（main への merge の後）の tree で `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh BUILD`。session は AAT の普通の起動（policy の指定なし）。T1-613 と同じく login して `settings display` を開き、Rotation の絵を **90° → 0° → 90° → 270° → 0°** と押す（位置は直前の PNG で読む）。
> - 各回: 押した **1 s 後と 3 s 後**に PNG を撮る（`r<n>-1s.png`・`r<n>-3s.png`）。`aat windows` で Settings が mapped True。
> - 3 回目（90°）の 3 s 後に `cat $HOME/.config/keiland/displays.conf` → `rotate=<key> 90` の行がある。最後（0°）の 3 s 後に同じく → `rotate=` の行が無い。
> - 最後に **logout の前に** session.log を host へ写し、次の行を結果に貼る: `KWL OUTPUT rotate`（`times`・`hold`・`drawn`・`shown` と `ms=` の行の全部）、`KWL DISPLAYS saved`、`KWL DISPLAYS rotation`、`KWL GLASS size`、`KWL OUTPUT window`、`KEILAND PRESENT_FAILED`、`ZSETTINGS FAILED`、`KWL FAILED`、`KWL SLOW_PASS` のうち us= が 1000000 を超える物。
> - 合否: (1) 大きさが変わる 4 回の `ms=` が各 2000 以下（超えたら FAIL ではなく値と `times` の行を記録する）、`times` の `save_ms` が 50 以下。(2) 各回の後に `KWL DISPLAYS saved by=writer errno=0`。(3) 1 s・3 s の PNG で、窓が切れたり古い大きさのまま新しい向きに置かれたりしていない（回転の前の絵のまま、または新しい向きで Settings が新しい配置で描かれている）。(4) `KWL OUTPUT rotate shown … late=0`（late=1 で held_ms=1000 なら記録して PASS）。(5) Settings が 5 回とも残り、FAILED の行が 0。(6) displays.conf の行が上のとおり。

## 回転の後の窓の描き直しの遅れ（2026-10-14 P3 新しい世代、T1-614 の解析と修正）

由来: T1-614（QEMU Venus、main `f66e92abe`、成果物 `t1/build/review/t1-614/`）。回転の `ms=` は 1825〜1938 で目標どおり、displays.conf も正しい（3 回目の後 `rotate=Venus virtual display 0 90`、最後は行なし）。しかし大きさが変わる 4 回のうち 3 回は `shown held_ms=1008〜1055 late=1`、回転の約 3 s 後の PNG で古い 1180x680 の Settings が縦の画面の x=12 に出て右が切れ、約 8 s 後に正常。QEMU は起動していない（解析は T1 の session.log・PNG と source から）。

### 時間の内訳（T1-614 の session.log、回転 1 の 0°→90°）

| 時刻（log の順） | 事 |
| --- | --- |
| 〜53.8 s | Settings の `set_rotation` の request を compositor の main thread が処理し始める |
| 55.71 s | 回転の終わり（`SLOW_PASS us=1942000 clients_us=1940216`）。この時点でまだ窓へ何も送っていない |
| 次の pass | `KWL BOUNDS changed 776x1160`、`KWL CONFIGURE client=4 serial=20`（新しい bounds が窓へ出るのは回転の**後**の pass の始め） |
| 〜56.0 s | Settings は configure より先に届いた回転の結果で、**古い大きさ**の frame を 1 枚描く（`SLOW-FRAME present=190`） |
| 56.0〜57.28 s | Settings が configure を読み、swapchain と canvas を作り直す（約 1.2 s。Venus の往復）。57.28 s に新しい大きさの `LAYOUT` |
| 56.80 s | 保持が 1000 ms で時間切れ（`shown held_ms=1010 late=1`）。古い 1180x680 の画像を新しい配置（x=12）で描く → T1 の「崩れた絵」 |
| 57.32〜57.93 s | compositor が新しい 3 枚の buffer を 1 pass に 1 枚ずつ import（各 80〜200 ms、`vk_outside_us`）、57.5 s ごろ新しい大きさが出る |

回転 2・3 も同じ形（configure が回転の後、Settings の作り直し約 1.2〜1.5 s、import 3 枚）。回転 5 の `drawn after_ms=186 late=0` は**誤り**だった: Settings が serial 41 を ack した後に commit したのは古い 776x800 の frame（`LAYOUT` の x=542 は 776 の幅の配置）で、保持はそれを「新しい大きさを描いた」と見て 186 ms で終わった。新しい 1180x680 は約 1.6 s 後。libkeiland は configure を受けた時にすぐ ack するので、ack の後に先に描いた古い frame が commit されうる。

**30 s ごとの `clients_us` 約 1.9 s**（T1 の `slow-pass.txt`）は別の問題ではない: at_ms の 55712・85751・118942・149392 は大きさが変わる 4 回の回転（`OUTPUT rotate key=` の行の直後）で、client 4（Settings）の `set_rotation` の request の処理そのもの（回転は main thread で同期に行う）。30 s の間隔は T1 の操作の間隔。大きさが変わらない 90°→270° は 56 ms で SLOW_PASS にならない。17615 の 1.34 s（`schedule_us`）は起動時の 1 回。

まとめ: 窓の遅れは (1) 新しい大きさを窓へ送るのが回転の終わった後で、Settings の作り直し（約 1.2 s）と import（約 0.3〜0.6 s）が回転の後に直列に来ること、(2) その間に古い大きさの frame を 1 枚無駄に描くこと。frame の throttle（KL_PACING_FRAME）は保持の間 frame callback をすぐ返すので待ちの原因ではない。page の layout は 20〜26 ms（`SLOW-FRAME draw=`）で小さい。

### 直したこと（agent/p3 02c02dbf4）

| 所 | 変えた事 |
| --- | --- |
| `wayland/output-rotate.c` | 回転を 2 つに分けた: `output_rotate_turn_begin`（新しい turn image と壁紙を作り、render thread を park、device を idle、新しい角度と論理の大きさを設定）と `output_rotate_turn_end`（古い image の破棄と cache の作り直し約 1.1〜1.2 s、unpark）。その間（park のまま）に pointer・窓の fit・`kwl_outputs_changed`・**`kwl_window_bounds_refresh`**（浮いた窓の bounds と configure を次の pass でなく今）・`kwl_input_geometry` を行い、`output_rotate_flush` で全 client に**すぐ送る**。窓は compositor の作り直しと並んで新しい大きさの swapchain を作れる。保持の 1000 ms は回転の終わり（unpark の後）から数え直す。`times` の行の段の順を変え `told_at_ms=`（窓へ送った時刻、monotonic の ms）を足した |
| `wayland/output-rotate.c`・`rotate.h`・`kwl.h` | 保持の終わりの判定: 回転の時の画像の大きさ（`rotate_width`・`rotate_height`）と、大きさを変えねばならないか（`rotate_needed`: fullscreen・docked は常に、浮いた窓は新しい bounds に入らない時、`kwl_rotate_size_needed`）を覚え、変えねばならない窓は ack の後でも**大きさが変わった画像**が来るまで待つ（`kwl_rotate_drawn`）。回転 5 の誤りの対策。log `KWL OUTPUT rotate hold windows= bounds=WxH`、`KWL OUTPUT rotate drawn … width= height= was=WxH after_ms=` |
| `wayland/protocol.c`・`kwl.h` | static の `window_bounds` を `kwl_window_bounds` として公開（保持が新しい bounds を知るため、中身は同じ） |
| `settings/main.c` | 新しい大きさの最初の frame で 1 行: `ZSETTINGS RESIZE width= height= swapchain_ms= shown_ms= at_ms=`（at_ms は configure を受けて作り直しを始めた時刻、compositor の `told_at_ms` と同じ時計）。次の T1 で窓の側の時間を直接測るため |

見込み（未計測）: 窓の作り直しは compositor の `swap_ms`（約 1.1〜1.2 s）と並ぶので、回転の終わりには新しい buffer の create と commit が届いている見込み。回転の後は import（約 0.3〜0.6 s）だけで `drawn after_ms` は 1000 未満、`late=0` の見込み。Venus の host で 2 つの process の Vulkan の往復が直列になるなら、窓の作り直しが swap より遅れ、その分だけ after_ms が伸びる（RESIZE の `at_ms`・`swapchain_ms` と `told_at_ms` で分かる）。

### 確かめ（host・build、P3）

- `sh plan/ws217/tests/host-rotate.sh` → PASS（plain、ASan/UBSan）。`test_hold` に `kwl_rotate_size_needed`（T1-614 の 1180x680→bounds 776x1160、776x800→1256x680、入る窓、bounds ちょうど、space 0）と `kwl_rotate_drawn`（新しい大きさ、ack の後の古い大きさ＝回転 5、高さだけ変化、ack の前、入る窓、serial の wrap）を足した。
- `sh plan/ws217/tests/host-rotation-settings.sh` → PASS。`sh plan/ws113/tests/host-output-switch.sh` → PASS（kwl.h の変更）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws217-p004r/amd64 …/bin/wayland …/bin/settings` → exit 0、warning 0。`ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws217-p004r/arm64` の同じ 2 つ → exit 0、warning 0。`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws217-p004r/linux keiland-linux` → exit 0、warning 0。`git diff --check` → 無し。
- 未実施: QEMU（T1 へ）、FreeBSD の build、実機。turn の begin/end の分割と早い flush の host 試験は無い（compose と Vulkan に依存。順序は review で確かめた: begin の後 render thread は park のままで、fit・tell は窓の配置と protocol の送信だけで turn・壁紙・cache の image を使わない）。

### T1 への再試験の依頼の文（Q1 が台帳へ）

> ws217-p004 / BUG-306（回転の後の窓の描き直しの遅れ）の QEMU Venus の確かめ。image: agent/p3 02c02dbf4 以降（main への merge の後）の tree で `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh BUILD`。session は AAT の普通の起動。T1-614 と同じく login して `settings display` を開き、Rotation の絵を **90° → 0° → 90° → 270° → 0°** と押す（位置は直前の PNG で読む、各回の間は 30 s でよい）。
> - 各回: 押した **1 s 後・2 s 後・3 s 後**に PNG を撮る（`r<n>-1s.png`・`r<n>-2s.png`・`r<n>-3s.png`）。`aat windows` で Settings が mapped True。
> - 3 回目（90°）の 3 s 後に `cat $HOME/.config/keiland/displays.conf` → `rotate=<key> 90` の行がある。最後（0°）の 3 s 後に同じく → `rotate=` の行が無い。
> - 最後に **logout の前に** session.log を host へ写し、次の行を結果に貼る: `KWL OUTPUT rotate`（`times`・`hold`・`drawn`・`shown` と `ms=` の行の全部）、`KWL BOUNDS`、`KWL CONFIGURE client=<Settings>`、`ZSETTINGS RESIZE`、`ZSETTINGS SLOW-FRAME`、`KWL IMPORT client=<Settings>`、`KWL DISPLAYS saved`、`KWL OUTPUT window`、`KEILAND PRESENT_FAILED`、`ZSETTINGS FAILED`、`KWL FAILED`、`KWL SLOW_PASS` のうち us= が 1000000 を超える物。
> - 合否: (1) 大きさが変わる 4 回の `KWL OUTPUT rotate shown` が `late=0` で `held_ms` が 1000 未満（届かなければ FAIL ではなく値と、同じ回の `times` の `told_at_ms`・`swap_ms`、`ZSETTINGS RESIZE` の `at_ms`・`swapchain_ms`・`shown_ms` を記録する）。(2) 各回の `drawn` の行の `width`x`height` が `was=` と違う（0°→90° は 776 幅、90°→0° は 1180 幅）。(3) 1 s・2 s・3 s の PNG で、窓が切れたり古い大きさのまま新しい向きに置かれたりしていない（回転の前の絵のまま、または新しい向きで Settings が新しい配置で描かれている）。(4) `ms=` が各 2000 以下（超えたら値を記録）。(5) Settings が 5 回とも残り、FAILED の行が 0。(6) displays.conf の行が上のとおり、`KWL DISPLAYS saved by=writer errno=0` が各回。

