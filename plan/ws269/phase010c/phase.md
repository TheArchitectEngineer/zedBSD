# ws269-p010c: image・format・view・sampler

- ID: ws269-p010c（親 [ws269](../ws.md)）
- Status: in-progress（実装と host 試験は済み。実機・T1 の試験は p010 の最後にまとめる。Q1 の判定待ち）
- Disposition: normal
- Queue: q1368 / q1368-i01（2026-10-20 P1）
- 設計: [design-p010.md](../design-p010.md) 版 2 の §3・§7（M8）・§10 の p010c、[design-p010-review-2.md](../design-p010-review-2.md) の Minor 2・Minor 10（Q1 の追加の指示、2026-10-20）
- 依存: p010b（cleared）、O-P009

## 受け入れ条件

- image: LINEAR は rowPitch が 32 byte かつ 2 pixel の倍数の最小、OPTIMAL は twiddled（2 の冪へ切り上げ、3D は alignment 4、mip が 2 段以上なら layer は 1×1 へ向けて数える）、alignment は page（4 KiB）。
- format の表（vkdemo・Keiland・WSI が使う物）、view・sampler の texture state の word（各 2 × 64 bit、LINEAR は strided の word 1）、descriptor の大きさ、Venus の decode への接続。
- M8: p010l まで external の chain・export・import が ENOTSUP（→ `VK_ERROR_FEATURE_NOT_PRESENT`）、linear の SAMPLED を出さない（review 2 の Minor 2: 外すのは linear の SAMPLED だけ、TRANSFER などは出す）。host 試験で Venus の frame を通して確かめる。
- M9: word の golden を Mesa 26.2.5 の `gen_pack_header.py` の pack の header と一度だけ照合（tree に入れない）。
- build warning 0（VF2・riscv64-virt・amd64）、host 試験 PASS。gpu-selftest の `image` の段。

## 実装（2026-10-20 P1）

kernel（`src/drivers/gpu/powervr/`）:
- `image-state.h`・`image-state.c`（新規、lock も memory も持たず host で build できる）: format の表（R8・R8G8・RGBA8・RGBA8 sRGB・BGRA8・BGRA8 sRGB・D16・D32、vertex の R32G32・R32G32B32・R32G32B32A32）、`powervr_image_lay_out`（LINEAR の row、twiddled の大きさと各段の offset）、`powervr_view_words`（twiddled は 2D の word、LINEAR は strided の word 1、channel の選びは client の mapping を format の channel に重ねる）、`powervr_sampler_words`（NEAREST・LINEAR、REPEAT・MIRRORED_REPEAT・CLAMP_TO_EDGE・CLAMP_TO_BORDER、border は透明の黒 = index 0、float は bit から整数で固定小数に）、`powervr_descriptor_bytes`（buffer 16・image 32・sampler 48・combined 80）、`powervr_format_linear_features`（`POWERVR_FORMAT_LINEAR_SAMPLED 0` の間は SAMPLED・SAMPLED_FILTER_LINEAR だけを外す。p010l で 1 にする）。出典は各 block の直前の複数行の comment（Value(s)・Procedure referenced、Mesa 26.2.5 の path と表・関数の名前）。
- `vk-image.c`（新規）: `GPU_OP_CREATE_IMAGE`（external の宣言は ENOTSUP、2D・1 sample・表の format・tiling の features、4096 と 256 層まで）・`DESTROY_IMAGE`・`BIND_IMAGE_MEMORY`（offset は 4 KiB の倍数）・`GET_IMAGE_MEMORY_REQUIREMENTS`（size は page に切り上げ、alignment 4096、type は 2 つ）・`GET_IMAGE_SUBRESOURCE_LAYOUT`（LINEAR だけ、offset 0）・`CREATE/DESTROY_IMAGE_VIEW`（bound の image、同じ format、2D と 2D_ARRAY、words は作成の時に組む、image を retain）・`CREATE/DESTROY_SAMPLER`（anisotropy・compare は ENOTSUP、透明の黒でない border は kernel の log に 1 回）。
- `vk-device.c`: format の照会を表から（linear は上の関数）、image format の照会（2D、flags 0、usage ごとに feature を確かめる、limits 4096・13 段・256 層、LINEAR は 1 段 1 層）。`maxImageDimension2D`・`maxImageArrayLayers` は `vk.h` の定数に。
- `vk.h`（payload の struct、route、定数）、`vk-stream.c`（route の追加）。
- export と import は p009 の `allocate` が chain を ENOTSUP にしていて、そのまま（M8 の試験で確かめた）。

userland:
- `userland/tests/vkimage/`（新規 package `vkimage`）: linear の SAMPLED が出ていない、external の宣言の image が `FEATURE_NOT_PRESENT`、LINEAR の 100×10 の rowPitch が 32 の倍数、OPTIMAL の image の memory・bind・view・sampler。1 行 `vkimage: <device>: PASS|FAIL, …`。
- `gpu-selftest`: `image` の段が vkimage を実行（`-i PATH`、既定 `/bin/vkimage`、exit 0 で ok、他は出力の行を理由に）。vkcompute の実行を `run_program` に分けた（exec 失敗の行が `_exit` で消えていたので `fflush` を足した）。package の require に `tests/vkimage`。
- `plan/ws269/tests/p010/config-visionfive2-selftest.mk` に vkimage。

共有の file（Q1 の merge で確認を）: `platform/riscv64/vmunix.mk`（vk-image.c・image-state.c）、`platform/common/userland-dynamic.mk`（vkimage を vkcompute と同じ形の dynamic の Vulkan client に）、`platform/amd64/vmunix.mk`（静的の command の除外の列に vkimage）。HAL・toolchain は変えていない。

Mesa との違い・決めた事:
- mip が 2 段以上で段の数が鎖より少ない時、Mesa の手順は鎖の残りを 1×1 の直前まで足し、1×1 の段自体は足さない（`pvr_image_plane_setup_mip_levels` の while の条件）。同じ数え方にした（Mesa が実機で使う形）。
- format 照会の `maxMipLevels` は 13（4096 の辺）。

## M9 の照合（一度だけ、tree に入れない）

- 取得: `https://archive.mesa3d.org/mesa-26.2.5.tar.xz` を `build/p010c/dl/`（自分の worktree）へ。sha256 `794b3d1729075dae7ebd47c7283b7ca0cdb5dec668f7f11cc0a5d82f6138a0a5`。`src/imagination` だけを `build/p010c/src/` へ展開。`texstate.xml`・`pbestate.xml` の blob SHA は p010b の値（d4028d75…・b06f02db…）と一致。
- 生成（review 2 の Minor 10 の形: 展開とは別の `build/p010c/gen/` へ出し、`python3 -I` で、入力の path は引数で渡す）: `python3 -I build/p010c/src/mesa-26.2.5/src/imagination/csbgen/gen_pack_header.py build/p010c/src/mesa-26.2.5/src/imagination/csbgen/rogue/texstate.xml > build/p010c/gen/texstate.h`（pbestate も同じ）。sha256: texstate.h `85e6365a…af7d`、pbestate.h `f4a38b28…2780`。
- 比べる program: `build/p010c/m9/compare.c`（tree の外）。同じ意味の値を Mesa の struct に詰めて `ROGUE_TEXSTATE_*_pack` で pack し、zedBSD の word と比べる。format の texture の code と PBE の packmode は Mesa の enum と比べる。
- 結果（2026-10-20 08:23、最終のコードで再実行）: `M9: 63 words/values compared, 385 fields, 0 mismatches`。内訳: format 11 行（22 値）、view 20 件（twiddled 13: 8 format・mip と層の部分・320×240・mapping 3 件、strided 7: 6 format・1920）× 2 word、sampler 6 件 × 2 word。
- in-tree の `image-host-test.c` の期待値の word はこの照合で一致した値。PBE の word の組み立ては p010e（render pass の begin）で、照合も p010e で足す。

## 確認（2026-10-20 P1、host のみ。QEMU は起動していない）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| image-state の host 試験と p010a の試験 | `sh plan/ws269/tests/p010/run.sh build/p010c/p010` | `stage-host-test: 24 passed, 0 failed`、`image-host-test: 55 passed, 0 failed`、`selftest-host: 0 failed`（8 場面、新しく image の段の fail の場面） |
| executor（Venus の frame、M8 を含む） | `sh plan/ws269/tests/p009/run.sh build/p010c/p009` | `powervr executor host test: 157 passed, 0 failed`（p009 の 109 に image の frame: RGBA8 の linear に SAMPLED なし・TRANSFER あり、linear の sampled の照会が `FORMAT_NOT_SUPPORTED`、external の宣言の image・export・import が `FEATURE_NOT_PRESENT`、100 texel の linear の rowPitch 416、30×20 optimal が 4096 byte・alignment 4096、bind・view・sampler、view の word の address が memory + bind の offset） |
| M9 | 上の節 | 0 mismatches |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010c/visionfive2 vmunix -j16` | rc=0、`warning:`・`error:` 0 件 |
| riscv64-virt | 同 `config-riscv64-virt.mk BUILD=build/p010c/riscv64-virt` | rc=0、0 件 |
| amd64 | 同 `config-amd64.mk BUILD=build/p010c/amd64`（vmunix.mk の変更の後にもう一度） | rc=0、0 件 |
| VF2 の試験の image | `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p010c/img disk-image -j16` | rc=0、自分の file の warning 0。`rootfs/bin/vkimage`・`gpu-selftest`・`vkcompute` |
| `git diff --check` | | 空 |

未実施: 実機（VF2）での vkimage・gpu-selftest の `image` の段。QEMU（Venus の host）での vkimage。

## 実機の試験の文面（ユーザーが選べる試験の一覧へ。今は頼まない）

VF2 に上の試験の image（firmware は `SELFTEST_FIRMWARE=powervr-firmware`、LICENSE.powervr の同意はユーザー）で起動し、`gpu-selftest: image: ok` が出ること（`vkimage` を手で流すと `vkimage: PowerVR …: PASS`）。

## 残り

- p010l で `POWERVR_FORMAT_LINEAR_SAMPLED` を 1 にし、external の宣言・export・import を開ける（vkimage と executor の試験の期待も変える）。
- descriptor の set の memory への書き込み（image の meta の 4 word、PCK_INFO を含む）は p010h。PBE の word は p010e。
- 片付け（Q1 に path を渡す）: `build/p010c/`（tarball・展開・生成の header・比べる program・build の出力）。
