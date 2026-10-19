<!-- awesome-plan project=zedbsd record=ws270-p001 -->

# ws270-p001 VF2 の Keiland の設計

- 親: [ws270](../ws.md)、Queue: q1335 / q1335-i01（P1、2026-10-19）
- Status: cleared（2026-10-20 Q1 の判定: design-review-2 の結論「N1〜N4 と Minor 1・3・4 が直った事を Q1 が diff で確かめれば cleared にしてよい」。改訂 2.1（6d43500c1、merge 6ff9f0872）で N1（sessiond が attaching の間 gpu1 を待つ）・N2（p006a/p006b）・N3（copy の道の damage）・N4（keyboard は text login、mouse は最小の program）と libcurses の PIC を Q1 が差分で確かめた。WS270-D1・D2・D3・D5 は未決。2026-10-20 の WS277（DC8200 の zero copy の scanout）で段 2 の実用の fps の道が変わり得る）

## 目的

VisionFive 2 で Keiland の desktop を動かす道を決め、`plan/ws270/design.md` に書く。コードは書かない。

## 受け入れの条件

1. 今の Keiland（userland/desktop の compositor・libkeiland・libkeiland-backend-zedbsd・shell・app）と依存（Wayland・libvulkan・font・input・seat・logind 相当・audio）を読み、riscv64 で build できるかと足りない物（package・arch の分岐・toolchain の要否）を一覧にする。他の port（amd64・arm64 の Pi 4）の desktop の image の作り方と比べる。
2. WS269 D5 (a)（2026-10-19 22:04 ユーザー）に従い、WS269 p010（GPU の Vulkan）までに進める範囲（service の起動、seat・input（USB keyboard・mouse、WS256 p020）、display の node（WS269 p007 の gpu0）の claim、graphical の login の道）と、描画を始める条件を決める。Keiland は Vulkan でしか描かない（WS269 design §2）事を前提に、画面なしで確かめる方法（画面に 1 行、log を画面に出す等、AGENTS.md の判定の規則に合わせる）を書く。
3. 実装の Phase の分割（依存・LW の見込み・T1（QEMU の riscv64-virt）と実機の確認の点）と、ユーザーの判断が要る点（D1…）。HAL の API の変更が要るなら差分の案を proposed/ に置くだけ。

## 記録 q1339 / q1339-i01（2026-10-19 P1）

- design.md 版 2.1（N1: sessiond の gpu1 の待ちを p006a に、N2: p006 を p006a・p006b に分割、N3: copy の道の WSI の damage を p006b に、N4: C の試験、Minor 1〜9）。
- Minor 1 の code: `platform/riscv64/userland.mk` の `lib/libcurses.a` を `$(call ZEDBSD_USERLAND_OBJECTS,$(DYNAMIC_DIR)/obj,curses)` の PIC の object から作る（amd64 の形）。注釈を直した。
- build（P1 の worktree の `build/`、main を merge した後の増分の build）:
  - `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk -j16 rootfs`: rc=0、log の `warning:` 0。curses の object は `-fPIC` で `dynamic/obj` から作られた。`llvm-readelf -r` で libcurses.a の再配置は PCREL・GOT・CALL_PLT・64 だけ（絶対の `R_RISCV_HI20` は 0）。`bin` 215 個、`dynamic/*.so` 23 個（Minor 9 の数を確かめ直した）。
  - `config/ci/config-amd64.mk`・`config/ci/config-rpi4.mk` の `rootfs`: rc=0、`warning:` 0。
  - 未実施: clean からの全 build（upstream の package の既存の warning は増分では出ない）、libcurses.a を PIE に link する試し（tree に使う物が無い）、QEMU・実機。
