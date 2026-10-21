<!-- awesome-plan project=zedbsd record=ws270-p006a -->

# ws270-p006a Keiland の起動と不足の一覧

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-20 P1、q1356 / q1356-i01: 2a の実装・build・host 試験。2026-10-21 P1、q1517 / q1517-i01: 2b の image・host の読み・板の手順。板の確認は Q1 待ち）
- 設計: [design.md](../design.md) 改訂 2.1 の §2.2 の 2a・2b、§3 の p006a。
- 依存: WS269 p010、本 WS の p004、WS269 p005（powervr の遅い段が gpu1 の登録まで `hw.gpu.attaching` を数える。q1356 で同時に実装）。

## 範囲

1. （2a、済み）sessiond の `main_wait_display` が、`/dev/gpu0` があっても `hw.gpu.attaching` が 0 でない間は待つ（上限は今の `MAIN_DISPLAY_WAIT_MS` 120 s）。
2. （2b、q1517 で image と手順まで）VF2 で Keiland を起動し、Vulkan の機能の不足の一覧と、それを確かめる試験の program を作り、WS269 p011 の入力として Q1 に渡す。

## 2a の実装（2026-10-20 P1、q1356）

| file | 変更 |
| --- | --- |
| `userland/desktop/sessiond/fallback-rules.c`・`.h` | 判断だけの関数 `sessiond_display_next(present, attaching, waited_ms, limit_ms)` と `enum sessiond_display_step`（READY・NONE・WAIT・TIMEOUT）。attaching が 0 なら present で READY か NONE、そうでなく上限に達したら TIMEOUT、他は WAIT |
| `userland/desktop/sessiond/main.c` `main_wait_display` | 毎回 gpu0 を stat し attaching を読んで上の関数で決める。NONE は前と同じ最後の 1 回の stat（数と look の間に出た node を拾う）。TIMEOUT は log（`SESSIOND DISPLAY timeout waited_ms=… attaching=… present=…`）の後、gpu0 があれば使い、無ければ前と同じく errno を返す |

### amd64・rpi4 の振る舞い（code で確かめた事）

- **rpi4（bcm2711）**: `src/drivers/gpu/bcm2711/` に `kern_gpu_attach_begin` の呼び出しは無い（grep）。attaching は常に 0 なので、gpu0 があれば READY、無ければ NONE（最後の stat）で、前と同じ。
- **amd64 の i915 だけの機械**: i915 は device ごとに start の registry の解放で数え（`i915/device.c:921`、`:370`）、node の登録か失敗で `drv_i915_device_attach_settled` → `kern_gpu_attach_end`（`:1110-1121`）。前は gpu0 が見えた時点で抜け、今は settled まで待つ。settled は node の登録の直後なので、差は高々 1 回の poll の間隔（`MAIN_DISPLAY_POLL_NS`）。
- **amd64 の nvrtx（resident かつ display）**: `nvrtx.c:385-388` で +1、worker の終わり（`:409-413`）で -1。node の登録は worker の最後の段なので同じく差は poll の間隔。
- **振る舞いが変わる場合**: GPU が 2 つあり、片方の gpu0 が先に出て、もう片方（i915・nvrtx・amdrdna）がまだ attaching の機械（例 iGPU と dGPU の両方を使う機械）。前は先の gpu0 で greeter を起動し、今は全部が settled まで待つ（上限 120 s）。libvulkan が node の組を instance ごとに固定する（design §2.2 の 2a）ので、待つ方が意図に合う。T1 の回帰の対象（amd64 は 1 GPU の QEMU）ではこの場合は起きない。

## 確かめ（2026-10-20、worktree /home/awe/zedBSD-worktrees/p1、host。QEMU は起動していない）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| VF2（desktop の試しの config）kernel と sessiond | `timeout 1800 make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk BUILD=build/visionfive2 vmunix build/visionfive2/bin/sessiond` | rc=0、`warning:`・`error:` 0 件（log `build/q1356-vf2.log`、main.c・fallback-rules.c・powervr.c・firmware-boot.c を compile） |
| riscv64-virt の kernel | `timeout 1800 make ZEDBSD_CONFIG=config/ci/config-riscv64-virt.mk BUILD=build/riscv64-virt vmunix` | rc=0、0 件（powervr は入らない） |
| amd64 の sessiond | `timeout 2400 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/amd64 build/amd64/bin/sessiond` | rc=0、0 件 |
| rpi4 の sessiond | `timeout 2400 make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p1-rpi4 build/p1-rpi4/bin/sessiond` | rc=0、0 件 |
| host 試験（新規） | `timeout 120 sh plan/ws270/tests/run-host-display-wait.sh` | plain・ASan/UBSan とも `PASS 8 FAIL 0` |
| host 試験（WS239、同じ file の既存） | `timeout 120 sh plan/ws239/tests/run-host-fallback-rules.sh` | PASS（plain、ASan/UBSan） |

未実施: QEMU・実機。amd64・rpi4 の desktop の回帰は下の T1 の依頼で。

## T1 への依頼文（Q1 が判断して送る）

- **ws270-p006a の sessiond（amd64・rpi4 の回帰）**: P1 の WIP（返却の SHA）を含む main で、amd64（QEMU、`config/ci/config-amd64.mk`）と rpi4（QEMU raspi4b か既存の rpi4 の回帰の道）の image を作り、`plan/tools/boot-test.sh` で (1) graphical login（`login=graphical`）の greeter の画面、(2) text login（graphical なし）の login prompt を PNG で撮る。合否: (1) greeter が出る、(2) login prompt が遅れずに出る。

## 2b の準備（2026-10-21 P1、q1517 / q1517-i01）

### image

- config: `plan/ws270/tests/config-visionfive2-desktop-gpu.mk`。`config-visionfive2-desktop.mk` を include し、次の 2 つを変える。
  - graphical login を y にする。
  - `mount sh gpu-selftest vkmatrix powervr-firmware` を足す。
  - DRIVER_POWERVR は y のまま。
  - Keiland と desktop の program は rpi4 と同じ並び。
- firmware の入手:
  - license の同意はユーザーが済ませている（main の `build/sources/firmware/powervr/license-accepted`）。
  - worktree の `build/sources/firmware/powervr` を、main の cache への symlink にして読んだ（Guardrail の「共有の build/ は symlink で読む」）。同意の操作はしていない。
- build: `timeout 3600 make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop-gpu.mk BUILD=build/q1517 -j16 build/q1517/native.img` で rc=0。
  - 自前の source の warning は 0。log `build/q1517-native.log` の warning は、外部 package の openssh の deprecated と llvm-ar の注記だけ。
  - 初めに `disk-image`（ramdisk の 64 MiB の root.img）を作ろうとしたら inode が尽きた。VF2 の板の SD は native.img なので、そちらを使う。
- 成果: `/home/awe/zedBSD-worktrees/p1/build/q1517/native.img`。
  - 412,090,368 byte。
  - sha256 `7b4faada1037dcad6bd7744d292459d14e7a58448fdd5a460ede90430fc91714`。
  - rootfs は 73 MiB。`/lib/firmware/powervr/rogue_36.50.54.182_v1.fw`、`/bin/gpu-selftest`、`/bin/vkmatrix`、`/bin/wayland` が入っている。
  - `login=graphical` は disk-image.mk が boot の行に足すので、手で足さない。
- SSH の host key は入れていない。Q1 が `build/uat/vf2-gpu/hostkeys` を `build/q1517/rootfs/etc/ssh/` に置き、同じ make で native.img を作り直す。

### host での読み（Keiland の起動の道）

1. **sessiond**（2a）: `/dev/gpu0`（dc8200）が出ても、`hw.gpu.attaching` が 0 になるまで待つ。powervr の遅い段が gpu1 の登録まで数える（WS269 p005）。
2. **libvulkan**: `instance.c` が `/dev` の GPU node を全部開き、node ごとに physical device を作る。Keiland の `compose_device` は最初の physical device の最初の graphics queue family を使う。
   - render node（gpu1）の queue family は GRAPHICS・COMPUTE・TRANSFER を持つ（863b44cf1、板 81）。
   - gpu0 は display だけの node。vkmatrix・gpu-selftest は gpu0 がある板で通っている。
3. **拡張**:
   - Keiland が必ず有効にする device の拡張は、swapchain、external memory（fd）、memory requirements2、dedicated allocation の 5 つ。
   - powervr の node は `GPU_CAP_SHARE`（render.c:160）を持つので、libvulkan は external memory fd を出す。kernel の executor にも export・import がある（vk-memory.c・vk-image.c）。
   - `GPU_CAP_FENCE` が無いので、external fence は出ない。Keiland は fence fd を使わない道に入る（compose.c、i915 と同じ）。
   - display control（`GPU_CAP_DISPLAY_EVENTS`）も出ない。hotplug の event は使わない道。
4. **present**:
   - Keiland は VK_KHR_display の swapchain で出す。
   - libvulkan の `wsi-display-nodes.c` が、別の display node（gpu0）を組にして readback して出す。vkimage の present の PPM は板 83 で確認済み。
   - game mode の直の scanout（`scanout-zedbsd.c`）は gpu0 に GPU_DISPLAY_BLOB が無ければ ENOTSUP で、合成に戻る。
5. **shader**: Keiland の shader 15 個（quad・panel・batch・turn と panel の 8 つの変種）は、powervr の compiler で全部 compile できた（host の probe、下の表）。

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| Keiland の shader の compile | `plan/ws270/tests/p006a/shader-probe.c` を compiler の 6 file と cc -Werror で build し、`build/q1517-probe/shader-probe` を流す | 15/15 OK（`FAILED 0`。panel.frag は 5208 byte・temps 55） |

### 板で通った範囲との差（不足の候補、WS269 p011 の入力）

Keiland が使い、vkmatrix 15/15・gpu-selftest 13/13 で確かめていない物を挙げる。

| # | 項目 | 理由 | 担当 |
| --- | --- | --- | --- |
| G1 | flat の varying、4 つの vertex attribute（R32G32 + R32G32B32A32 ×2 + R32） | batch.vert/frag。vkmatrix は位置と texcoord だけ | 板で見る。駄目なら compiler（P2） |
| G2 | push constant 96 byte を vertex と fragment の両方で使う | panel の layout。vkmatrix の push128 は片方の stage だけ | 同上 |
| G3 | 分岐と数学の多い fragment shader（length・abs・clamp・mix・dot・除算、temps 55） | 結果の正しさは host の compile だけで、板では未確認 | 同上 |
| G4 | 1 frame に 16 draw を超える pass（job の分割と LOAD の draw） | 正しさは draws-64 で確認済み。速さは未測定で、frame ごとに複数の job の同期の待ち（D-P4 の 2 秒）になる | driver（P2、p011） |
| G5 | client（terminal など）の buffer の export と、compositor での import（別の session） | vkmatrix・gpu-selftest は 1 つの session だけ | driver（vk-memory の import）。板で見る |
| G6 | 1080p の swapchain の readback present の速さ（CPU の copy） | vkimage は小さい画だけ | WSI。板で見る |
| G7 | vkCmdCopyImageToBuffer（screenshot） | gpu-selftest の copy は buffer→image | 板で見る |
| G8 | GPU の memory の量（1080p の複数の image と client の buffer） | heap の上限を見ていない | driver（P2） |

- desktop の code（WS270 の範囲）で直す所は、読んだ範囲では見つからなかった。

## 板の確認の手順（Q1 が SSH で行う。P1 は板を操作しない）

1. 上の native.img を SD に書くか、host key を入れて作り直した物を書く。板は kei@10.0.30.2。HDMI の monitor と USB の keyboard・mouse をつなぐ。
2. 起動後、SSH で次を読む。
   - `ps ax | grep -E 'sessiond|wayland|greeter'`: sessiond と、greeter の compositor（`/bin/wayland`、`_greeter` の user）が動いていること。
   - `cat /var/log/sessiond.log`:
     - `SESSIOND DISPLAY` の行（待った時間・attaching・present）があること。
     - `SESSIOND GREETER` があること。
     - `timeout` が無いこと。
   - `cat /var/log/greeter.log`:
     - `KWL STARTUP` があること。
     - `KWL FAILED` と `KWL VULKAN_ERROR` が無いこと。
   - `sysctl hw.gpu.powervr.stages`: 全部の段が ok。
   - `dmesg | grep powervr`: 次のどれも無いこと。
     - `device lost`
     - `context reset notice`
     - `session lost`
     - `refused (EIO)`
     - `quarantine`
     - `firmware restart`
   - `ls -l /dev/gpu*`: gpu0 と gpu1 がある。
3. 画面: greeter の login の画面が monitor に出る（写真か、ユーザーの目視）。
4. kei で login する（greeter で）。
   - 続けて `<XDG_RUNTIME_DIR>/session.log`（`find / -name session.log 2>/dev/null`）を見る。`KWL FAILED` が無いこと。
   - desktop が出て、terminal が開く。
   - 2 の dmesg をもう一度見る。
5. 同じ板で `/bin/vkmatrix` と `/bin/gpu-selftest` を流す（回帰。15/15 と 13/13）。

**合格**:
- 2 の条件が全部満たされる。
- 3 で greeter が見える。
- 4 で desktop と terminal が出て、dmesg に lost・reset が無い。
- 5 が 15/15 と 13/13。

**不合格の時**:
- greeter.log・session.log・dmesg の全部と、`hw.gpu.powervr.stages` を P1 に渡す。
- G1〜G8 のどれに当たるかで、担当を分ける。

## 残件

- 2b の板の確認（上の手順、Q1）。
- 不足の G1〜G8 の扱い（板の結果で決める）。
- T1 の回帰（上）。
- 片付け（Q1 に依頼）: worktree /home/awe/zedBSD-worktrees/p1 の `build/q1356-p004host/`・`build/q1356-vf2.log`・`build/q1356-virt.log`・`build/q1356-amd64.log`・`build/q1356-rpi4.log`・`build/tmp/ws270-display-wait*`。

## 板の結果（2026-10-21 Q1、desktop の 1 回目）

- 方法: SD を書き直さず、`plan/tools/vf2/overlay.sh build/uat/vf2-gpu/rootfs build/uat/vf2-desktop/rootfs OUTDIR` で desktop の rootfs の差分（255 file、setuid と 644/755 以外の mode を戻す）を板の root に入れ、/boot/boot0/zedbsd.cfg に `login=graphical`（元は zedbsd.cfg.old、kernel の元は vmunix.old）、kernel は build/uat/vf2-desktop/vmunix（main 7f1ce814e 以降、config-visionfive2-desktop-gpu.mk、screen capture 入り）。
- 起動: `SESSIOND DISPLAY waited_ms=2800` → greeter → `AUTOLOGIN user=kei` → `SESSIOND DESKTOP shown at_ms=12514`。session.log: `KWL DISPLAY device=zedBSD powervr (PowerVR BXE-4-32) width=1920 height=1080`、Vulkan の段が全部通る。dmesg に device lost・context reset 無し。
- 画面: keiland-shot（swapchain の画）で panel・壁紙・時計が出ている（build/review/vf2-desktop/shot1.png）。
- 遅さ: 1 frame の submit が約 160〜180 ms（`KWL PERF rec … submit=161`、present 9〜10 ms）→ 約 5 fps。G4（job ごとの同期の待ち）か。
- client: `terminal` を session の中で起動すると `ZTERM FAILED run=term operation=vkCreateSwapchainKHR result=-11`（VK_ERROR_FORMAT_NOT_SUPPORTED）。G5（client の buffer の export/import、p010l の shared の道）の側。
- SD の dw-mmc の read の失敗（BUG-343）は起動の間にも出るが、起動は通った。

## q1523-i01（2026-10-21、P2）: client の vkCreateSwapchainKHR -11

- 原因: libvulkan の Wayland の platform（`userland/desktop/libvulkan/wsi-wayland.c`）は copied の present を持たない（ops の present は NULL）。
  `swapchain_create` は shared の道（`swapchain_native_images_create` → `wsi-image.c`）だけを使える。
  その道は、format の linearTilingFeatures に SAMPLED_IMAGE が無いと VK_ERROR_FORMAT_NOT_SUPPORTED を返す（wsi-image.c:57）。
  PowerVR は `POWERVR_FORMAT_LINEAR_SAMPLED` 0 で linear の SAMPLED を外していたので、Wayland の swapchain はどれも -11 になった。
- 修正: shared の道に必要なので、`src/drivers/gpu/powervr/image-state.h` の `POWERVR_FORMAT_LINEAR_SAMPLED` を 1 にした
  （p010l で用意した切り替え。executor の linear の image state・external・export・import は p010l で host を通している）。
- 確認:
  - p009 の executor host 試験は、切り替え 0・1 とも 505 passed、0 failed。
  - p010 の host 試験は 0 failed（image-host-test は切り替えに従う期待に直し、63 passed）。
  - VF2 の vmunix は rc 0、warning 0。
  - 板は未実施（Q1 が overlay して terminal を確かめる）。
  - libvulkan の判断（Wayland が copied の道を持たない）は host 試験にしていない。

## q1523-i02（2026-10-21、P2）: linear の client image の合成で 3D が lockup

- 板（Q1）: client の window は import され描かれるようになったが、合成の数 frame 後に compositor の 3D job が lockup した
  （reason 1、dm 3、fault 0）。
- 読んだ範囲: linear の texture state（STRIDE、word1 の stride は texel 単位で −1、num_mip_levels 1、texaddr）と row の揃え
  （私たちは 32 byte、Mesa は PBE の 2 pixel）は Mesa の `pvr_arch_tex_state.c`・`pvr_image.c` と矛盾しない。読むだけでは原因が出ない。
- 診断: vkmatrix に sample-linear-1180（1180x8）と sample-linear-65（65x3）を足した。LINEAR R8G8B8A8 を mapping から行ごとに書き、
  nearest で 32x32 の quad に sample して、pixel ごとに比べる。
  lavapipe（`plan/ws269/tests/vkmatrix/run.sh`）で 22/22 PASS。VF2 の vmunix と bin/vkmatrix は rc 0、warning 0。
- 次: 板で sample-linear-* の結果（PASS、FAIL、lockup）を見て、desktop の前に linear の sampling だけを切り分ける。

## 板の経過（2026-10-21 Q1、desktop の 2〜4 回目）

- d2（q1523 の linear SAMPLED on、ec66edf23）: client の swapchain は通り、settings --welcome（1180x800）・files の desktop（1920x1036）が import される。数 frame の後に compositor の 3D が lockup: `context reset notice: context 0x5 reason 1 dm 3 … pc 0x47dec001 fault 0` → `session lost` → `KWL EXIT … error=5`、greeter に戻る（build/review/vf2-desktop/shot3.png、greeter は正しく描かれる）。terminal は `terminal_window_open … errno=6`（compositor が居ない）。
- d3（P1 の timing、52353d85d）: 同じ lockup。1 回は terminal の起動の時に板が自分で再起動し、log は残らなかった（原因は未確認）。
- d4（f046eed60）: login の直後（terminal なし）に `context 0x4 reason 1 dm 3 pc 0x41db7000 fault 0x8009ae2500`。timing: job の待ちは 3D が 1 job 約 37 ms（TA は 1.6 ms）、1 submit に約 2.4 job → P1 q1524。
- vkmatrix 22/22（sample-linear-65・sample-linear-1180 を含む、e71afc04c）: linear の sampling 自体は通る → lockup は import の memory・blur・大きな draw など compositor に固有の所（P2 q1523-i02）。

### 板の sample-linear PASS の後（2026-10-21、P2）

- 板: vkmatrix 22/22（sample-linear-* も PASS）。linear の sampling そのものは動く。
  desktop の lockup の fault 0x8009ae2500 は general heap（0x8000000000〜）の中にある。
- 見つけた穴: powervr の handle の破棄（`render.c` の `resource_unmake`）は、session の job が動いている間でも
  すぐに GPU の mapping を外し、その VA を空間へ返していた（job の参照による延命が無い）。
  compositor が client の buffer の import を、それを読む最後の frame の完了の前に捨てると、3D は外れた page を読む（fault あり）。
  返した VA に別の mapping が入ると、別の中身を読む（fault 0）。どちらも lockup の形に合う。
- 修正: session に job が残っている間（outstanding か pending_count が 0 でない）は、handle を mapping のまま deferred の list に置く。
  session の次の handle の操作（make・unmake）で、job が無ければ外す。session の破棄では残りを全部外す。
  `render.c` の `resource_unmake`・`resources_drain`・`resource_release` と、`render-private.h` の `deferred`。
- 確認:
  - VF2 の vmunix は rc 0、warning 0。
  - render.c を通す host 試験は無い（未実施）。
  - 板は未実施。
  - 別の process の export を import して sample する vkmatrix の case は作っていない。
- d5（P1 e4748857a、1 job に 64 draw、170ce98fe）: 1 submit ≈ 1 job、submit の平均 57 ms（前 108 ms）、3D 1 job 44 ms、transfer 32 回 平均 5.6 ms。KWL の rec submit は 171〜176 ms/frame のまま。
- d6（P2 c5a00a106 の deferred unmap + P1 render-linear、0fa1d8ecb）: vkmatrix 23/23（render-linear: PBE の linear の store が 1920x1080 で正しい → q1525 の前提が成立）。compositor の lockup は残る: `context 0x7 reason 1 dm 3 pc 0x41e1b000 fault 0x8009ae2000`（d4 と同じ辺りの address）→ P2 q1523-i03 で fault の address の持ち主を log に。

### q1523-i03（2026-10-21、P2）: fault の address の持ち主を log に出す診断

- 板の run 6: deferred の後も lockup は残った。fault は 0x8009ae2000 と 0x8009ae2500 で、boot をまたいで同じ所に出る。
- 診断: session の生きている handle を全部、core の lock の下で list（`all`・`next_all`）に持つ。各 handle には作った関数の戻り番地（maker）、
  host・shared・blob・deferred を記録する。context reset の通知（`render-queue.c` の `handle_notice`、名指しの session）で
  `powervr_session_describe_fault` が次を出す:
  - fault を含む handle（範囲、大きさ、end からの距離、各 flag、maker）;
  - 含む handle が無ければ、すぐ下と上の handle（end の後ろを読んだかが分かる）;
  - general heap の fixed area。
  - maker は vmunix の `nm`・`addr2line` で関数に引く。
  - render job の buffer、render target と free list、region header、vk の memory はどれも handle なので、この list で覆う。
- 確認: VF2 の vmunix は rc 0、warning 0。host 試験は無い。板は未実施。

### q1523-i04（2026-10-21、P2）: fault 0 の lockup の手がかり

- run 7: fault 0 の 3D lockup（pc 0x50b1e001）。fault の address が無いので describe は出なかった。
- reset の pc（0x41db7000、0x41e1b000、0x47dec001、0x5055d001、0x50b1e001）は、どれも 0x4000_0000 からの DRAM の物理の範囲にある。
  program の window（PDS・USC の heap の先頭近く）の address ではない。
  そこで reset のたびに、pc（下の 2 bit を落とす）を物理の address と見て、その page を持つ live handle を出す
  （`powervr_session_describe_physical`、`render-queue.c` に 1 行の呼び出し）。
- compositor の効果を切る設定（`userland/desktop/wayland/settings.c`、key は `settings-keys.c`）:
  - `~kei/.config/keiland/desktop.conf` に `window.frosted=0`（panel を不透明にする）と `window.opacity=100`（window の半透明を切る）。
  - blur は CPU が壁紙から一度だけ作る小さい画像で、GPU の blur pass は無い。GPU は linear filter でそれを sample するだけ。
  - 影と角丸を切る設定は見当たらない。
- 未実施: 失敗した render job の draw ごとの記録（P1 の render-job.c に job の要約を持たせる必要がある）。
- 確認: VF2 の vmunix は rc 0、warning 0。板は未実施。

### q1523-i05（2026-10-21、P2）: linear image の末尾の先を transfer が読む

- run 9 で特定できた。fault 0x8009ae2fc0 は、1920x1080x4 = 8294400 byte の host の handle（vkAllocateMemory）の end から 4032 byte 先。
  失敗したのは transfer（dm 3）。swapchain の native image（linear）を source か destination にした transfer が、
  16 行の tile 単位で 1080 行目より先（1080 は 16 の倍数でない）に触れたと見る。
  copy の program は unnormalised の座標で sample するので、clip の外の pixel も linear の source の外を読みうる。
- 修正: linear image の memory（`powervr_image_lay_out` の bytes）を 32 行の倍数まで延ばす（`POWERVR_IMAGE_LINEAR_ROWS`）。
  row と layer の大きさ（layer_bytes）は変えない。vkGetImageMemoryRequirements と subresource の size が延びる。
- 確認:
  - p010 の host 試験は 0 failed。image-host-test の linear の大きさの期待は 32 行に直した。
  - p009 は切り替え 0・1 とも 505 passed。frame-host の 100x10 linear の size の期待を 416x32 に直した。
  - VF2 の vmunix は rc 0、warning 0。
  - 板は未実施。
  - guard 付きの vkmatrix の case は作っていない。lavapipe では越境を検出できない。
- 残り: app の VkBuffer を linear の transfer 面にする copy（image↔buffer）は、buffer の大きさを driver が延ばせない。
  そちらで越えるなら、transfer の側で、source の座標を clamp するか、最後の tile 行を分けて書く必要がある。
- d8（q1526-i02、1399a25da）: `visionfive2: ccache at 0x2010000, cache maintenance on`、present 11.7 ms。ユーザー（HDMI）「キャッシュの挙動が消えました。」
- d9（P2 の pc・fault の記述、9f0fdcf43）: `fault 0x8009ae2fc0 … above handle 0x80092f9000..0x8009ae2000 (8294400 bytes, past end by 4032) maker run_stream`、`transfer: fail` → 1920x1080 の linear の swapchain の image の末尾を transfer が越えて読む（16/32 行の tile）。P2 q1523-i05（7b97d66a5）で linear の image の memory を 32 行の倍数に。
- **d10（P1 q1525 の linear の直接 scanout、3e530b10a、2026-10-21 07:37）: Keiland の desktop に Settings（welcome）と Terminal の window が表示され、terminal の prompt `kei@kei:/tmp$` が出た（build/review/vf2-desktop/shot10.png・shot11.png）。context reset 0。KWL PERF rec: present 1〜3 ms（前 10 ms）、copied=0、submit は 15〜300 ms（重い frame で遅い）。**

### q1523-i06（2026-10-21、P2）: transfer の座標の clamp と端数の大きさの試験

- transfer の copy と blit の program は、FMAD で作った source の座標を MINMAX（F32）で clamp する。
  範囲は source の矩形の端の texel の中心で、x は [x+0.5, x+w−0.5]、y も同じ。
  - tile の全体を shade しても、destination の矩形の外の pixel が、linear の source（buffer・linear image）の memory の外を読まない。
  - 境界の値は shared の 12〜15 に置き、`POWERVR_TRANSFER_SHARED_WORDS` は 12 から 16 にした。program の中で temp の 8〜11 に移してから使う。
  - destination の側の書き込みは、従来どおり PBE の clip（矩形の中だけ）に任せる。
- vkmatrix: copy-1080（1920x1080 の RGBA8 を、ちょうどの大きさの buffer から OPTIMAL へ入れて戻す）と copy-65（65x3）を足した。
  wide の case の大きさは `wide_width`・`wide_height` から取るようにした（既定は 64x64）。
- 確認:
  - p010 の host 試験は 0 failed（copy program の組み直しの期待に、limit の移動と MINMAX の 4 つを足した）。
  - p009 は 0・1 とも 505 passed。
  - vkmatrix は lavapipe で 25/25。
  - VF2 の vmunix と bin/vkmatrix は rc 0、warning 0。
  - 板は未実施（MINMAX の transfer での実機の動作も未確認）。

### q1523-i07（2026-10-21、P2）: copy-65 の 1 行目以降

- run 12: copy-65 は row 1・2 が書かれなかった（0xa5 のまま）。copy-1080 は PASS。
- 原因: 奇数の行（65 texel）の linear destination は、1 行ずつの job で書く（`powervr_transfer_needs_rows`）。
  その job は surface 全体のまま、area の y だけを変えていた。PBE の line stride は 2 pixel 単位なので、(65−1)/2 で 66 texel の行になる。
  row 1 以降は、ずれた所（y×66×4）に書かれていた。
- 修正（P1 の `transfer-job.c` の `powervr_transfer_run`、最小）: copy の行の job では、destination を、その行の address で始まる 1 行の surface にし、area の y を 0 にする。
  blit は従来どおり（linear image の行は偶数に揃えてあるので、奇数の行にならない）。
- 確認: p010 は 0 failed、p009 は 505 passed、VF2 の vmunix と bin/vkmatrix は rc 0、warning 0。板は未実施。
- d11（f72c803b4、i05 の padding まで）: login の直後に lockup（pc 0x4f25b001 fault 0）。d12（96192f4b3、i06 の clamp）: vkmatrix 24/25（copy-65 FAIL → i07）、terminal が出て reset 0。d13（8a8e32abd、i07）: vkmatrix 25/25、reset 0（display は `frames copied (zero copy off for the lease)`）。
- d14（P1 の 594 MHz の使い捨て、78975419c 基）: wait 3d 24〜40 ms（396 MHz で 37〜44 ms）と明らかな差なし、login で lockup 1 回 → 396 MHz のまま。
- d15（63445e412、P2 の late vblank start 8b1edfa7c）: `display scanout 1`、`zero copy 1`、だが `frames copied (controller not ready)`、`dc8200: vblank 0 …`（IRQ 95 が数えない）。**lockup が再び出た（pc 0x4dc12001 fault 0）→ i06・i07 の後も残る。d12・d13 はたまたま。** P2 は lockup を最優先（q1523-i08）、次に vblank、cursor plane は後。
- d16（7df2eca73、P2 の DC8200 の cursor plane eb3836d67、2026-10-21 08:1x）: `KWL CURSOR_PLANE open plane=1 width=64 height=64 images=3 format=37` — Keiland が hardware cursor を使う。display は `frames copied (controller not ready)` のまま（vblank が数えない）。滑らかさはユーザーの目視待ち。


### q1523-i08（2026-10-21、P2）: render job の記録（fault 0 の lockup 用）

- run 15: i06・i07 の後も、login の直後に fault 0 の 3D lockup が出た（d11・d14・d15）。
- 診断:
  - session ごとに、最新の render job の要約（`struct powervr_job_note`、render-private.h）を持つ。
    中身は area、colour target（address・format・大きさ・pitch・padded・linear・load）、draw ごとの pipeline と scissor、texture の image state の 2 word と sampler の word 0（各 set の image binding の最初の要素、draw ごとに 4 つまで）。
  - reset が session を名指したら、`powervr_session_describe_job` がそれを出す。texture は種類・format・大きさ・stride・address に解いて、address を含む live handle と、strided の行の末尾が handle の end をどれだけ越えるかを出す。
  - 呼び出し: render-job.c の `powervr_render_job_run` の、job_draws が成功した直後の if の中に 1 行（`powervr_session_note_job`、P1 の q1528 と衝突しないよう 1 block）。render-queue.c の `handle_notice` に 1 行。
  - p010 の job-host-test に stub を 1 行足した。
- (B) vblank の数が 0 のまま: `drv_powervr_dc_vblank_report` が、controller の interrupt enable・pending、panel の config、scan の位置を 2 回出す。late の vblank start の直後にも出す。
  pending が立つのに handler が来ないなら PLIC 側、pending が立たないなら controller 側。
- 確認: p010 は 0 failed、p009 は 505 passed、ws277 の host 試験は全部 0 failures、VF2 の vmunix は rc 0、warning 0。板は未実施。
- d17（f490a65b8、P2 の job・vblank の診断）: `dc8200: interrupt enable 5 pending 1` で handler が走らない → aedded233 で IRQ 95 の unmask。lockup は出ず。
- d18（e4bdff188、P1 の非同期の submit stage A 2e3e67418）: SD の I/O の失敗（`ufs … refuses writes … error 5`、同じ address の segfault が複数）で信用できない。gpu-selftest の image が `linear sampled` で fail（vkimage の期待が古い → P1）。TA の lockup の job は 41 draw、texture は全部 handle の中。
- d19（b754f95b9、非同期 + unmask）: compositor の起動ごとに 3D の lockup（context 0x1a・0x1f・0x23、`job 2 … (load draw first)`、色は linear の swapchain、load 2）。vblank は 0 のまま、`display flips 26`。→ P1 に非同期の切り替え（既定 off）と競合の調査、P2 に vblank。
- d20（7be88f048、P1 981367dec で同期の submit を既定に、vkimage の期待を直した）: 1 回目は SD の I/O の失敗で kernel の入れ替えが止まり d19 のまま（`tail: Input/output error`）。電源を入れ直して d20 で起動。**同期でも 3D の lockup が 3 回、全部「load draw first」（load 2）の 1080p の job**（twiddled の target でも linear でも）→ 共通は全画面の LOAD の draw（同じ attachment を sample する）。P1 に最優先で依頼。SD の read の失敗は 86 回（BUG-343 が重くなっている）。
- d21（9ed641cf4、P2 の PLIC の診断）: source 95 priority 1、boot の hart の context 2 で enable 1・threshold 0（経路は正しい）。DC も PLIC も pending 0、location 0 → DC が vblank の割り込みを上げていない（P2）。
- **d22（ba13a06fa、P2 208ac2e70: report が 0x10 を読まない、claim の complete）: `dc8200: vblank 1 other 1`、`powervr: display lease 1: frames scanned directly` — DC8200 の zero copy の flip が動いた。** 原因は診断の read が割り込みを消していた事。
- vkmatrix load-1080-6・load-1080-32・load-1080-linear（P1 50c4dee12、各 60 pass）: 全部 PASS、reset 0 → LOAD だけでは lockup は再現しない。d20 の失敗の job は LOAD + 文字の atlas（1024x256）・1024x1024 の texture の compositor の pipeline の 32 draw。P1 に draw の二分探索の切り替えを依頼（q1530）。
- d23（2eb705201、P2 の vblank の整理 bb98e59d5）: `dc8200: vblank 470`（数え続ける）、`frames scanned directly`。login の直後に lockup 1 回。
- **lockup の再現（2026-10-21 09:1x、47d26b51a、P1 の b07ea4a73）**: vkmatrix load-1080-textured は単独で PASS。draw の上限の sysctl（`hw.gpu.powervr.stages=N`、試験だけの一時の例外、Q1 承認、原因が分かったら外す）と `service restart greeter`（autologin し直す）での二分探索は決まらなかった。**3 つの vkmatrix を別 process で同時に流すと load-1080-textured が context reset で device lost（`context 0x81 reason 1 dm 3`）** → 複数の session が同時に 3D を使うと lockup。desktop の login の直後（compositor・settings・files が同時に描く）に集中する事と合う。P1 に session 間で共有している物（region header の固定の window、free list、固定の区画など）の調査を依頼。
- d24（a687d788b、P1 f9aa75bc9 で firmware の context switch を off、mainline と同じ flags）: login までに reset 3、同時の 3 つの vkmatrix を 3 回 → 通過 2・2・1、reset 3→7。context switch は原因でない。P1 は session 間で共有している物へ（q1530-i02）。P2 の SD の直し（eacefb94e、recovery の全 reset・5 回の retry・1 block ずつの fallback）を merge。

### q1532-i01（2026-10-21、P2）: context reset の時の firmware の記録

- 新しい file `src/drivers/gpu/powervr/firmware-hwr.c` の `powervr_hwr_dump`（weak）。`CONFIG_POWERVR_HWR_DUMP := y` の kernel にだけ入る（`platform/riscv64/vmunix.mk`）。
  desktop の試験の config `plan/ws270/tests/config-visionfive2-desktop.mk` で on にした。
- 出す物（Linux v7.3 の rogue_fwif_hwrinfobuf の並び）:
  - HWR の counter と write index;
  - data master ごとの locked up・overran・recovered・false の数（0 でない物）;
  - 埋まっている各 record: number・dm・type（ROGUE_HWRTYPE）・flags・event status・frame・active HWRT data・core、hwr_data の 4 word（BIF・MMU・poll の情報）、kick・reset の開始と終わり・free list・timer の時刻;
  - trace の最後の 256 word（`powervr: dump: trace` の形で、`plan/tools/vf2/sfdec.py` がそのまま読める）;
  - reset された context の firmware の record（render・transfer・compute の別、raw、最大 96 word）。
- 呼び出し: render-queue.c の `handle_notice` の、名指しの session の block で `powervr_session_describe_job` の直後に 3 行（`if (powervr_hwr_dump != NULL) powervr_hwr_dump(...)`）。
- 確認:
  - desktop の config で VF2 の vmunix は rc 0、warning 0（firmware-hwr.c が build に入る）。
  - selftest の config（switch 無し）でも rc 0、warning 0。
  - 板は未実施。
- d25（3c347a062、P1 の `vkmatrix -p N -r R`、P2 の SD の直し）: SD の read の失敗は retry で通る（errors 29、止まらない）。同時の試験の途中で板が log 無しで再起動（2 回、原因未確認、SoC ごと止まり watchdog か）。
- **d26（61002a373、P2 の HWR の dump 37645e247）: `vkmatrix -p 3 -r 3 load-1080-textured` → FAIL。`hwr: record 0: number 1 dm 3 type 3 … hwrt 0xc0129000`、`data 10650000000000 d001 58ae9001 0` → type 3 = BIF0FAULT（GPU の MMU の fault）、notice の「pc」は page catalogue の address（0x58ae9001）。複数 session の同時の描画で MMU の文脈が衝突する疑い（page_cat_base_reg_set を全 session で共有、または MMU cache の invalidate）。P1 に修正、P2 に decode を依頼。**

### q1532-i02（2026-10-21、P2）: BIF fault の解読と page catalogue の名

- run 26 の record（type 3 の BIF0FAULT、`data 10650000000000 d001 58ae9001 0`）を、vendor の rgx_cr_defs_km.h（XE MEM）と rgxdebug.c の tag の表で解くと次のとおり。
  - address 0x0、read、tag 0x25 = IPF CREQ（ISP の parameter fetch の control stream の要求）、side band 1。
  - mmu status は fault 1、catalogue base 13、page size 0。page catalogue は 0x58ae9001。
  - つまり 3D の ISP が region header か control stream の address 0 を読んだ。
- HWR dump は、type 3〜5（BIF の fault）の record に、上の解読の 1 行（`bif fault at …, read by tag N (name) …; page catalogue …`）を足す。
- notice の行の "pc" は page catalogue の物理 address なので、log の名を `page catalogue` にした。`powervr_session_describe_physical`（shader の address と見て探していた）は消した。
- session を作る時に `powervr: session context 0x… memory context 0x… page catalogue 0x…` を出す。fault の page catalogue から session を引ける。
- 確認: desktop の config で VF2 の vmunix は rc 0、warning 0。p010 の host 試験は 0 failed。
- d27（3f8a885bd、P2 0ac34cbd9 の decode）: 3 session が同じ形で fault（`bif fault at 0x0, read by tag 37 (IPF CREQ) … catalogue base 13`、page catalogue はそれぞれ別）。session 0x1 と 0x2 の FW の memory context が同じ 0xc002b000。試験の途中で板が止まり（No route to host）、電源の入れ直しで戻った。
- d28（7d69d8479、P1 7c5c31f0b の target の診断）: fault の page catalogue 0x5a20a001 は session の catalogue（0x4e03d000）でも target の record の PM の catalogue（0x5a21e001・0x5a221001・0x5a224001）でもなく、それらの近く → 別（解放済みか）の HWRT の PM の catalogue で ISP が読んでいる疑い。P1 に PM の catalogue の確保・解放を調べるよう依頼。
- P2 の読み（q1532-i03、code は変えない）: (1) render job は毎回 HWRT の half 0 だけを使い、TA(N+1) を 3D(N) の後に並べる待ちが無い（Mesa は rt_data を交互にし、前の fragment を待つ）→ 走っている 3D の IPF が作り直された PM の catalogue を辿る、と run 28 に合う。(2) free list を伸ばさない（notice_grow が何も返さない）ので同時の 1080p で partial render に落ちる。(3) target の解放の順は正しい。(4) memory context の address の重複は再利用なら正常、ただし古い session の全 context の cleanup を確かめる。→ P1 に (1) から。
- d29（22c0c673e、P1 680f339b0 で DM の overlap を止めた、system flags 0x00080010）: login は reset 0。同時の試験は FAIL（reset 2、3 回目で板の SSH が切れた）。**fault の PM の catalogue は 4 件とも fault した session 自身の local free list（32 page）の中** → session 間の取り違えではなく、その HWRT の PM の表が 3D の途中で作り直される、または local list が足りず grow を断って partial render に落ちる疑い。P1 に grow の log と実装、HWRT の TA を前の 3D の後に並べる事を依頼（q1530-i04）。
- d30（f2c8a64a1、P1 679e119bb: geometry が前の 3D を firmware で待つ、grow・partial render・OOM の log）: login は reset 0。同時の試験は 3 回とも FAIL（3 つのうち 2 つ）、grow・partial render・OOM の行は 0 → 原因は PM の state の保存と戻し（別 context の TA が間に入る時、HWRT ごとの mlist・vheap の表など）の疑い。P1 に Mesa の rt_data と項目ごとに照らすよう依頼。
- d30 追試: `vkmatrix -p 2 -r 10 load-1080-6`（texture 無し）でも FAIL → 最小の再現。1 process の push4 は PASS。
- d31（fe34c86a7、P1 6d1e187c1 で session ごとに render target の address をずらす、vkmatrix -p の出力の直し）: login は HWR 0、`-p 2 -r 10 load-1080-6` は 3 回とも FAIL（HWR 2→3→4）→ address の cache の取り違えではない。P1 は half の交互と per-half の macrolist、render context の TA・3D の state の保存先を調べる（q1530-i06）。
- d32（P2 の使い捨て 7a71c351c、dust の電源を host が保つ、system flags 0x00080000）: 試験の途中で SSH が 2 回切れ、3 回目も FAIL → 電源の管理は原因でない、main に入れない。
- **d33（c6407c84f、P1 403974796 の catalogue の比較）: `-p 2 -r 10 load-1080-6` FAIL。fault の catalogue 0x5cd0a001 = kick の前の TE の catalogue、今の TE は 0x5ccef001 → 私たちの 3D が、私たちの TA が置き換えた前の render の TE の表を辿っている。** P1 に half の交互（per-half の macrolist・region header）と fragment が自分の geometry を待つ事を依頼。P2 に trace（runs/d33/capture.txt、1500 行）の解読を依頼。
- **d33 の firmware の trace（P2 q1532-i05 の解読、build/uat/vf2-desktop/runs/d33/trace-decoded.txt）**: 通常は TA の終わりに「FL different between TA/3D: global:0x1, local:0x2」→ 3D の前に Store/Load Freelist と「Loading stack-pointers … MLIST ALIST」→ 3D は新しい表。失敗の時は、私たちの TA の前に別 session の 3D の transfer（Kick 3D TQ）が入り、TA の終わりに「FL different … 0x0/0x0」→ 引き継ぎを飛ばし、fence だけの DM3 の命令の後の 3D が古い TE（0x5cd0a001）で fault。別の TA・電源・context switch は無い。どの session も free list が同じ GPU の address（local 0x8002063000、global 0x800205e000）。P1 に (1) free list の address を session ごとに変える、(2) 3D の前の fence だけの命令を調べるよう依頼。
- **d34（9b35cf3fd、P1 562c6550c: render が target の 2 つの half を交互に使う、Mesa と同じ）: lockup が直った。** `vkmatrix -p 2 -r 10 load-1080-6` 2 回・`-p 3 -r 3 load-1080-textured` 1 回が全部 PASS、HWR 0。desktop で Settings・Terminal・Files の window を開いたまま vkmatrix 29/29、vkcoherency PASS、HWR 0（build/review/vf2-desktop/shot34.png）。gpu-selftest の 2 つの fail は present の段で、desktop が display を持っているため。
- 原因（d33 の trace と P1 の読み）: 毎回 half 0 だけを使っていたので、別 session の 3D の transfer（TQ）だけが間に入ると、firmware が「TA と 3D の free list・render target data が同じ」と見て 3D の前の PM の stack pointer の読み込みを飛ばし、TA が書き換えた前の TE の表で 3D が走った。Mesa は rt_data を交互にする前提。

### q1534-i01（2026-10-21、P2）: 奇数の行の linear への copy を 2 job に

- 以前（cf79f8703）は、奇数の行（PBE の line stride は 2 pixel 単位）の linear destination への copy を、1 行に 1 job で書いていた。
  1180x800 の copy out なら 800 job になる。
- 変更（Mesa の pvr_double_stride と同じ考え）:
  - copy は 2 job。偶数の行と奇数の行で、それぞれ line stride を 2 倍にし、高さを半分にする。
  - 各 job は source の 1 行おきを読む。`powervr_transfer_shared_words_rows`（transfer-program.c）が y の scale 2 と offset を作り、clamp は全体の高さで取る。
  - blit と clear は従来どおり 1 行ずつ。
- Keiland の shm（`userland/desktop/wayland/shm.c`）:
  - commit のたびに、client の傷んだ行を CPU で linear の staging に写す。
  - その後 `image_copy_record` が、staging から sampled image へ image の全体を 1 回 vkCmdCopyImage する（傷んだ行だけではない）。
  - 傷んだ行の範囲に絞れば、transfer の大きさが減る（Keiland の変更、未実施）。
- 確認: p010 の host 試験は 0 failed（transfer-program 33 passed、1 行おきの shared words を足した）。desktop の config で VF2 の vmunix は rc 0、warning 0。板は未実施。

### q1534-i02（2026-10-21、P2）: Keiland の shm の copy を傷んだ行に絞る

- `struct kwl_import`（compose.h）に、書かれた行（dirty）と、取られてまだ写していない行（copy）の範囲を持たせた。
- 書き込みの印:
  - `kwl_host_image_written_rows(import, first, last)`: 行の範囲を、まだ取られていない範囲と合わせる。
  - `kwl_host_image_written` は全部の行（glass などは従来どおり全体）。
  - shm の `surface_upload` は、傷んだ行（大きさが変われば全部）だけを印す。
- frame の取り込みと copy:
  - `kwl_host_images_take` は dirty を copy の範囲へ合わせて、dirty を空にする。
  - `image_copy_record` は copy の範囲の行だけを vkCmdCopyImage し（空なら copy しない）、範囲を空にする。
  - 作ったばかりの image は全部の行を写す。
- 確認:
  - `plan/ws213/tests/list-replay-host-test.sh` は PASS（shm.c の vkCmd の数は 4 のまま）。
  - desktop の config で VF2 の `bin/wayland` は rc 0、warning 0。
  - 板（KWL PERF の比較）は未実施。
- d35（ea331254e、非同期の submit と DM overlap を on）: 同時の試験 PASS・HWR 0、vkmatrix 29/29、vkcoherency PASS、vkreset PASS（5 s、`no progress for 5000 ms`）、gpu-selftest 13/13 → on のまま。
- d36（41273725f、P2 の奇数幅の copy・Keiland の shm の damage の copy）: timing の submit 2.5 ms（非同期が効く）、job の待ち 76〜262 ms、KWL の rec submit 78〜300 ms/frame → GPU の実行時間が上限。P1 q1535 で原因を測る。
- d37（3af4571d6、P1 の perf の case、SLC bypass は今まで通り）: 板の vkmatrix は perf の時間の行を出さない（host では出る、原因未確認、a6f76d321 で整数の表示にしたが出ず）。kernel の timing で代わりに: 1080p の clear だけの job で約 20〜40 ms、LOAD 約 21 ms、平らな quad 約 20 ms、texture の quad 約 42 ms（desktop の job も混ざる粗い値）→ 描画の中身より土台（tile・ISP・PBE・memory）が遅い。
- d38（8e7b784f8、P1 1db3be24d で device-local の memory を SLC に通す）: 試験の途中で板が log 無しで再起動（2 回、kernel.log.old が途中で切れる）→ Q1 が POWERVR_DEVICE_MEMORY_CACHED を 0 に戻した（main）。P1 に原因（SLC の flush の command か解放の順）を調べるよう依頼。


### q1536-i01（2026-10-21、P2）: compiler と Mesa pco の比較

- Mesa 26.2.5 pco（host、drm-shim、`PCO_DEBUG_PRINT=all,stats`）で、Keiland の shader の組を pipeline にして数を取り、こちらの compiler（`shader-probe`）と比べた。
  Mesa の側の道具は scratchpad の pcostats.c（repository の外）。

| shader | Mesa の code byte | Mesa の temp | Mesa の group | こちらの code byte | こちらの temp（前 → 後） |
| --- | --- | --- | --- | --- | --- |
| quad.vert | 232 | 10 | 21 | 440 | 22 → 13 |
| quad.frag | 720 | 32 | 67 | 640 | 20 → 19 |
| panel.vert | 296 | 11 | 27 | 648 | 26 → 20 |
| panel.frag | 4128 | 24 | 393 | 5208 | 55 → 55 |
| batch.vert | 168 | 4 | 16 | 848 | 38 → 32 |
| batch.frag | 936 | 13 | 83 | 1144 | 25 → 24 |
| turn.vert | 216 | 5 | 19 | 520 | 21 → 15 |
| panel.frag[0..7] | 880〜1688 | 13〜32 | 83〜153 | 816〜1984 | 21〜31 → 20〜30 |

- 読み:
  - fragment の code はどれも Mesa の 0.9〜1.3 倍。temp は quad.frag と panel.frag[5]・[6] で Mesa より少なく、ほかは 1〜2.3 倍。
  - 実行が 10〜30 倍遅い理由を、compiler だけでは説明できない。
  - 差が大きいのは次の 2 つ:
    - panel.frag の temp（55 と 24）;
    - vertex の temp と code（2〜8 倍。vertex の入力を temp に移している見込み）。
- 修正（`shader-backend.c` の `compact_temporaries`）:
  - back end が固定で取っていた 9 個の temp（0〜8）のうち、program が使わない block を返す。block は EMC 0、scratch 1〜3、座標・address・store data 4〜8（store は address の対の次の register を読むので、4〜8 は分けない）。
  - 値の temp は詰める。temp の register の場所の値だけを変え、動作は変えない。
- 確認:
  - compiler の host 試験は 411 passed と 22 passed で、0 failed。
  - p010 は 0 failed、p009 は 505 passed。
  - desktop の config で VF2 の vmunix は rc 0、warning 0。
  - 板の vkmatrix は未実施。
- d39（cd107fa41、SLC は bypass のまま、P2 の temps の詰め）: vkmatrix 33/33。perf: 1080p の clear 3.85 ms、LOAD 23.5 ms、平らな全画面の quad 20.8 ms、texture の全画面の quad 43.25 ms → 土台は速く、fragment の shader の実行が約 10 倍遅い（約 120 Mpixel/s）。`-p 2 -r 5 load-1080-6` は `no progress for 5000 ms`（watchdog の誤検出、HWR 無し）で FAIL → P1 に watchdog の見直し。P1・P2 に fragment の道（PDS の pixel program・USC の task・instance の数）を Mesa と比べるよう依頼。
- P2 q1536-i02（報告だけ）: clear だけの 1080p の 3D の command を Mesa（drm-shim、BVNC 36.50.54.182）と比べ、usc_pixel_output_ctrl・isp_ctl・ZLS・PBE・tiles in flight・partitions が同じ。違いは isp_bgobjvals（0x3ff と 0x300、stencil 無しなら無関係）と DISABLE_PIXELMERGE（Mesa は立てる）だけ。d39 の clear 3.85 ms と合う → 遅いのは draw（fragment の shader の実行）の側。
- d40（28d74195c、P1 b88dc3975: watchdog は全 session の timeline が 5 s 動かない時だけ）: `-p 2 -r 5 load-1080-6` PASS、`-p 3 -r 3 load-1080-textured` PASS、`no progress` 0、HWR 0、vkreset PASS（hang 5059 ms で DEVICE_LOST）。
- P1 の読み（driver 側、commit なし）: pixel の task（DOUTU）と draw ごとの PPP・ISP の state は Mesa と同じ。残るのは fragment の program の長さ: vkmatrix の push4.frag（定数の色だけ）が 288 byte・temps 13、texture.frag が 336 byte・19。約 36 命令/pixel × 1080p ÷ 16 lane ≈ 12 ms at 396 MHz で、測った +17 ms に近い。P2 に Mesa pco との比較と短縮、Keiland の quad.frag・panel.frag の重さの判断を依頼。


### q1536-i02（2026-10-21、P2）: 画素ごとの仕事を減らす

- Mesa（BXE-4-32、`PVR_SHIM_DEVICE_BVNC=36.50.54.182`、`PVR_DEBUG=cs`）と命令を比べた。
  - 3D の command（usc_pixel_output_ctrl 0x1ff8、isp_ctl 0x80033000、isp_aa 0、ZLS 無し、PBE の word）は、こちらと同じ。
  - draw の PDS state も同じ形。違いは 2 つで、Mesa は depth を書かず比較が always の時に pds_tri_merge_disable（SIZEINFO2 bit 14）を立て、pds_tempsize は 0。
  - vkmatrix の push4.frag は Mesa で 312 byte・9 temp・28 group、こちらは 288 byte・13 temp・24 op。
- 変更:
  - `render-draw.c`: depth を書かず比較が always の draw に tri merge disable を付けた（Mesa の pvr_setup_triangle_merging_flag と同じ条件）。
  - `shader-spirv-interface.c`: 正規化した attachment の色の clamp（channel ごとに MINMAX 2 つ）をやめた。
    - blend が無い時は clamp しない。pck.scale.u8888 が飽和するので、Mesa の pco も clamp していない。
    - blend の後の和も clamp しない。
    - blend の前の source の clamp は残す（Vulkan の規定）。
- 効果（host）:
  - push4.frag は 24 op → 16 op（288 → 168 byte）。
  - texture.frag は 33 → 25 op（336 → 216 byte）。
  - Keiland の quad.frag は 640 → 520 byte、panel.frag は 5208 → 5088 byte。
- 確認:
  - compiler の host 試験は 411 と 22 passed。
  - p010 は 0 failed。
  - desktop の config で VF2 の vmunix は rc 0、warning 0。
  - 板は未実施（perf-flat・perf-texture、vkmatrix の色の試験）。
- P2 q1536-i02（4a1c53e4b・06f2bcf9f、373365bb9 で merge）: depth を使わない draw で tri merge を止める（Mesa と同じ）、blend 無しの時の色の clamp を外す（pack が飽和する）→ push4.frag 24→16 op、texture.frag 33→25 op、Keiland quad.frag 640→520 byte。板は未確認。
- P1 q1537（9b9bf968b の数え、3402d837e の不透明の内側を blend 無しで描く、merge 済み）: 板と PNG の比べは未確認。
- d41（373365bb9 の kernel）: run.sh が 500 s で止まり、その後 2 回電源を入れ直しても SSH に戻らない（2026-10-21 10:4x）。原因は未確認（kernel か SD か）。
- d41 の後: 板が 2 回の電源の入れ直しでも戻らず、3 回目で戻った（kernel は d41、SD の read の失敗 46 回の起動）→ kernel ではなく SD の不安定（BUG-343）と見る。
- d42（85d8e9ae4、P2 の短い shader・tri merge、P1 の不透明の内側、userland を overlay して session を起動し直し）: 画面は同じ（build/review/vf2-desktop/shot42.png）、vkmatrix 33/33、HWR 0。perf: clear 3.8 ms、load 23.35 ms、flat 19.25 ms（前 20.8）、texture 38.15 ms（前 43.25）→ 約 1 割。KWL の rec submit 162 ms。命令数が主因ではない。次の疑い: PDS・USC の program・定数・descriptor を SLC bypass で DRAM から読む事（P1 q1537-i02）、P2 は pixel あたりの固定費と命令あたりの費用を分ける perf の case（q1536-i03）。

### q1536-i03（2026-10-21、P2）: 画素ごとの費用を分ける perf の case

- vkmatrix に perf-immediate・perf-alu・perf-quarter を足した。shader は `shaders/immediate.frag`・`shaders/alu.frag` で、`regenerate.py` で shader.h を作り直した。
  - perf-immediate: push constant 無しの固定の色。こちらの compiler で 15 op。
  - perf-alu: 64 個の依存する乗加算。こちらの compiler で 402 op（定数の即値の移動を含む。flat の 16 op の約 25 倍）。
  - perf-quarter: flat を 960x540 の viewport で。
  - (b) の「最小の program」は immediate と同じなので別にしなかった。
  - (e) の instance の数の上限は、vkmatrix からは変えられない（driver の knob が要る）。
- 確認:
  - lavapipe で 36/36 PASS（perf の行の形は従来どおり）。
  - VF2 の bin/vkmatrix は rc 0、warning 0。
  - 板は未実施。
- d42 の上で P2 の perf の case（1c089ebee、fdcc38ee1 で merge）: clear 3.85 ms、immediate（15 op）15.1 ms、flat 15.55 ms、quarter（1/4 面積）7.2 ms、alu（402 op）268.75 ms、texture 37.95 ms → 1 op あたり 0.32 ns/pixel（約 3.2 G pixel-op/s、ALU は妥当）、命令数に依らない固定費が約 5.4 ns/pixel（約 2 clock）で軽い shader の時間の大半、面積にほぼ比例。固定費は pixel の task の起動（PDS の program・定数の取り込み）の疑い。
- d43（P1 の試し 84eaa2f3b、program と job の general の memory を SLC に通す、main には入れない）: 入れた後に板が戻らない（電源の入れ直し 2 回）。
- d43（P1 の試し 84eaa2f3b、program・job の general を SLC に通す）: 起動は SD の失敗 73 回で不安定だったが動いた。perf は変わらず（flat 15.7、immediate 15.0、texture 37.85、alu 268.55 ms）、vkmatrix 36/36、HWR 0 → program の取り込みは原因でない。KWL の rec submit 108 ms（max 256）。ALU も pixel の固定費も名目の約 1/2〜1/8 → GPU の実際の clock が 396 MHz より低い疑いも。板は main の kernel（55e6638ed 以降、切り替え 0）に戻す（d44）。
- P2 q1536-i04（報告だけ）: pixel の task の起動は Mesa と同じ（DOUTU 1 つ、group は hardware、max_instances_per_pds_task 32）。違いは fragment の program: Mesa は phase change（phase2end）と DOUTU の dual_phase を使う、temps は Mesa 6・ours 12、END が別の group。dual-phase が無いと program 全体が overlap check の待ちを受けるので、命令数に依らない pixel の固定費と合う → P2 に dual-phase の opt-in の build と temps の field を変える knob を依頼（q1536-i05）。

### q1536-i05（2026-10-21、P2）: dual-phase の訂正と、temps の試しの switch

- 訂正: i04 で「Mesa は dual-phase」と書いたのは誤りだった。
  - `phase2end` は TST・MOVC の encoding の bit（命令の group の中の phase 2 の終わり）で、program の phase の切り替えではない。
  - `pco_data.fs.uses.phase_change` は宣言だけで、pco はどこでも立てない。Mesa の fragment の DOUTU の dual_phase は常に 0 で、こちらと同じ。
  - dual-phase の switch は作らなかった。
- temps の試し: `POWERVR_FRAGMENT_TEMPS_FORCE`（render-program.c、既定 0）。
  - draw の fragment の task は、`powervr_render_fragment_kick_build` で少なくともこの数の temp を求める。
  - render-job.c の呼び出しを 1 行替えた。
- 確認: p010 は 0 failed、p009 は 505 passed、desktop の config で VF2 の vmunix は rc 0、warning 0。
- d45（P1 の試し 434ca0566）: `powervr: core clock measured 395651840 Hz` → GPU の clock は 396 MHz で設定どおり。P2 の dual-phase の読みは誤り（Mesa も dual_phase 0）と P2 が訂正。
- d46（fragment の temps を 48 に固定した使い捨て、main の tree で build、commit しない）: immediate 14.95、flat 15.65、alu 268.7、quarter 7.05 ms → temps（同時に動く pixel の数）は無関係。clear（background object も pixel ごとに USC）3.85 ms と全画面の draw 約 15 ms の差は draw の道（TA・ISP の primitive の処理、coefficient の PDS、pixel の出力）→ P2 に PPP・ISP・PDS の語の Mesa との比較と PUNCH_THROUGH の試し、P1 に三角形の数を変える perf と TA・ISP の道。
- d47: 板を main の kernel（91885aec7、knob 0）に戻した。
