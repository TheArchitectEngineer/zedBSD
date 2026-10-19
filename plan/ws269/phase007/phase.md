<!-- awesome-plan project=zedbsd record=ws269-p007 -->

# ws269-p007 display の node（boot framebuffer の `drv_gpu_display_ops`）と `scanout` の import

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §3.5 の「表示」・§6 の p007 と §6 の末尾の必須項目（「display の node は powervr と同じ driver に置き、import は private な lookup。import の部分は p006 の後」）
- Queue: q1333 / q1333-i01（P1、2026-10-19、前半: claim・present まで）
- Status: in-progress（2026-10-19 P1: 前半の実装・build・host 試験まで。残り: (1) `scanout` の import（p006 の render の `share` の後）、(2) 下の実機の確認（未実施）。clearance は Q1 の判定）
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
2. 自分の resource で claim して色を出す試験の program（後で用意: `/dev/gpu0` を開き、GPU_RESOURCE_CREATE で画面の大きさの resource を作り、赤・緑・青・白の 4 本の縦の帯を GPU_RESOURCE_WRITE で書き、GPU_DISPLAY_QUERY → GPU_DISPLAY_CLAIM → GPU_DISPLAY_PRESENT（RGBA と BGRA の両方を 1 回ずつ、各 3 秒）→ GPU_DISPLAY_RELEASE）を走らせる。**目視**: 帯が左から赤・緑・青・白で出る（RGBA・BGRA のどちらでも同じ色。色が入れ替わったら swap の誤り）。
3. release の後、画面が text console に戻り、それまでの文字が描き直されて、keyboard の入力が console に効く事を**目視**。
4. 同じ program を claim の後で Ctrl-C で殺し（release しない）、画面が console に戻る事を**目視**。

判定: 1〜4 が全部そのとおりなら前半の実機の受け入れ。試験の program は Q1 が依頼する時に作る（今は無い）。

## 残り・再開の条件

- 後半: `scanout` の import（render の buffer の object を driver の中の private な lookup で retain し、uncached の VA から damage の矩形だけを copy、constraints に SHARED・FOREIGN、BLOB の present）。条件: p006（render の node の `share`）が main に入った後。import の試験は p010。
- 実機の上の 1〜4。
