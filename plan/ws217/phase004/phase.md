<!-- awesome-plan project=zedbsd record=ws217-p004 -->
# ws217-p004: protocol・libkeiland・Settings の回転

Status: in-progress（2026-10-14 P3: 実装 agent/p3、host 試験と build は済み、T1 の QEMU 待ち。clear は Q1 が T1 の結果で判定）
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
- 未実施: rpi4 の build、FreeBSD の build、QEMU・実機（T1 へ）、aggregate の `make check`（禁止）。

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
- rpi4・FreeBSD の build は未実施（Settings の rotate.c は Makefile.freebsd にも足した）。
