<!-- awesome-plan project=zedbsd record=ws103-p002 -->

# ws103-p002: 起動の問い合わせを VK_KHR_display へ、`--direct` の削除

- Parent: [WS103](../ws.md)
- Status: cleared（2026-09-30 夜。q509-i01 は uncleared で終え、同日の追いの確かめで基準 6 を満たした）
- Disposition: normal
- Queue: q509-i01
- Design: [design.md](../design.md) §2.1・2.2、§3 の p002

## 範囲

compositor（`userland/desktop/wayland/`）の `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE`・`GPU_DISPLAY_CLAIM`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_RELEASE` と
`--direct` の道（Vulkan が開けないときに落ちる道を含む）を消す。大きさの既定は選んだ display の `physicalResolution`。GPU の fd は buffer の import のために残る。
`ZWL GPU` の行と `ZWL PRESENT` を読む試験を直すか退役させる。

範囲の外: `RESOURCE_IMPORT`・`FENCE_QUERY`、libvulkan、HAL、toolchain。

## 完了の基準

1. 上の 6 つの ioctl と `--direct`・`schedule_direct`・`zwl_present`・`claim_display`・`zwl_unscan`・`lease` が compositor に無い（grep）。
2. amd64 の build が warning 0。
3. `--width` なしで、画面の大きさが今と同じ（QEMU の Venus）。
4. greeter → login → Log Out → greeter（WS099 C1 の QEMU の部分）。
5. boot test（`plan/tools/boot-test.sh`）。
6. 5330 の passthrough で起動と login の smoke。
7. 規約の全文（`plan/coding-style.md`）を変更に適用。

## 記録（2026-09-30 夜、q509-i01、メインのエージェント Q1）

### 変更（commit `e27565f3` とその後の整え）

- `display.c`: `zwl_gpu_open` は GPU の fd を開くだけ（buffer と fence の確かめのため p004・p006 まで残る）。`zwl_unscan`・`zwl_present`・`claim_display`・`schedule_direct` と
  `zwl_schedule` の compose 無しの分岐を削除。`enter_window_mode` の `zwl_unscan` を削除。
- `compose.c`: `compose_display`・`compose_refresh`（新規）。device の直後に、vkdemo と同じ選び方（identity の変換を持つ最初の display）で `physicalResolution`
  （`--width`・`--height` が無いとき）と、出力の大きさの mode の refresh（無ければ最初の mode の refresh。vkdemo が mode を作るときと同じ）を取る。
  `ZWL DISPLAY device= width= height= refresh_mhz=` の行を出す（旧 `ZWL GPU fd=… display=…` の行の代わり）。
- `main.c`: `--direct` の option と usage、Vulkan が開けないときに直の表示へ落ちる道を削除（開けなければ起動の失敗）。pointer の初期位置は大きさが決まった後へ。
  shutdown の `zwl_unscan` を削除。`ZWL PERF` の行から直の present だけの計数（`presents`・ioctl・wake-to-flush）を削除（行を読む試験は無い。集めるだけの
  `plan/ws035/tests/zdesktop-p053-perf.sh` だけ）。
- `zwl.h`: `uapi/gpu-display.h` の include、`front`・`display`・`lease`・`direct`・直の present の perf の field、`zwl_present`・`zwl_unscan` の宣言を削除。`refresh` は
  wl_output の mode の通知（`protocol.c:462`）に要るので残し、出所（Vulkan）を注釈。`struct zwl_server` に役割の注釈。
- `handoff.c`・`objects.c`: `zwl_unscan` の呼び出しを削除（window mode では lease が常に 0 で何もしていなかった。`front_surface` の扱いは変わらない）。
- `import.c`: compose の無いときの分岐を削除。
- 設計との差: `refresh` は `--direct` だけでなく wl_output にも使われていた（design §2.1 に無かった）ので、Vulkan の mode から取る形にした。共有の `vkdemo/display.c` は変えていない。
- 範囲の外で見つけたもの: `plan/ws014/tests/wayland-qemu.py:540` は直の道だけが出す `ZWL PRESENT` を読む。WS014（p006、2026-09-13）の直の表示の試験で、直の道の削除とともに
  使えなくなった（退役）。file は WS014 の物なので変えていない。

### 確かめ

| 基準 | 結果 |
| --- | --- |
| 1 ioctl と直の道が無い | grep で `GPU_GET_INFO`・`GPU_DISPLAY_*`・`--direct`・`schedule_direct`・`zwl_present`・`claim_display`・`zwl_unscan`・`lease` は 0（経緯を書いた注釈 1 か所だけ）。`git diff --check` 通過 |
| 2 build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/wayland`: rc 0、warning 0。基準の image（`plan/ws099/tests/build-criteria-image.sh build/amd64` → `build/ws103/p002-criteria.img`）と passthrough の demo の image も warning 0 |
| 3 `--width` なしの大きさ（QEMU の Venus） | C2（`VENUS_SIZE=1920x1280`）で最大化した窓が 1920x1242（system bar の下）、画面 1920x1280。大きさは Vulkan の `physicalResolution` から取れた |
| 4 greeter → login → Log Out → greeter（C1、QEMU の Venus） | `criteria.sh … C1 C2`: C1 p126 PASS（2 周）、C1 c1-boot-shutdown PASS（起動・Shut Down、黒・text 0）、C2 PASS（14/14）。`build/ws103/p002-criteria/results.txt` |
| 5 boot test | `plan/tools/boot-test.sh build/ws103/p002-criteria.img`: PASS（`build/ws103/p002-boot/login.png`。GPU の無い回帰の構成なので greeter は終わり text の login。変更の前と同じ） |
| 6 5330 の passthrough の smoke | **未達**。`plan/ws099/tests/c5-hw.sh build/ws103/p002-pt.img build/ws103/p002-hw 1`: FAIL（起動画面の spinner のまま、`ZWL FIRST_FRAME` 0、i915 の画面の撮影 `-live` 無し、guest の disk に session の log 無し）。**変更の前の compositor**（`e27565f3~1` の `userland/desktop/wayland/`）で同じ作り方の image `build/ws103/p002-pt-before.img` も同じく FAIL（`build/ws103/p002-hw-before`）。原因は p002 の変更ではない。ws099-p002 は 2026-09-30 13:38 に同じ試験で PASS しており、その後の main の変更（13:30〜16:34 の kernel・sessiond・libvulkan などの約 20 commit）か環境を疑う（未確認）。QEMU の console の log は規則により判定に使っていない |
| 7 規約の全文 | 変更の範囲を `plan/coding-style.md` の checklist で見直した（段落の注釈、`if` の前の空行、3 項以上の条件の分割、成功の return、型の注釈、置き換えた注釈の削除） |

passthrough の image の作り方: `plan/ws075/demo/build-demo-image.sh build/amd64 passthrough "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical" ZEDBSD_NOCT_ACCEL=n`。
`ZEDBSD_NOCT_ACCEL=n` は、`userland/base/noct/noct/build-zedbsd-amd64` の CMake の cache が別の checkout（`/home/awe/zedBSD-rpi4`）で作られていて、新しい build directory では
Noct の build が失敗するため（toolchain の範囲なので触れていない。compositor の smoke には Noct の GPU 版は関係しない）。

### 追いの確かめ（2026-09-30 夜、q509 の後、ユーザーの指示の下で）

- ユーザーの指示「シリアルCOM1にdmesgをコピー出力するコンフィグがあります。それを使えば、エラーをつかめると思います。」により、この切り分けに限り
  AGENTS.md の「serial の log を読んで判定しない（解析を含む）」の例外として、`CONFIG_PCAT_SERIAL_MIRROR=y`・`ZEDBSD_BOOT_KERNEL_MESSAGES=y` の image
  （`build/ws103/p002-pt-mirror.img`）の COM1 の log（`build/ws103/p002-hw-mirror/kernel.log`）を解析に使った。判定には使っていない。
- 原因: **試験の image の作り方の誤り**。`ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical` を graphical boot（`ZEDBSD_GRAPHICAL_BOOT=y`、`login=graphical` を自分で足す）と
  組み合わせたため `login=graphical` が 2 回になり、kernel が `boot: parameter parsing failed (16); entering idle.` で止まっていた。Guardrail のその書き方は
  `ZEDBSD_GRAPHICAL_BOOT=n` と組にする物。変更の前の image（`p002-pt-before.img`）も同じ作り方だったので同じく止まった。main と p002 の不具合ではない。
  （同じ時に、ユーザーの見た 640x480 は古い overlay の layout の image によるもので、main・CI・nightly-143 は native の layout と graphical boot で正しいことを確かめた。）
- 作り直し: `plan/ws075/demo/build-demo-image.sh build/amd64 passthrough ZEDBSD_NOCT_ACCEL=n`（boot の行は config の既定 `display=edp`）→ `build/ws103/p002-pt2.img`、warning 0。
- `plan/ws099/tests/c5-hw.sh build/ws103/p002-pt2.img build/ws103/p002-hw2 3`: **PASS**（`ZWL FIRST_FRAME` 34 回、最大 48 ms、100 ms 超え 0）。session の log に
  `ZWL DISPLAY device=zedBSD i915 (Gen12 Xe) width=1920 height=1080 refresh_mhz=60011`、`ZWL ERROR`・`GPU_ERROR`・`VULKAN_ERROR` 0。画面 `build/ws103/p002-hw2/shots/wiseview-live.png`
  （10 個の窓の Wiseview）。ユーザーも i915 の実機（passthrough）で表示を確かめた（「i915実機で表示されています。」）。
- 基準 6 を満たし、Phase は cleared。これは QEMU の passthrough の証拠で、5330 を USB から単独で起動した実機の確かめは未実施。

### 再開の条件（q509 の終わりの時点の記録、上の追いの確かめで満たした）

5330 の passthrough で今の main（変更の前）の demo の image が greeter まで進むようになるか、別の確かめ方（実機の USB の起動など）をユーザーが決めたら、基準 6 だけを確かめて clear する。
