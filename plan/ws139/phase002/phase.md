<!-- awesome-plan project=zedbsd record=ws139-p002 -->

# ws139-p002: E2（5330 の host の i915 の Venus）で測る手順と基準値

Status: planned
Disposition: normal
Parent: [WS139](../ws.md)
Queue: none
依存: p001（`perf-run.sh`・`type-only.sh`・`perf-summary.py`・`build-perf-image.sh` が main に統合済み）、ws.md の **U2（5330 の操作の承認と担当）**
調べの上限: 3 h。手順 7 の確かめ（guest の Vulkan の device が Intel）まで届かなければ、そこで止めて uncleared にし、分かったことを記録する

## 範囲と担当

- **入る**:
  - 5330 の Linux（hostname `chaos`、centris からの ssh の alias `solaris10-man`）の上で、p001 の `perf-run.sh` を流す script を作る。
    script は `plan/ws139/tests/e2-run.sh`（centris で呼ぶ）と `e2-remote.sh`（5330 で走る）。
  - E2 の基準値を 2 回取る。
- **担当の分け方**（AGENTS.md: 実装の担当は QEMU を自分で起動しない）:
  - 実装の担当は、script を書き、手順 1 の読むだけの確かめ（U2 で許された範囲）をし、commit するところまで行う。
  - 5330 での実行（手順 3〜8）は、U2 で決めた担当（T1/T2 など）だけが行う。
- **入らない**: product の source の変更、iGPU の passthrough（E3）、AX211、5330 の host の package・kernel・設定の恒久の変更、
  WS035 の共有の道具（`plan/ws035/tests/zdesktop-guest.sh`）の変更。
- **所有する path**: `plan/ws139/`。

## 分かっていること（2026-10-04、記録から。5330 の上では確かめていない）

- 5330 の操作の正本は `plan/tools/hw5330/README.md`。
  - ssh の名前は `solaris10-man`。IP は電源の入れ直しで変わったことがある（`10.0.10.25` ↔ `10.0.30.3`、README の 29 行）。
  - **実機の lock は centris（開発の host）の `/tmp/i915-hw.lock`** である（README の 11 行・111-112 行）。5330 の上の file ではない。
    空きの確かめは、centris で `flock -n /tmp/i915-hw.lock true && echo free || echo busy`（README の 121 行）。
  - iGPU は起動時の既定で vfio-pci。Venus の時だけ `~/bigbang/igpu-mode.sh host` で host の i915 に付け替える。
    - `igpu-mode.sh`（写し `plan/ws031/tests/host/igpu-mode.sh`）は、qemu-system-x86_64 が 1 つでも動いていると、どちらの向きの切り替えも拒む（12・21 行）。
    - `host` は renderD128 が出るのを待ち、自分の利用者に renderD128 と `/dev/kvm` の ACL を付ける（16-18 行）。だから renderD128 は i915 の node になる見込み。
- 5330 で Venus を動かした前例がある: `plan/ws014/tests/venus-qemu.py:180-202`、`plan/ws031/ws.md:173`。
  - 使った物: `virtio-vga-gl`（venus=on、blob=on）、`-display egl-headless,rendernode=/dev/dri/renderD128`、`VK_DRIVER_FILES` に Intel の ICD（前例は `/usr/share/vulkan/icd.d/intel_icd.json`）。
  - `LIBGL_ALWAYS_SOFTWARE`・zink は付けていない。
  - 一方 `zdesktop-guest.sh:34-36` は開発の host 向けに `LIBGL_ALWAYS_SOFTWARE=1` と zink を固定で export する。
    Intel の ICD だけの時は egl-headless が起きない見込み（推測）。だからこの Phase では zdesktop-guest.sh を使わない。
- Venus の renderer（strict queue の virglrenderer の build）は、5330 の `/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install` にある
  （`zdesktop-guest.sh:16-19`）。
- E2 で desktop を測った記録は無い（ws.md の環境の表）。
- 5330 の CPU は i5-1245U。E1÷E2 の比には GPU だけでなく CPU の差も混ざる。

## 手順

### 1. 読むだけの確かめ（実装の担当、U2 で許された範囲）

```sh
flock -n /tmp/i915-hw.lock true && echo free || echo busy          # centris で
ssh solaris10-man 'hostname; uname -r; qemu-system-x86_64 --version | head -1; python3 --version'
ssh solaris10-man 'bigbang/igpu-mode.sh show; pgrep -af qemu-system-x86_64'
ssh solaris10-man 'ls /usr/share/vulkan/icd.d/; ls -l /dev/dri/ /dev/kvm; ls /usr/share/OVMF/'
ssh solaris10-man 'free -g; df -h ~; cat /sys/class/power_supply/*/online 2>/dev/null'
ssh solaris10-man 'ls ~/zedbsd-q306-venus/dependencies/q312-quiesce/install/libexec/virgl_render_server'
```

- 結果を「結果」の表に書く。Intel の ICD の file の名前は、ここで見た物を使う（決め打ちしない）。
- 確かめる物:
  - OVMF が `guest.py` の使う path（`plan/tools/guest/guest.py:371-374` を読んで確かめる）に在ること。
  - memory の空き: guest の 8 GiB と、memfd の 8 GiB（`zdesktop-guest.sh:59`）。
  - home の disk の空き: image の約 2 GB と tree。
- QEMU が動いていれば、他の試験が使っている。止めずに待つか、Q1 に知らせる。

### 2. `e2-run.sh IMAGE OUT`（centris で呼ぶ）の流れ

1. 送る前の確かめ:
   - `git status --porcelain plan/ws139` が空であること（`git archive HEAD` は commit 済みの版しか送らない。未 commit の script は届かない）。
   - `PERF_COMMIT=$(git rev-parse HEAD)` を取る。
2. tree を送る。git の追跡中の file だけ。
   ```sh
   git archive HEAD plan/ws139 plan/ws099/tests plan/ws095/tests plan/ws134/tests plan/ws089/tests plan/ws035/tests \
       plan/ws014/tests plan/tools/guest plan/tools/qmp.py plan/tmp/guest \
     | ssh solaris10-man 'rm -rf ~/ws139-e2 && mkdir -p ~/ws139-e2 && tar -x -C ~/ws139-e2'
   ```
   - `plan/ws014/tests` が要る: `type-latency.py:30-33` と `zdesktop-check.py:20-23` が `venus_rfb.py` を import する。
     無いと P-02 の段が黙って失敗する（起動は通る）。
   - `plan/tmp/guest` は guest の SSH の鍵（git で追跡されている）。
   - path の一覧の根拠: 呼ぶ script の `plan/` への参照を grep したもの
     （`grep -ohE 'plan/[a-z0-9/_.-]+' plan/ws139/tests/*.sh plan/ws099/tests/c5-transitions.sh plan/ws095/tests/type-latency.py plan/ws134/tests/monitor-p003.sh plan/ws089/tests/settings-wait.sh | sort -u`）。
     script を書いた後でこの grep を流し直し、漏れが無いかを確かめて「結果」に貼る。
3. image を送る: `scp IMAGE solaris10-man:ws139-e2/image.img`（SHA-256 を記録）。
4. **lock は centris で取る**。全体を 1 時間で括る。
   ```sh
   flock /tmp/i915-hw.lock timeout 3600 ssh solaris10-man "cd ~/ws139-e2 && PERF_COMMIT=$PERF_COMMIT sh plan/ws139/tests/e2-remote.sh"
   ```
   lock を持つ間（約 45 分）は、passthrough の試験が待つ。U2 の承認に含める。
5. 持ち帰る: `mkdir -p OUT && scp -r solaris10-man:ws139-e2/out-1 solaris10-man:ws139-e2/out-2 OUT/`。
6. 5330 の `~/ws139-e2` を消す（自分が作った物だけ）。

### 3. `e2-remote.sh`（5330 で走る）の流れ

1. 後始末の trap を最初に置く。順は次のとおり（失敗の時も必ず通る）。
   1. `GUEST_RUNTIME=$HOME/ws139-e2/run python3 plan/tools/guest/guest.py stop`（10 秒待ってから kill する、`guest.py:334-348`）。
   2. `pgrep -f ^qemu-system-x86_64` が、自分の起こした pid を含まなくなるまで、最大 30 秒待つ。
   3. `~/bigbang/igpu-mode.sh vfio` と、`~/bigbang/igpu-mode.sh show` の結果を `out-1/igpu-after.txt` に記録する。
      ここで vfio に戻らないと、以後の passthrough の試験が全て止まる。
2. AC の確かめ: `/sys/class/power_supply/*/online` のどれかが 1 であること。電池なら止める（BUG-159: 電池では描画が約 5 fps に落ちる）。
   CPU の governor（`/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`）も記録する。
3. `~/bigbang/igpu-mode.sh host`。失敗したら止める（trap が後始末する）。
4. guest を起こす。`zdesktop-guest.sh` を使わず、`guest.py start` を直に呼ぶ。
   - 引数は `zdesktop-guest.sh:57-59` と同じにする。
   - GL の環境変数（`LIBGL_ALWAYS_SOFTWARE`・`MESA_LOADER_DRIVER_OVERRIDE`）は付けない。
   ```sh
   export GUEST_RUNTIME=$HOME/ws139-e2/run
   export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/<手順 1 で見た Intel の ICD>
   R=$HOME/zedbsd-q306-venus/dependencies/q312-quiesce/install
   export RENDER_SERVER_EXEC_PATH=$R/libexec/virgl_render_server
   export LD_LIBRARY_PATH=$R/lib/x86_64-linux-gnu
   . plan/tools/guest/venus-hostmem.sh        # VENUS_HOSTMEM
   python3 plan/tools/guest/guest.py start image.img --qemu-extra "-object memory-backend-memfd,id=mem,size=8G,share=on \
     -machine memory-backend=mem -device virtio-gpu-gl-pci,id=venus,venus=on,blob=on,hostmem=$VENUS_HOSTMEM,max_outputs=1 \
     -display egl-headless,rendernode=/dev/dri/renderD128 -vnc unix:$GUEST_RUNTIME/vnc.sock,display=venus \
     -device usb-tablet,bus=xhci.0,port=4"
   ```
5. 起動しない時は、`$GUEST_RUNTIME/qemu.log`（`guest.py` が残す QEMU の stdout/stderr、`guest.py:209-211`）を読む。
   - serial・console の log では判定しない。
   - 前例（`venus-qemu.py`）との差（`virtio-vga-gl` と `virtio-gpu-gl-pci`、`-vga`）を一つずつ試す。調べの上限の内で行う。
6. 計測を 2 回行う。
   ```sh
   sh plan/ws139/tests/perf-run.sh --env=E2 out-1
   sh plan/ws139/tests/perf-run.sh --env=E2 out-2
   python3 plan/ws139/tests/perf-summary.py out-1
   python3 plan/ws139/tests/perf-summary.py out-2
   ```
   `perf-run.sh` は guest の SSH を待ち、KVM が無ければ止まる（p001）。
7. **E2 になっているかの確かめ**: `out-1/device.txt` の device の名前が Intel（例 `Intel(R) Graphics (ADL GT2)`）であること。
   `llvmpipe` なら E2 ではない。値を台帳に入れず、手順 3 の 4・5 に戻る。
8. 終わり（trap が後始末する）。

## 受け入れの条件

1. guest の Vulkan の device が Intel であることの記録（`device.txt`）。KVM が有効（`env.txt` の `query-kvm`）。AC で測った記録。
2. E2 の 2 回の `summary.tsv` で、P-01・P-02・P-03 の metric が `NA` でない。
3. 台帳（ws.md）の P-01・P-02・P-03 に E2 の値を足す。E1 との比（E1 ÷ E2）と、CPU の違いの注を書く。
4. `e2-run.sh`・`e2-remote.sh` が commit され、使い方が先頭の comment にある。
5. iGPU が vfio に戻ったこと（`igpu-after.txt`）と、centris の lock を放したことの記録。

## 結果

（未実施）
