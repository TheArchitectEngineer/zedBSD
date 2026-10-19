<!-- awesome-plan project=zedbsd record=ws275-p006 -->

# ws275-p006 T1 の試験（sun4u の X）と startx で client が出ない件の観察

- 親: [ws275](../ws.md)、Queue: q1325 / q1325-i01（P1、2026-10-19、T1-746 (2) の解析）
- Status: uncleared（2026-10-19 T1-746 (2)・T1-747 で startx の後に client が出ない、原因不明 → BUG-337。ユーザーの指示で WS275 を停止）

## 症状（T1-746 (2)、QEMU）

`config-sun4u-x11.mk` の image で login → `startx &`、約 120 秒たっても root window（0x203040）と arrow の cursor だけ。
zwm・zterm・zshell・wallpaper が出ない（PNG: T1 の worktree の build/t746x/r3-x32/login-after.png）。

## code の読み（2026-10-19 P1、host のみ）

- startx → `Xzed --size 800x600 -- /bin/sh /etc/Xzed/Xzedrc`。Xzed は initialize（ENTER・`/tmp/.X11-unix/X0` の listen・root の描画・cursor）
  の後で fork/execve。Xzedrc は `zwm /bin/zterm &` と `zshell`。PNG は initialize の終わりまでは進んだ事を示すだけ。
- byte 順: libX11 は常に 'l' を名乗り、全部の request・reply・event を `w16/w32/r16/r32`（byte 単位の LE）で詰め・読む。
  CPU の順の memcpy は p005 で直した property だけ。Xzed は setup の 'B'/'l' を `c->order` に取り、全部を `rd16/rd32/wr16/wr32(…, order)` で扱う。
  XPutImage（opcode 128）は 3 byte の RGB で順に依らない。xzed の evdev の capability の bitmap は kernel と同じ `unsigned long` の語で整合。
  → protocol の byte 順の誤りは code の上で見つからない。
- 残る疑い（未確認）: (a) client が XOpenDisplay で失敗（sun4u の AF_UNIX・connect）、(b) Xzed の poll の loop が accept に戻らない
  （sun4u の poll・evdev の fd・timer の wakeup）、(c) client が sparcv9 で落ちる（SIGBUS 等）、(d) sh が Xzedrc を走らせない。
- 試験の config に `sleep` を足した（`ps` は sparc64 の gcc で `-Werror=maybe-uninitialized`（userland/base/ps/main.c:277）になり build できないので入れない。
  ps の直しは sparcv9 の base の build でユーザーの担当）。build: `make ZEDBSD_CONFIG=plan/ws275/tests/config-sun4u-x11.mk BUILD=build/ws275-p006 -j16
  build/ws275-p006/hdd-image.img` rc 0、warning/error 0。

## T1 への依頼（観察、QEMU のみ）

image: 上の config で build（`--size`・`-g` は T1-746 (2) と同じ x32）。login の後、`keys:` で次の 1 行を打つ（startx は使わない。Xzed を後で kill して
text の console に戻し、画面で読む）:

```
/bin/Xzed --size 800x600 -- /bin/sh -c '/bin/zwm /bin/zterm >/tmp/zwm.out 2>&1 & /bin/zshell >/tmp/zshell.out 2>&1; echo zshell=$? >>/tmp/zshell.out' & sleep 60; ls -l /tmp /tmp/.X11-unix; kill %1; sleep 2; cat /tmp/zwm.out /tmp/zshell.out; echo DONE
```

1. sleep 60 の間の X の画面の PNG（60 秒の前後に 1 枚）。
2. DONE の後の text の console の PNG（`ls` に X0 の socket があるか、`zwm: cannot open display` 等の文、`zshell=` の値）。
3. 画面が text に戻らない（Xzed が SIGTERM で終わらない）時は、QEMU の gdbstub（`-gdb tcp::<port>`）で止め、`bt` と `info registers`、
   `x/i $pc` を返す（Xzed が poll・read のどこで止まっているか）。可能なら QMP の `info registers` を 2 回、間を置いて。

判定はしない（P1 が結果から原因を決める）。serial・console の log は判定に使わない。
