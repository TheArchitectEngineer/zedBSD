<!-- awesome-plan project=zedbsd record=ws139-p002 -->

# ws139-p002: E2（5330 の host の i915 の Venus）で測る手順と基準値

Status: planned
Disposition: normal
Parent: [WS139](../ws.md)
Queue: none
依存: p001（`perf-run.sh`・`perf-summary.py`・`build-perf-image.sh` が main に統合済み）、ws.md の **U2（5330 の操作の承認と担当）**
調べの上限: 3 h。手順 6 の確かめ（guest の Vulkan の device が Intel）まで届かなければ、そこで止めて uncleared にし、分かったことを記録する

## 範囲

- **入る**: 5330 の Linux（hostname `chaos`、ssh の alias `solaris10-man`）の上で、p001 の `perf-run.sh` を流す手順を作って記録する。
  E2 の基準値を 2 回取る。手順は再利用できる script（`plan/ws139/tests/e2-run.sh`）にする。
- **入らない**: product の source の変更、iGPU の passthrough（E3）、AX211、5330 の host の package・kernel・設定の恒久の変更。
- **所有する path**: `plan/ws139/`。共有の道具（`plan/ws035/tests/zdesktop-guest.sh` など）を変える必要が出たら、止めて Q1 に相談する（手順 5）。

## 分かっていること（2026-10-04、記録から。5330 の上では確かめていない）

- 5330 の操作の正本は `plan/tools/hw5330/README.md`。
  - ssh の名前は `solaris10-man`。IP は電源の入れ直しで変わったことがある（`10.0.10.25` ↔ `10.0.30.3`、README の 29 行）。
  - iGPU は起動時の既定で vfio-pci。Venus の試験の時だけ `~/bigbang/igpu-mode.sh host` で host の i915 に付け替える。QEMU が動いている間は切り替えを拒む（README の 49-51 行）。
  - i915 の実機の試験は `flock /tmp/i915-hw.lock` で順番にする。
- Venus の renderer（strict queue の virglrenderer の build）: `plan/ws035/tests/zdesktop-guest.sh:16-19` によると、5330 の
  `/home/awe/zedbsd-q306-venus/dependencies/q312-quiesce/install` にある（開発の host の `build/ws035-sq-venus/install` はその写し）。
- `zdesktop-guest.sh` は開発の host 向けに、次を固定で export している（32-34 行）。
  - `LIBGL_ALWAYS_SOFTWARE=1`、`MESA_LOADER_DRIVER_OVERRIDE=zink`。
  - `VK_DRIVER_FILES`（既定は lavapipe の ICD。環境変数で変えられる）。
  - QEMU の `-display egl-headless,rendernode=/dev/dri/renderD128`（59 行。開発の host では vgem の node）。
  5330 で host の i915 を使うには、少なくとも `VK_DRIVER_FILES` を Intel の ICD にする。renderD128 が i915 か vgem かは未確認。
- E2 で desktop を測った記録は無い（ws.md の環境の表）。

## 手順

### 1. 読むだけの確かめ（承認の前でもよい物は U2 の決めに従う）

```sh
ssh solaris10-man 'hostname; uname -r; qemu-system-x86_64 --version | head -1; python3 --version'
ssh solaris10-man 'bigbang/igpu-mode.sh show; pgrep -af qemu-system-x86_64; ls -l /tmp/i915-hw.lock'
ssh solaris10-man 'ls /usr/share/vulkan/icd.d/; ls -l /dev/dri/; lsmod | grep -E "^(i915|vgem) "'
ssh solaris10-man 'ls ~/zedbsd-q306-venus/dependencies/q312-quiesce/install/libexec/virgl_render_server'
ssh solaris10-man 'python3 -c "import PIL; print(PIL.__version__)"'
```

- 結果を「結果」の表に書く。
- QEMU が動いていれば、他の試験が使っている。止めずに待つか、Q1 に知らせる。

### 2. lock と iGPU の付け替え

1. 1 回の計測の全体を、5330 の上の `flock /tmp/i915-hw.lock` の下で行う。
   - 手順 3〜7 を 1 つの remote の shell script（`e2-remote.sh`、手順 3 で送る）にまとめる。
   - それを `ssh solaris10-man 'flock /tmp/i915-hw.lock sh ~/ws139-e2/plan/ws139/tests/e2-remote.sh'` で呼ぶ。
2. `e2-remote.sh` の始めに `~/bigbang/igpu-mode.sh host` を呼び、終わり（trap で、失敗の時も）に `~/bigbang/igpu-mode.sh vfio` で戻す。
   passthrough の試験の script は始めに自分で vfio にする（README の 51 行）。それでも元に戻す。

### 3. tree と image を送る

1. git の追跡中の file だけを送る。
   ```sh
   git archive HEAD plan/ws139 plan/ws099/tests plan/ws095/tests plan/ws134/tests plan/ws089/tests plan/ws035/tests \
       plan/tools/guest plan/tools/files plan/tmp/guest | ssh solaris10-man 'rm -rf ~/ws139-e2 && mkdir -p ~/ws139-e2 && tar -x -C ~/ws139-e2'
   ```
   - 呼ぶ script が他の path を使っていれば足す。足りない path は、手順 4 の起動の失敗で分かる。
   - `plan/tmp/guest` は guest の SSH の鍵。tree にあることを確かめる（master.md の作り直しの手順の 5）。
2. image（T か自分の worktree の `BUILD/hdd-image.img`、SHA-256 を記録）を `scp` で `~/ws139-e2/image.img` に送る。

### 4. guest を起こす（5330 の上、`e2-remote.sh` の中）

```sh
cd ~/ws139-e2
export GUEST_RUNTIME=$HOME/ws139-e2/run
export VENUS_RENDERER=$HOME/zedbsd-q306-venus/dependencies/q312-quiesce/install
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.x86_64.json   # 手順 1 で見た Intel の ICD の名前
sh plan/ws089/tests/settings-guest.sh start ~/ws139-e2/image.img
```

### 5. 起動しない時（調べの範囲）

まず QEMU の起動の失敗の文（stderr）と、`GUEST_RUNTIME` の QEMU の log を見る。console・serial の log で判定しない。

- **egl-headless の node**: renderD128 が i915 でなければ、i915 の render node を指す必要がある。
  `zdesktop-guest.sh` の 59 行は固定なので、環境変数（例 `VENUS_RENDERNODE`）で変えられるようにする小さな変更が要る。
- **zink・software GL**: `LIBGL_ALWAYS_SOFTWARE`・`MESA_LOADER_DRIVER_OVERRIDE` の固定の export が i915 の GL の readback を妨げるなら、同じく環境変数で外せるようにする。

`zdesktop-guest.sh` は WS035 の共有の道具である。変える前に、差分と理由を Q1 に送って許可を得る。
許可の後に、開発の host の既定の動きが変わらない形（環境変数が無ければ今と同じ）で変える。

### 6. guest の Vulkan の device を確かめる（E2 の要）

- `GUEST_RUNTIME=… python3 plan/tools/guest/guest.py run '…'` で、guest の中から device の名前を出す。
  compositor の起動の log（`/tmp/zdesktop.log` の `ZWL` の device の行。`grep -i device` で探す）か、`/bin/monitor` の `ZMON READY` の行。
- 名前が **Intel（例 `Intel(R) Graphics (ADL GT2)`）** であること。`llvmpipe` なら E2 になっていない。手順 4・5 に戻る。
- 確かめた行を `OUT/device.txt` に残す。

### 7. 計測

```sh
sh plan/ws139/tests/perf-run.sh --env=E2 ~/ws139-e2/out-1
sh plan/ws139/tests/perf-run.sh --env=E2 ~/ws139-e2/out-2
python3 plan/ws139/tests/perf-summary.py ~/ws139-e2/out-1
python3 plan/ws139/tests/perf-summary.py ~/ws139-e2/out-2
sh plan/ws089/tests/settings-guest.sh stop
```

- `perf-run.sh` の画面の取得（VNC の socket・QMP）は 5330 の上で完結する。開発の host から tunnel は張らない。
- 終わったら `scp -r solaris10-man:ws139-e2/out-1 solaris10-man:ws139-e2/out-2 build/ws139-perf-e2/` で持ち帰る。
- 5330 の `~/ws139-e2` は消してよい（自分が作った物だけ）。

### 8. 再利用の script

手順 2〜7 を `plan/ws139/tests/e2-run.sh IMAGE OUT`（開発の host で呼ぶ）と `e2-remote.sh`（5330 で走る）にまとめる。
改善の Phase（p004 以降）が前後の E2 の値を取る時に使う。

## 受け入れの条件

1. guest の Vulkan の device が Intel であることの記録（`device.txt`）。
2. E2 の 2 回の `summary.tsv` で、P-01・P-02・P-03 の metric が `NA` でない。
3. 台帳（ws.md）の P-01・P-02・P-03 に E2 の値を足す。E1 との比（E1 ÷ E2）を書く。
4. `e2-run.sh`・`e2-remote.sh` が commit され、使い方が先頭の comment にある。
5. iGPU が `vfio` に戻ったこと（`igpu-mode.sh show`）と、lock を放したことの記録。

## 結果

（未実施）
