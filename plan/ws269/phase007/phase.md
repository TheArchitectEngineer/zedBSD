<!-- awesome-plan project=zedbsd record=ws269-p007 -->

# ws269-p007 display の node（boot framebuffer の `drv_gpu_display_ops`）と `scanout` の import

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §3.5 の「表示」・§6 の p007 と §6 の末尾の必須項目（「display の node は powervr と同じ driver に置き、import は private な lookup。import の部分は p006 の後」）
- Queue: q1333 / q1333-i01（P1、2026-10-19、前半: claim・present まで）
- Status: in-progress（2026-10-19 P1: 前半。2026-10-19 P2（q1340）: 後半の `scanout` の import の実装・build・host 試験まで。残り: 下の実機の確認（未実施、未依頼）。clearance は Q1 の判定）
- 前提: WS256 p011（framebuffer、実機の目視は未）、p004（main に merge 済み 41dd20993）。import の部分は p006。

## 目的と受け入れの条件

1. display の node を `src/drivers/gpu/powervr/` の中に置き、VF2 の boot framebuffer（WS256 p011、`riscv64.framebuffer`）を `drv_gpu_display_ops`（query・mode・claim・release・present・wait）で出す。
2. claim で kernel の text console を止め（`kern_text_suspend`）、release（と、release しないで close・exit した時）で console に戻す（`kern_text_resume` が grid から描き直す）。
3. 自分の resource（node の中で確保した buffer）での claim・present。
4. 順は gpu0 = display（早い段）、gpu1 = render（遅い段、p005 以降）。display の node は GPU の core と独立（core の attach が失敗しても残る）。
5. `scanout` の import は口だけ用意し、render の buffer の private な lookup ができるまで ENOTSUP で断る（後半）。
6. HAL の API は変えない。build（VF2・riscv64-virt・amd64）warning 0、host 試験。QEMU は起動しない。

## 実装（commit 8715e954c）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/display.c`（新規） | node。capabilities は RESOURCE・TRANSFER・MAPPING・DISPLAY。resource は `kern_pmem_alloc` の普通の cacheable な RAM（page 単位、0 で埋める、session ごとに 16 個・1 個 64 MiB まで）、`resource_map` は attributes 0（cacheable）。出力は 1 つ（id 1、generation 1）、plane 1 つ、BGRA・RGBA、mode は loader の大きさ 1 つ（60 Hz と報告。loader は refresh を渡さない）、validate は収まる大きさだけ。claim は lease 1 つ（EBUSY）→ `kern_text_suspend` → 見える範囲を 0 で消す。present は CPU の copy（左上で clip、screen の順と frame の順が違えば R と B を入れ替える）で、返った時に画面に出ている（wait は待たずに最新の sequence、0 枚なら EAGAIN、他の open の lease は EBUSY）。BLOB の present は ENOTSUP。framebuffer は direct map で書いて `kern_dcache_clean_range`（WS256 p011 の描き方 (β) と同じ）、direct map に無い時は `kern_device_map` の uncached の view。`scanout`: query_device は role DISPLAY・companion 0、constraints は COPY だけ（SHARED・FOREIGN は import ができてから）、import_image は ENOTSUP |
| `src/drivers/gpu/powervr/powervr.h` | `drv_powervr_display_attach(void)` の宣言を末尾に足しただけ（`powervr.c` は変えていない。P2 の p005 との衝突を避けるため） |
| `src/drivers/platform/visionfive2/vf2-gpu.c` | `drv_vf2_gpu_attach` の最初（PMU の前）で `drv_powervr_display_attach()` を呼ぶ（gpu0 にするため）。ENOENT（screen が無い）は黙り、他の失敗は 1 行で、core の attach は続ける |
| `platform/riscv64/vmunix.mk` | CONFIG_DRIVER_POWERVR の時に `display.c` と、共通の GPU core（`gpu.c`・`gpu-fence.c`、PCI の GPU が無い riscv64 では今まで入っていなかった）を足す（arm64 の bcm2711 と同じ `filter-out` の形） |
| `plan/ws269/tests/p007/`（新規） | host 試験 `run.sh`・`display-host-test.c`（偽の 8x4 の BGRX の framebuffer で display.c を動かす） |
| `plan/ws269/tests/p004/vf2-gpu-host-test.c` | vf2-gpu.c が呼ぶようになった `drv_powervr_display_attach` の stub（ENOENT）を足した |

Procedure referenced: `src/drivers/gpu/bcm2711/display-device.c`（device の ops、output_device・constraints）と `src/drivers/gpu/i915/display/bootfb.c`（boot framebuffer の display の契約）。名前は写していない。

### 設計との違い・注

- damage の矩形だけの copy（§3.5）は render の buffer の import（uncached の読みが遅い）の為の物で、自分の resource は cacheable なので、今は frame の全体（clip 後）を copy する。import と一緒に入れる。
- `drv_gpu_register` を board の bring-up（`probe_soc` の中、`drv_vf2_gpu_attach`）から呼ぶ。rpi4 の bcm2711 も board の attach から登録しているので同じ時期で動くと見込むが、VF2 での `/dev/gpu0` の出現は**未確認**（実機の試験で見る）。

## 後半: `scanout` の import（q1340、2026-10-19 P2）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/display.c` | `import_image`: 線形・BGRA/RGBA・4 byte 境界の stride/offset・stride ≥ 幅×4 を確かめ、p006 の `powervr_render_lookup`（render.h）で render の buffer を retain（lookup できない＝render の node の live な export でない backing は ENOTSUP、何も持たない）。行が全部 `powervr_buffer_bytes` に収まる事を確かめ、resource（`buffer`・`image`）を作って session の上限（16）に数える。`resource_destroy` は import なら `powervr_buffer_release`（close の時も core が destroy を先に呼ぶので返る）。import の resource は read・write・map を ENOTSUP。present: BLOB は import だけ・export の layout（幅・高さ・format・stride・offset）と一致する時だけ、`powervr_buffer_view` の uncached の view から 1 word ずつ（plain aligned load）copy、R/B の入れ替え・clip・clean は自分の resource と同じ。普通の present に import を渡すと EINVAL、BLOB に自分の run を渡すと EINVAL。constraints は `COPY | SHARED | FOREIGN`（placement 0：CPU が読むので要らない） |
| `plan/ws269/tests/p007/display-host-test.c` | 偽の `powervr_render_lookup`（export 1 つ、6x2 の BGRA、stride 32）で: constraints、run の BLOB は EINVAL、他の backing の import は ENOTSUP、tiled は ENOTSUP、はみ出す image は EINVAL で reference を返す、import で retain、import への write は ENOTSUP、layout 違いの BLOB は EINVAL、import の普通の present は EINVAL、BLOB present の画素と clean、残りの画素は前の frame のまま、destroy で reference が 0 |

### 設計との違い

- **damage の矩形だけの copy（design.md §3.5）はしていない**: `struct gpu_display_present`（uapi）に damage の欄が無く、display の ops にも渡らない。ABI（core・uapi）を変えずに作れないので、BLOB の present は毎回 frame の全体（clip 後）を uncached の view から copy する。1080p で 1 frame 8 MB の uncached の読み（未計測）。damage が要るなら present の ABI の拡張を別に計画する（Q1 の判断。p010 の計測の後に）。
- BLOB の present は返った時に copy が済んでいるので、frame を保持しない（import の resource の reference は destroy まで持つ）。

### libvulkan の WSI との整合（確かめただけ、変えていない）

- `display_import_image`（`userland/desktop/libvulkan/wsi-display.c`）は constraints の `GPU_SCANOUT_SHARED` と stride/offset の alignment（ここでは 4）を見て `GPU_RESOURCE_IMPORT`（`GPU_IMPORT_SCANOUT`）→ core の `gpu_scanout_import` → render の `share.get_scanout_backing`（render.c の `render_backing`）→ この `import_image` の道。present は `GPU_DISPLAY_PRESENT_FIFO | GPU_DISPLAY_PRESENT_BLOB` で export の descriptor の layout をそのまま送るので、`shared_frame_valid` の一致の条件に合う。
- **合わない所**: libvulkan が shared の道を選ぶのは `context->copy_display == VK_FALSE` の時だけで、それは context.c:176-211 の renderer の capset（WINQ の XML 版と vendor の flag `OPAQUE|STRICT_QUEUE|QUIESCE|HOST_SCANOUT`）で決まる。PowerVR の native の renderer がこの capset を出さない限り、constraints に SHARED があっても libvulkan は copy の道（`GPU_SCANOUT_COPY`）を使う。PowerVR で shared を使うには WS270 の側で capset（または選択の条件）を合わせる必要がある（libvulkan は変えていない、WS270 の範囲）。
- `GPU_DISPLAY_PRESENT_SYNC`（fence 付き）の道はこの node では試していない（display の ops は同期の present なので、core が fence を扱う範囲。p010 で確かめる）。

### 確認（後半）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 p007（拡張） | `plan/ws269/tests/p007/run.sh build/ws269-p007b/host` | PASS |
| host 試験 p004（回帰） | `plan/ws269/tests/p004/run.sh build/ws269-p007b/p004` | PASS（0 failures） |
| VF2・riscv64-virt・amd64 の kernel | `timeout 3000 make ZEDBSD_CONFIG=config/ci/config-<c>.mk BUILD=build/ws269-p007b-<c> vmunix` | 3 つとも rc=0、`warning:`・`error:` 0 件 |
| QEMU | — | 起動していない（依頼の範囲外） |
| 実機 | — | 未実施 |

### 実機の試験の文面（後半、未依頼）

前半の 1〜4 に加えて、p006 の render の node（gpu1）が出る kernel で:

5. render の node で BLOB を作り、線形の BGRA の image として export し（色の帯を書く）、display の node（gpu0）で `GPU_RESOURCE_IMPORT`（`GPU_IMPORT_SCANOUT`）→ claim → `GPU_DISPLAY_PRESENT`（FIFO|BLOB）。**目視**で帯が出る事、release で console に戻る事。試験の program は未作成（p010 の WSI の試験で兼ねる候補。作るなら `gpu-bars` に `--shared` を足す）。
6. import の後に export の fd を閉じても、release・close まで画面が崩れない事（retain の確かめ）。

## 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| VF2 の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p007-vf2 vmunix` | rc=0、`warning:`・`error:` 0 件。vmunix に `powervr: display` の文字列 3 つ（最初は `drv_gpu_register` が未定義で link が失敗 → vmunix.mk に gpu.c を足して直した） |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p007-virt vmunix` | rc=0、0 件 |
| amd64 の kernel | `timeout 3000 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p007-amd64 vmunix` | rc=0、0 件 |
| host 試験 p007 | `plan/ws269/tests/p007/run.sh <OUT>` | PASS（attach・2 回目は EALREADY、query・mode、claim で console の停止と見える範囲だけの消去、2 つ目の claim は EBUSY、present 前の wait は EAGAIN、他の session の resource は EINVAL、RGBA→BGRX の入れ替えと clip、clean 1 回、BLOB と import は ENOTSUP、wait、他の session の release は EINVAL、release と close で console に戻る） |
| host 試験 p004（回帰） | `plan/ws269/tests/p004/run.sh <OUT>` | rc=0、PASS 65・FAIL 0 |
| QEMU | — | 起動していない（依頼の範囲外） |
| 実機 | — | **未実施**（下の文面） |

## 実機の試験の文面（未依頼。ユーザーが拾える試験の一覧の候補）

VisionFive 2、SD の image（WS256 p015 の rootfs からの起動、framebuffer は p011）。この Phase の kernel（CONFIG_DRIVER_POWERVR=y）。

1. 起動して画面の text console に login prompt が出るのを見る。boot log（画面）に `powervr: display <幅>x<高さ> on the boot framebuffer` の 1 行があり、`ls /dev/gpu*` で `/dev/gpu0` がある事（render の node は p005 まで出ない）。
2. `gpu-bars` を走らせる（`userland/tests/gpu-bars/`。`/dev/gpu0` を開き、display の index 0 を query、画面の大きさの resource に赤・緑・青・白の 4 本の縦の帯を GPU_RESOURCE_WRITE で書き、claim → RGBA で present（3 秒）→ BGRA で書き直して present（3 秒）→ release）。claim の前に `GPU-BARS claim WxH: ...` の 1 行が出る。**目視**: 帯が左から赤・緑・青・白で、RGBA・BGRA の 2 回とも同じ色（入れ替わったら swap の誤り）。`--seconds=N` で 1 枚の時間を変えられる。
3. release の後、画面が text console に戻り、それまでの文字が描き直され、最後に `GPU-BARS PASS rgba=1 bgra=1 WxH` の 1 行が出て、keyboard の入力が console に効く事を**目視**。失敗なら `GPU-BARS FAIL step=<open|query|size|create|claim|rgba|bgra|release> error=<errno>`（claim より後の失敗も release してから出す）。
4. `gpu-bars --hold` を走らせ、帯が出た後に Ctrl-C（または別の端末から `kill`）で殺す（release しない）。画面が console に戻る事を**目視**（この場合 PASS の行は出ない）。

image: `plan/ws269/tests/config-p007.mk`（`config/ci/config-visionfive2.mk` の形に `CONFIG_DRIVER_POWERVR := y` と `powervr-firmware gpu-bars` を足した物。`powervr-firmware` は p003 と同じく先に `yes | make ZEDBSD_CONFIG=plan/ws269/tests/config-p007.mk BUILD=<dir> firmware-licenses` が要る）で build した SD の image。

判定: 1〜4 が全部そのとおりなら前半の実機の受け入れ。

### 試験の program（q1334、2026-10-19 P1）

- `userland/tests/gpu-bars/`（`main.c`・`Makefile`、platform は `amd64 visionfive2`。visionfive2 を足さないと riscv64 の選択から落ちる）。
- build: `make ZEDBSD_CONFIG=plan/ws269/tests/config-p007.mk BUILD=build/ws269-p007-bars-vf2 "ZEDBSD_USER_PROGRAMS=cat ls sh gpu-bars" rootfs` rc=0・warning 0、`rootfs/bin/gpu-bars` は riscv64 の PIE（firmware の license の同意は P1 では行わず、firmware を外して確かめた）。amd64: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p007-bars-amd64 ZEDBSD_USER_PROGRAMS=gpu-bars build/ws269-p007-bars-amd64/bin/gpu-bars` rc=0・warning 0。
- host: `cc -Wall -Wextra -Werror` で host 向けに build、`--node=/nonexistent` で `GPU-BARS FAIL step=open error=2`、不正な引数で usage と rc=2。帯の画素の byte の順（RGBA は R,G,B,A、BGRA は B,G,R,A、4 色）を `main.c` を include した一時の試験で PASS。ioctl の経路は実機（未実施）。

## 残り・再開の条件

- 後半: 実装済み（q1340、上の「後半」）。damage の copy は ABI に欄が無く未対応（全面の copy）。libvulkan の shared の選択は WS270 の capset 次第。import の実機の試験は p010。
- 実機の上の 1〜4（program は用意済み、未依頼）。
