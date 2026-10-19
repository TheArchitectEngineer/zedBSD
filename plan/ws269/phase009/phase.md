<!-- awesome-plan project=zedbsd record=ws269-p009 -->

# ws269-p009 最小の Venus の decode（compute）

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) 改訂 3.1 の §5（D6 A: kernel の powervr が Venus を decode）・§6 の p009、D7 (a)
- Queue: q1352 / q1352-i01（P2、2026-10-20）、q1357 / q1357-i01（P2、2026-10-20、残り 1: vkcompute と VF2 の試験の image）
- Status: in-progress（2026-10-20 P2: 実装・host 試験・build（VF2・riscv64-virt・amd64）warning 0。q1357 で試験の program `vkcompute` と VF2 の試験の image の config まで（build warning 0、host の lavapipe で PASS）。**実機待ち**（下の文面）。clearance は Q1 の判定）
- 前提: p008（encoder・compute の program・CDM の kick、実機待ち）。HAL の API は変えない。toolchain は変えない。

## 目的と受け入れの条件

1. libvulkan（`userland/desktop/libvulkan`）が PowerVR の render の node を renderer として開ける: capset、同期の frame（`GPU_COMMAND`）、queue の frame（`GPU_COMMAND_SUBMIT`）、supervised job（`GPU_JOB_*`）。
2. instance・physical device の列挙と照会・device・queue・memory（D7 (a)）・buffer・descriptor・compute の pipeline・command buffer・dispatch・submit を decode し、compute の仕事を core に出す。
3. shader は SPIR-V から USC への compiler がまだ無いので、範囲を決めた小さい subset だけを受ける（下の「SPIR-V の subset」）。
4. build（VF2・riscv64-virt・amd64）が warning 0、host 試験 PASS。QEMU は起動しない。
5. 実機: compute の Vulkan の試験の PASS の行（下の「実機の試験の文面」、残り 1・2 の後）。

## 決めた事

### 置き場所と形

既存の decoder（`src/drivers/gpu/bcm2711/vulkan-*.c`、compute を持つ `src/drivers/gpu/amdrdna/vk/`）の wire の読み方（各 command の field の順）を Procedure referenced で読み、名前と構造は写さずに書いた。wire の reader・writer と生成の record の codec は i915 の物（`src/drivers/gpu/i915/render/codec.c`・`vulkan-codec.inc`、Zlib）をそのまま使う（bcm2711・amdrdna・nvrtx と同じ）。

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/vk.h`（新規） | executor の session・object・payload の型と関数 |
| `vk-object.c`（新規） | session の open・close、wire の identity の登録（session ごとの 1 本の list、数十個なので hash なし）、create・destroy の共通 |
| `vk-stream.c`（新規） | transport: reply の region（blob の uncached の view）、seek、decoder の完了の trailer（release store）、blob の中の command stream（copy してから decode、深さ 4・16 MiB） |
| `vk-device.c`（新規） | instance・physical device（1 つ）・properties・features（全部 0）・memory properties・queue family（compute の 1 family 1 queue）・format（features 0）・device・GetDeviceQueue2（timeline）・wait idle |
| `vk-memory.c`（新規） | memory・buffer・bind・requirements・commitment |
| `vk-layout.c`（新規） | shader module（作る時に subset を読む）・set layout・pipeline layout・pipeline cache（記録しない）・descriptor pool・set・update（storage buffer）・compute pipeline |
| `vk-command.c`（新規） | command pool・buffer・begin・end・reset、記録: bind pipeline・bind descriptor sets・dispatch・pipeline barrier（読むだけ） |
| `vk-queue.c`（新規） | QueueSubmit（control stream を組み kick して待つ）・fence・semaphore、executor の memory |
| `spirv-fill.h`・`.c`（新規、host でも build できる） | SPIR-V の subset の parser |
| `compute-program.c`・`.h` | `powervr_compute_fill_program`（11 group、8 temps） |
| `render-runtime.c`（新規） | capset、同期の frame、queue の frame、job の reserve・commit・cancel・capacity |
| `render.c`・`render-private.h`・`render-queue.c` | blob の identifier と session の list、`blob_id` の blob（memory の buffer を写す）、close で executor を閉じる、ring の entry に error と marker、marker の解放と capacity の通知、空の job 以外の frame を `powervr_render_frame` へ |
| `platform/riscv64/vmunix.mk` | 上の file と i915 の `codec.c` を VF2 の powervr に |
| `plan/ws269/tests/p009/` | host 試験（下） |

### capset（WS277 R7 との関係、Q1 へ報告）

- 1.3.269（`(1<<22)|(3<<12)|269`）、wire_version 1、timelines（byte 152）1、**168 byte で vendor の suffix（"SDBZ"）と flags 7（OPAQUE|STRICT_QUEUE|QUIESCE）**。bcm2711・amdrdna と同じ profile。
- R7（plan/ws277/design.md §9）の「1.3.269 なら vendor の flag の義務無し（156 byte）」は今の libvulkan では成り立たない: libvulkan は `strict_queue`・`native_quiescence` と `GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY` の無い node を instance の作成で外し（`instance.c:731-736`）、device の作成も断る（`device.c:278-281`）。この 2 つは 168 byte の capset の flags 3・7 からしか立たない（`context.c:184-208`）。よって 1.3.269 のまま flags 7 を出し、その義務（strict queue: marker は session の前の仕事の後に完了。quiescence: p006 の stop_begin・stop_poll）を満たした。R7 の共通の必須（wire 1、≥156 byte、timelines ≠ 0、`GPU_CAP_SHARE`）は満たす。**linear の SAMPLED は p009 では満たさない**（image を扱わない、format の features は 0）。p010 の範囲。
- R7 の文面の直しは WS277 の担当（Q1 経由）。

### queue と完了の順

- frame は session の ioctl の中で直ちに decode する（GPU の core の session ごとの admission が直列にする）。QueueSubmit は stream を組んで compute の命令を 1 つ kick し、session の timeline を 2 秒まで待ってから reply を書く（amdrdna と同じく submit の中で待つ）。frame の callback は ring に session の今の timeline の値で入り、前の native の命令の後に service が届ける。
- job の marker は ring の枠を予約し（`reserved`）、commit で ring に入る。cancel(fault) は EIO で ring に入る。届けた時に marker を空け `drv_gpu_capacity_changed`。
- 時間切れ（2 秒）の submit: executor の memory（program・stream）を core に残し（`abandoned`、解放しない）、`drv_gpu_report_session_error(EIO)` で core に session を止めさせる。reply は DEVICE_LOST。
- 空の job（p006 の 8 byte の native の命令）は今まで通り。

### memory（D7 (a)）

- type 0 = DEVICE_LOCAL、type 1 = HOST_VISIBLE|HOST_COHERENT（HOST_CACHED は出さない）、heap 1 つ（DEVICE_LOCAL、256 MiB = 1 資源の上限）。
- AllocateMemory で buffer（連続・uncached の view）を作り、session の address に写す。libvulkan が host visible の memory のために作る blob（`blob_id` = memory の identity）は同じ buffer をもう一度写す。**写像の属性は全部「CPU が見る」の物（SLC を迂回）**: device local の memory も SLC を使わない（R3 の「1 つの page の属性を揃える」を簡単に守るため。性能の費用、p011 以降で見直す候補）。
- import（`GPU_OP_RECORD_IMPORT_RESOURCE`）と export（`EXPORT_MEMORY_ALLOCATE_INFO`）の chain は ENOTSUP（p010 の WSI で）。`GET_PHYSICAL_DEVICE_PROPERTIES2`（identity）も p010。

### SPIR-V の subset

compiler は p010 以降。p009 は 1 つの形だけを受け、driver が USC の program を組む:

- `GLCompute` の entry point 1 つ、function 1 つ、`LocalSize x 1 1`（1 ≤ x ≤ 32、1 task）。
- storage buffer（`StorageBuffer` か `Uniform`+BufferBlock）1 つ（set・binding は decoration、無ければ 0）、`ArrayStride 4`、member 0 の offset 0。
- 本体は「x = gl_GlobalInvocationID.x として、buffer の配列の `x + k` 番目に `a*x + c`（mod 2^32）を 1 回 store」だけ。値の計算に使える命令: Load（invocation id・その成分・function の変数）、AccessChain、CompositeExtract、IAdd・ISub・IMul（片方が定数）・ShiftLeftLogical（定数）、Bitcast・CopyObject、function の変数の Store。y・z の成分は 0（dispatch が 1 行なので）。
- それ以外（buffer の読み、分岐、他の builtin、2 つ目の store、specialization constant、LocalSizeId）は ENOTSUP。module の作成は成功し、pipeline の作成が VK_ERROR_FEATURE_NOT_PRESENT。
- dispatch は y = z = 1、1 回の submit で workgroup は 128 まで。workgroup ごとに kernel を 1 つ（その workgroup の address と値の addend を焼き込んだ program）、dispatch の後に fence の kernel、最後に stream の終わり。push constant・dynamic offset は ENOTSUP。

USC の program（`powervr_compute_fill_program`）: r7 = 番号（sr51）、r0・r1 = base、r2 = 4、r0 = r7·r2 + r0、r3 = a、r4 = c'、r2 = r7·r3 + r4、st r2 → [r0:r1]、wdf、end。p008 の temps・store の program と同じ group の作り方（Procedure referenced は compute-program.c に）。temps の field は p008 と同じく 2 の単位（4 の単位でも足りる）。workgroup が 32 未満の時は KERNEL8 の instances を x、max_instances 0（32）にした（未確認、実機で）。

## 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `timeout 300 sh plan/ws269/tests/p009/run.sh build/ws269-p009/host` | `powervr spirv host test: 69 passed, 0 failed`（glslc で作った 4 つの受ける shader の index・値・set・binding・workgroup、4 つの断る shader、切れた module・壊れた magic、fill の program の長さ（≤ 512、8 byte の終わり）・即値の bytes の位置・拒否の case）、`powervr executor host test: 109 passed, 0 failed`（libvulkan の `wire.c`・`codec.c` をそのまま link し、libvulkan の各 file と同じ framing で frame を書いて executor に通す: instance・列挙・properties（名前・1.1・workgroup の上限）・features・memory（2 type・1 heap・flags）・family・device・queue（timeline 1 の bit）、memory と blob_id の blob（同じ buffer、大きさ違いは EINVAL）、buffer・requirements・bind・module 2 つ（subset の外も作成は成功）・layout・pipeline（subset の外は FEATURE_NOT_PRESENT と null）・pool・set・update・command pool・buffer・begin・記録・end・fence・submit・fence の status、decoder の trailer。submit が core に出した stream: workgroup 3 つの kernel の 7 語、各 USC の program が `powervr_compute_fill_program(memory + 128g, 32, 3, 96g + 7)` と同じ bytes、PDS の program、fence の kernel、終わりの語、slot 0 の fence の program。途中で切れた frame は拒否） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p009-vf2 vmunix -j16`（log `build/ws269-p009/vf2.log`） | rc=0、`warning:`・`error:` 0 件（-Werror）。`llvm-nm` で `powervr_spirv_fill_parse`・`runtime_capset` が link されている（LTO で local） |
| VF2 の p008 の試験の kernel（render-private.h を変えたため） | `... ZEDBSD_CONFIG=plan/ws269/tests/p008/config-visionfive2-compute.mk BUILD=build/ws269-p009-vf2c vmunix -j16`（log `vf2-compute.log`） | rc=0、0 件 |
| riscv64-virt | `... ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/ws269-p009-virt vmunix -j16`（log `virt.log`） | rc=0、0 件 |
| amd64 | `... ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws269-p009-amd64 vmunix -j16`（log `amd64.log`） | rc=0、0 件 |

- QEMU は起動していない。実機の試験は未実施（下）。
- host 試験の framing は libvulkan の source を読んで書いた物で、libvulkan の実行の道（`vulkan_command_execute` など）そのものではない（record の encoder と reader は libvulkan の物）。

## q1357: 試験の program と VF2 の image（残り 1 の結果）

### libvulkan が VF2 の image に入らなかった理由

- 規則の不足ではなかった。libvulkan は menu が `desktop` なので platform は `*`（`Makefile` の `ZEDBSD_USERLAND_PACKAGE`）、riscv64 の link の規則は `platform/common/userland-dynamic.mk` の共通の物（WS270 p002 で riscv64 も include）。VF2 の image に入らなかったのは、`config/ci/config-visionfive2.mk` の `ZEDBSD_USER_PROGRAMS` に libvulkan（と依存の libwayland-client）が無いだけ。
- よって直しは試験の config で選ぶ事だけ: `plan/ws269/tests/p009/config-visionfive2-vkcompute.mk`（CI の config に `libwayland-client libvulkan vkcompute` を足し、`powervr-firmware` を外す）。build で `rootfs/lib/libvulkan.so` と `rootfs/bin/vkcompute` が入る事を確かめた。

### vkcompute（`userland/tests/vkcompute/`）

- `main.c`: 最初の physical device、compute の family の queue 1 つ、HOST_VISIBLE|HOST_COHERENT の memory の storage buffer（4096 語、0xa5 で埋める）、set 0 binding 0、`vkCmdDispatch(128, 1, 1)`、compute→host の barrier、fence（5 秒まで）、map して `3x + 7` を数え、1 行を出す。exit は PASS の時だけ 0。
- `fill.comp` と `shader.h`: `plan/ws269/tests/p009/shaders/fill.comp` と同じ shader。`glslc --target-env=vulkan1.0 -mfmt=num` の出力を `shader.h` に埋め込んだ（host 試験の run.sh と同じ flag なので、host 試験が受けた words と同じ。build に glslc は要らない）。
- package: `userland/tests/vkcompute/Makefile`（platform `amd64 visionfive2`、既定で入らない、require `desktop/libvulkan`）。link の規則は `platform/common/userland-dynamic.mk`（vkdemo と同じ形、`DYNAMIC_PORTABLE_PROGRAMS` に追加）、amd64 の `vmunix.mk` の静的の command の除外にも追加。
- 出力の device の名前は `deviceName` そのもの（PowerVR では `zedBSD powervr (PowerVR BXE-4-32)`）。

### 確かめ（q1357）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2 の試験の image | `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p009/config-visionfive2-vkcompute.mk BUILD=build/ws269-p009-vk disk-image -j16`（log `build/ws269-p009/vk-vf2.log`） | rc=0、`warning:` 0 件。`build/ws269-p009-vk/disk.img`、`rootfs/lib/libvulkan.so`・`rootfs/bin/vkcompute`（RISC-V の PIE、interpreter `/lib/ld.so`） |
| amd64 の vkcompute | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-amd64.mk ZEDBSD_USER_PROGRAMS="libwayland-client libvulkan vkcompute" BUILD=build/ws269-p009-vka build/ws269-p009-vka/bin/vkcompute -j16`（log `vk-amd64.log`） | rc=0、0 件 |
| host（Linux の loader と lavapipe） | `cc -std=c99 -Wall -Wextra -Werror userland/tests/vkcompute/main.c -lvulkan` を実行 | `vkcompute: llvmpipe (LLVM 19.1.7, 256 bits): PASS, 4096/4096 words`、rc=0（program と shader の正しさ。zedBSD の libvulkan・powervr ではない） |

- QEMU は起動していない。実機は未実施。
- firmware 付きの image は未 build（LICENSE.powervr の同意が要る。入れ方は config の注記: `VKCOMPUTE_FIRMWARE=powervr-firmware` を足すと最初の fetch で `license-agree.sh` が全文を見せて yes/no を聞く）。

### T1 への依頼文（任意、amd64 の Venus）

価値: zedBSD の libvulkan の compute の道（buffer・descriptor・compute pipeline・dispatch・fence・map）を、PowerVR の executor と切り離して先に確かめられる（実機で FAIL した時に libvulkan 側か executor 側かを分けられる）。費用は短い（1 command）。

> ws269-p009（q1357）: amd64 の QEMU（Venus）で `vkcompute` を 1 回。image は `config/ci/config-amd64.mk` の Venus の desktop の試験の image に `ZEDBSD_USER_PROGRAMS += libwayland-client libvulkan vkcompute` を足した物（T1 の既存の Venus の config に 3 つ足す形でよい）。login して `vkcompute` を実行し、画面の 1 行を QMP の screendump の PNG で読む。期待 `vkcompute: <Venus の device 名>: PASS, 4096/4096 words`。FAIL の行はそのまま返す。

## 実機の試験の文面（T1・ユーザー向け、VF2）

image: `make ZEDBSD_CONFIG=plan/ws269/tests/p009/config-visionfive2-vkcompute.mk VKCOMPUTE_FIRMWARE=powervr-firmware BUILD=build/<dir> disk-image`（LICENSE.powervr を読んで同意の後）。U29 と同じ手順で VF2 に書き、login して:

```
vkcompute
```

| 行 | 期待 | 読み方 |
| --- | --- | --- |
| `vkcompute: zedBSD powervr (PowerVR BXE-4-32): PASS, 4096/4096 words` | PASS | `FAIL, <n>/4096 words (first bad <i>: 0x…, expected 0x…)`（program・address の誤り。0xa5a5a5a5 は書かれていない）、`FAIL, vkQueueSubmit -4` または `vkWaitForFences -4`（DEVICE_LOST、2 秒の時間切れ。画面の `powervr: vk:` の行も記録）、`FAIL, vkCreateComputePipelines -8`（subset の外と判定された）、`no Vulkan device (<call> <n>)`（render の node が無い・capset で外れた） |

p008 の 3 行（kernel・coherency・temps の単位）の結果を先に見る。

## 残り

1. 実機の試験（T1・ユーザー、上の文面）。任意で amd64 の Venus の vkcompute（上の依頼文）。
2. WS277 R7 の文面（1.3.269 でも flags 7 と job の caps が要る）を Q1 経由で WS277 に。
3. p010 に渡す物: image・linear の SAMPLED・transfer、import・export の memory、`GET_PHYSICAL_DEVICE_PROPERTIES2`、compiler（共有の IR から USC）で subset を置き換える、device local の memory の SLC。
4. Q1 への片付けの依頼: `build/ws269-p009/`（host の出力・log・`inc/vulkan` の symlink）、`build/ws269-p009-vf2/`・`build/ws269-p009-vf2c/`・`build/ws269-p009-virt/`・`build/ws269-p009-amd64/`・`build/ws269-p009-vk/`・`build/ws269-p009-vka/`（p2 の worktree の中）。

## 出典

- wire の framing: zedBSD の `userland/desktop/libvulkan`（instance.c・device.c・memory.c・resources*.c・descriptors.c・pipeline.c・commands*.c・queue.c・context.c）と、それを decode する `src/drivers/gpu/bcm2711/vulkan-*.c`・`src/drivers/gpu/amdrdna/vk/` を Procedure referenced で読んだ（同じ project の Zlib の code。名前・構造は写していない）。
- SPIR-V の opcode・enumerant: Khronos SPIR-V Specification unified 1.6（`spirv-fill.c` の Values referenced）。
- USC・PDS・CDM: p008 と同じ Mesa 26.2.5 の field（compute-program.c の Procedure referenced）。
