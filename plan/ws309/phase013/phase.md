<!-- awesome-plan project=zedbsd record=ws309-p013 -->

# ws309-p013 present の常駐の thread（T4、暫定の ioctl、共通の wake）

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.4（T4）・§6 の「暫定」・§8（起動と終了の順）。Queue: q1662-i10（P1）。
- 状態: cleared（2026-10-29 q1662-i10 P1、branch `agent/p1-ws309i`。下の「結果」の未実施を参照）。
- 依存: p012（cleared）。

## 範囲と判定

- 「暫定の ioctl」: 決定 8 のとおり、V2（p015）が入るまでゲームモードの present は backend の既存の `GPU_DISPLAY_PRESENT`（uapi の既存の口）を T4 から呼ぶ。
  kernel・HAL（`include/hal/hal.h`）は変えない（変える必要は無かった）。
- 判定:
  1. backend（scanout-zedbsd.c）が thread と pipe を持たない。present は compositor の常駐の thread `present`（lane、`KWL THREADS` に出る）が同期で待つ。
  2. 結果は共通の wake pipe と take で main に来る（main の poll に scanout の fd が無い）。
  3. claim（`kl_backend_scanout_open`）と release（`_close`）は T3（display の thread）の job で、release は T4 の in flight の present が返った後。
  4. buffer の import は main（`kl_backend_scanout_begin`）。release が終わるまで表示していた buffer の hold を保つ（client が消える時はその場で放す）。
  5. build（amd64・Linux）warning 0、host の lane 試験 PASS、QEMU `scanout-guest.sh` PASS（ゲームモードに入って frames が進み、出て window mode が開き直る）。

## 結果（q1662-i10）

- 実装:
  - backend の口（keiland-backend-display.h）: `kl_backend_scanout_begin`（main: 検査・import・present の用意だけ）と新しい `kl_backend_scanout_present`
    （present の thread: blocking の present、frame を数える）。`kl_backend_scanout_fd`・`_end` を消した。Linux・FreeBSD の drm の stub も同じ形。
  - scanout-zedbsd.c: thread・pipe・cond を消した。`scanout_open` と claim の番号、forget の `GPU_RESOURCE_DESTROY` を mutex で守る（T3 の open・close と main の forget）。
  - compositor（scanout.c）: lane `present`（1 本）を display の thread の起動の時に始め、止める時は T3 の後（§8）。entry は T4 が無ければしない。
    frame は begin の後に job を submit、done で前の front を放し frame callbacks。leave は T3 に release を頼み、front・flight の hold は release の終わりで放す
    （`kwl_scanout_client_gone`: client の破棄の時、release を待つ記録からその client の buffer を放す。objects.c）。claim の番号 `server->scanout_claim` で
    leave の後に返った present の結果を捨てる。
  - display-thread.c: job `DISPLAY_SCANOUT_OPEN`・`_CLOSE`（close は `kwl_scanout_present_wait` → `kl_backend_scanout_close`）。終了の drop でも release は走り then を呼ぶ。
    その場で（inline）走らせる job は、thread に先に渡した job（release）の終わりを待ってから（`kwl_lane_wait`）。
  - lane.c: `kwl_lane_wait`（take をしない待ち、他の thread から呼べる）。`kwl_lane_finish` はそれを使う。
  - main.c: poll から scanout の slot を外し、`kwl_scanout_take`。
- 確かめ（2026-10-29）:
  - build: `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/bin/wayland` exit 0・`warning:` 0。
    `make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws309/keiland-linux all`（-Werror）exit 0・`warning:` 0。
  - host `plan/ws309/tests/lane/run.sh build/ws309/host-lane-p013`（ASan+UBSan・TSan）PASS（別の thread からの wait を足した）。
  - QEMU Venus `scanout-guest.sh build/ws309/p013b build/ws309/img/hdd-image.img build/amd64/bin/wayland` PASS:
    `KWL THREADS count=4 names=io-file,io-device,present,display`、`KWL SCANOUT direct=1 … switch_ms=520`、`direct=0 reason=no-window … frames=86`、
    window mode が開き直り（switch_ms=446）、capture 1280x800 spread=169、KWL FAILED 無し。
- 未実施: 実機（RDNA・RTX・VF2）のゲームモード、arm64・riscv64 の build、FreeBSD の build。
