# ws269-p010a: UAT の段の行と gpu-selftest の骨

- ID: ws269-p010a（親 [ws269](../ws.md)）
- Status: in-progress（実機待ち）
- Disposition: normal
- Queue: q1364 / q1364-i01（2026-10-20 P1）
- 設計: [design-p010.md](../design-p010.md) 版 1 の §8.1・§8.2・§10 の p010a（design-reviewer のレビューと並行。レビューで変わり得る所は小さく作った）
- 依存: p009（compute の道）

## 目的と受け入れ条件

- kernel の段の行 `powervr: <段>: ok|fail(<理由>)`（firmware・render node・compute・ta・3d・transfer・present）。ok は起動ごとに 1 回、fail は段ごとに 3 回まで、4 回目に `powervr: <段>: further failures not shown`。画面の console（`kern_logf`）に出る。既存の詳しい行は残す。
- sysctl `hw.gpu.powervr.stages`（読むだけの文字列）。
- userland の `gpu-selftest` の骨: 段ごとに fork した子で上限時間、前の段が失敗したら skip、最後に合計の行。段 kernel・node・instance・device・compute。残りは `skip(not built)`。
- VF2 の試験の image の config（D-P2: VF2 の試験の image だけ、起動で自動に流すかは config で選ぶ）。
- build warning 0（VF2・riscv64-virt・amd64）、host 試験 PASS。実機: 下の文面。

## 実装（2026-10-20 P1）

kernel:
- `src/drivers/gpu/powervr/stage.h`・`stage.c`（新規）: 段と理由の enum（理由は意味、errno ではない）、行の規則（`powervr_stage_note_ok`・`_note_fail`）、sysctl の文字列（`powervr_stage_text`）、`powervr_stage_reason_of`（ENOENT・ETIMEDOUT・EBUSY・EFAULT だけ固有の理由、他は `error`）。lock も出力も持たず host で build できる。
- `stage-log.c`（新規）: 起動の 1 つの記録と spinlock（LOCK_RANK_DEVICE）、`powervr_stage_init`・`powervr_stage_ok`・`powervr_stage_fail`（`powervr-private.h` に宣言）。行は lock の外で `kern_logf`。
- sysctl: `include/uapi/sysctl.h` に `HW_GPU_STAGES 11`、`src/kern/sysctl.c` に leaf `hw.gpu.powervr.stages` と `sysctl_gpu_stages`（ENOENT は driver 無し、書き込みは EPERM）、`include/kern/sysctl.h` に `kern_gpu_stages_set`（hw.gpu.start と同じ形、boot の thread で 1 回）と `KERN_GPU_STAGES_MAX 256`。HAL の API は変えていない。
- 呼ぶ所:

| 段 | ok | fail |
| --- | --- | --- |
| firmware | `firmware-boot.c` late_thread の「firmware … running」の行の後 | 同 bring_up の失敗（`image` の step は `bad image`、他は errno から）、`powervr.c` の alias の失敗（`coherency`）・later stage の開始の失敗 |
| render node | `firmware-boot.c` の `powervr_render_start` 成功の後 | 同 失敗（errno から、`ebusy` 等） |
| compute | `vk-queue.c` run() の timeline の待ちの成功（Venus の最初の compute の job の fence） | 同 kick・待ちの失敗（`timeout` 等） |
| ta・3d・transfer・present | まだ無し（p010d・p010f・p010k で足す） | |

- 理由の `spirv subset` は enum にあるが、まだ呼ばない（vertex・fragment の module も同じ create を通るので、compute の段の fail にすると誤る。p010h で compute の pipeline の作成に付ける）。
- `src/kern/sysctl.c`・`include/uapi/sysctl.h`・`include/kern/sysctl.h` は WS269 の外の共有の file。hw.gpu.start と同じ形の 1 leaf の追加（Q1 の merge で確認を）。

userland:
- `userland/tests/gpu-selftest/main.c`・`Makefile`・`service`（新規）。package `gpu-selftest`（visionfive2、既定で入らない、require tests/vkcompute）と `gpu-selftest-boot`（data、`/etc/service.d/gpu_selftest`、oneshot、`-c` で行を `/dev/console` にも書く）。
- 段: kernel（stages の文字列を `gpu-selftest: stages: …` で写し、firmware と render-node が ok なら ok、でなければ `fail(firmware=fail:enoent)` 等）、node（`/dev/gpu0`・`/dev/gpu1` のどれかが O_RDWR で開く）、instance・device・compute（vkcompute を 1 回 exec して出力の行と exit で分ける）、image・clear・copy・draw・sample・depth・present・frame は `skip(not built)`。
- 各段は fork した子（自分の process group）で上限 10 秒（`-t`）、超えたら group ごと kill して `fail(timeout)`、子が死んだら `fail(crash)`。依存の段が ok でなければ `skip(<段>)`。最後に `gpu-selftest: N ok, M fail, K skip`、fail が 0 なら exit 0。
- host 試験用の option: `-s FILE`（stages の文字列）、`-n NODE`、`-v PATH`（vkcompute）。
- `userland/base/etc/rc.conf` に `gpu_selftest`（enabled、optional）を 1 つ（bootlog・netclock と同じ形。service の file が無い image では何もしない）。共有の file（Q1 の merge で確認を）。
- `plan/ws269/tests/p010/config-visionfive2-selftest.mk`: CI の VF2 + libwayland-client・libvulkan・vkcompute・gpu-selftest、`GPU_SELFTEST_BOOT=y`（既定）で gpu-selftest-boot も。firmware は外す（`SELFTEST_FIRMWARE=powervr-firmware` で足す、LICENSE.powervr の同意はユーザー）。
- `plan/ws269/tests/p009/vk-host-test.c` に stage の 3 関数の stand-in（vk-queue.c が呼ぶため）。

## 確認（2026-10-20 P1、host のみ。QEMU は起動していない）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host 試験 | `plan/ws269/tests/p010/run.sh build/p010a/host3` | `stage-host-test: 24 passed, 0 failed`（行の文面、ok 1 回、fail 3 回と 4 回目の行と以後の沈黙、理由の空白、未知の理由・段、sysctl の文字列、短い buffer）。`selftest-host`: pass・firmware 失敗・node 無し・device 無し・compute 失敗・hang（-t 1、1 s で timeout）・sysctl 無しの 7 場面 PASS、`0 failed` |
| p009 の回帰（stand-in の追加） | `plan/ws269/tests/p009/run.sh build/p010a/p009` | `powervr executor host test: 109 passed, 0 failed` |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010a/vf2 vmunix -j16` | rc=0、`warning:`・`error:` 0 件。`stage.o`・`stage-log.o` あり |
| riscv64-virt | 同 `config-riscv64-virt.mk BUILD=build/p010a/virt` | rc=0、0 件（powervr はこの board に link されない） |
| amd64 | 同 `config-amd64.mk BUILD=build/p010a/amd64` | rc=0、0 件 |
| VF2 の試験の image | `timeout 3600 make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p010a/img disk-image -j16` | rc=0。自分の file の warning 0（log の warning は openssl の既存と perl の locale だけ）。`rootfs/bin/gpu-selftest`・`bin/vkcompute`・`etc/service.d/gpu_selftest`、rc.conf に `gpu_selftest`、`disk.img` |
| `git diff --check` | | 空 |

未実施: 実機（VF2）。firmware を入れた image の build（ライセンスの同意はユーザー）。rc の oneshot が `-c` の行を画面に出す事の確かめ（実機で）。

## 実機の試験の文面（ユーザーが選べる試験の一覧へ。今は頼まない）

image: `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk SELFTEST_FIRMWARE=powervr-firmware BUILD=build/<dir> disk-image`（LICENSE.powervr を読んで同意の後）。VF2 に書いて起動し、HDMI の画面を見る。

期待する画面の行（kernel、起動の途中）:

```
powervr: firmware: ok
powervr: render node: ok
```

（既存の `powervr: firmware 1.x build … running, health check ok …`・`powervr: render node ready …` の行の近く）。失敗なら `powervr: firmware: fail(enoent)` 等。

期待する画面の行（gpu-selftest、login prompt の前後に自動で）:

```
gpu-selftest: stages: firmware=ok render-node=ok compute=- ta=- 3d=- transfer=- present=-
gpu-selftest: kernel: ok
gpu-selftest: node: ok
gpu-selftest: instance: ok
gpu-selftest: device: ok
powervr: compute: ok
gpu-selftest: compute: ok
gpu-selftest: image: skip(not built)
…（clear・copy・draw・sample・depth・present・frame も skip(not built)）
gpu-selftest: 5 ok, 0 fail, 8 skip
```

login の後に `sysctl hw.gpu.powervr.stages`（または `/bin/gpu-selftest` をもう一度）で `compute=ok` になっている事。写真を Q1 へ。

## 残り

- 実機の結果で cleared の判定（Q1）。
- design-reviewer のレビューで §8 が変われば合わせる。
- 規約の全文の見直しは WS の最後の Phase で。

## 追補: gpu-selftest の clear〜present-clear の段（2026-10-20 P2 q1376 / q1376-i01）

記録の置き場所はこの phase.md（gpu-selftest の骨の Phase）に決めた。新しい Phase は作らない。

- **program**: `userland/tests/vkstage/`（1 つの program で、段の名前を引数に取る。`vkstage clear|copy|draw|sample|depth|present`）。vkcompute と同じ形で、instance・device（graphics の family）を作り、操作して読み戻し、`<段>: <device>: PASS, …` か `FAIL, <call> <VkResult>`／`FAIL, pixel (x,y) 0x…, expected 0x…` を 1 行出す。6 つの program でなく 1 つにしたのは、package・platform の規則（`platform/common/userland-dynamic.mk`、`platform/amd64/vmunix.mk` の除外）を 1 組にするため。
  - clear: OPTIMAL の 64×64 R8G8B8A8 に `vkCmdClearColorImage`(0.25,0.5,0.75,1) → buffer、全 pixel を比べる。
  - copy: buffer → OPTIMAL → buffer、全 word。
  - draw: render pass（clear 青）+ 三角形 1 つ（緑）、中心と角 3 点。
  - sample: 4×4 の texture（四象限）を全面の四角（strip）に NEAREST で、各象限の中心。
  - depth: D32、近い赤を先に・遠い緑（全面）を後に、中心が赤・角が緑。
  - present: VK_KHR_display の最初の display・mode・plane で FIFO swapchain、1 枚を transfer の clear で出して 1 秒、戻る（vkdemo を exec する段とは別）。
  - shader は `shaders/colour.{vert,frag}`・`texture.{vert,frag}` と `.spv`、`shader.h` に glslc（shaderc 2025.2-1）の `-mfmt=num` の出力を埋め込み、build に glslc は要らない。
- **gpu-selftest**: `-g PATH`（既定 `/bin/vkstage`）。clear・copy・draw・sample・depth と `present-clear`（`vkstage present`、上限は 3 倍の 30 秒）が vkstage を exec する。§8.2 のとおり `frame` を外し、`present`（vkdemo）は skip(not built) のまま。
- **package**: `vkstage`（amd64 visionfive2）、gpu-selftest の依存に `tests/vkstage`、VF2 の試験の config に vkstage。amd64 の試験の config `plan/ws269/tests/p010/config-amd64-vkstage.mk` を足した。
- **compiler（O-VSFS の分）**: `plan/ws269/tests/p010-compiler/compile-host-test.c` に vkstage の 4 つの shader（key なし・key あり: 属性 106/109/103、出力 37）を足した。全部 compile でき、IR の実行と machine の比較が一致。compiler の機能の不足は無かった（8/16 bit・整数の除算は要らない形）。制限: key ありの fragment の値の確認は、試験の iterate の値が 1 を超えて pack で 255 に飽和するので弱い。

### 確認（2026-10-20）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| host の Vulkan（llvmpipe）で vkstage | `cc -Wall -Wextra -Werror userland/tests/vkstage/main.c -lvulkan`、各段 | clear・copy・draw・sample・depth PASS。present は host に display が無く `FAIL, no display -3`（期待どおり） |
| gpu-selftest の host 試験 | `sh plan/ws269/tests/p010/selftest-host.sh OUT` | 10 件 PASS（draw の失敗で sample・depth が skip(draw)、present-clear の timeout を足した） |
| compiler の host 試験 | `sh plan/ws269/tests/p010-compiler/run.sh OUT` | 382 passed, 0 failed、group 20 passed |
| VF2 の試験の image | `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p010st/img disk-image -j16` | rc=0、`-Werror` で vkstage・gpu-selftest は warning 0、`rootfs/bin/vkstage` あり |
| amd64 | `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-amd64-vkstage.mk BUILD=build/p010st/amd64 disk-image -j8` | rc=0、warning 0、`rootfs/bin/vkstage` あり |

未実施: QEMU（Venus）と VF2 の実機。VF2 では driver の transfer・render（P1 の p010n）が入るまで clear 以降は fail が見込まれる。

### T1 への依頼文（案、Q1 が判断して送る）

amd64 の QEMU の Venus で vkstage を流す（driver を介さない参照の結果として価値がある）: image `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-amd64-vkstage.mk BUILD=build/<dir> disk-image`、Venus の GPU 付きで起動し、SSH で `/bin/vkstage clear`、`copy`、`draw`、`sample`、`depth` を各 1 回（各 10 秒の上限）。期待は 5 行とも `PASS`。`present` は display の node の有無で結果が変わるので参考（PASS なら QMP の screendump の PNG を 1 枚）。
