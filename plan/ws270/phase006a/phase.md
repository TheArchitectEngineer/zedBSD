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
