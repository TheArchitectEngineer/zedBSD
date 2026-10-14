# T1: WS037 nvrtx R6・R7・R7g・R7(P8a) の再試験 2026-10-15

host awe@10.0.10.3（RTX 2070 を VFIO で QEMU guest に。実機の GPU の証拠、QEMU の guest）。main 84eb7bbf8（R6・R7、P6 の片付けの直し 2a2718208 を含む）、R7g は d7ef10f24（P5g）、R7(P8a) は 58cfb4dea（P8a）。image は `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v6/<run> NVRTX_RUN=<R6|R7|R7g|R7>`（R7(P8a) は v6/R7p8）。host の ~/nvrtx/v6R6・v6R7・v6R7g・v6R7p8 に置いた（rm せず）。各 run の前に host を reboot、`prepare.sh --check` の ok 以外は 0 行。判定は QMP screendump の PNG。

| run | 結果 | 要点 | PNG |
| --- | --- | --- | --- |
| R6 | PASS | P5 の 4 行（fence 0x1234 1405 us、0x1235 995 us、filled 64 words）→ `P6 ok: channel and vaspace freed` → `P7 begin: unload`・FWSEC-SB・booter_unload → `P7 ok: region torn down`。`P7 unload call` の行無し。`login:` 表示あり | /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/R6-a.png |
| R7 | PASS | R6 と同じ印（fence 550・992 us）に `P8 is not in this kernel; stopping after P5`、`P6 ok`、`P7 ok`。画面は scroll して login の行は切れている（getty_console は起動済みの画面を R6・R7p8 で確認） | .../hw6/R7-a.png |
| R7g | PASS | `P5g ctxbufs: 9 buffers, 10741 KiB`、`P5g arena at 1eb800000, buffers c85000 bytes`、`P5g ok: golden context`、`P5g ok: scrubber`、`P6 ok`（frees failed 無し）、`P7 ok: region torn down`。 | .../hw6/R7g-a.png |
| R7(P8a) | PASS | `P8 begin` → `P8 heads 4 (f), windows ff, outputs ff00` → `P8 ok: display, heads f, windows ff, lit 0` → `P8b is not in this kernel; stopping after P8a` → `P6 ok` → `P7 ok: region torn down`。lit 0: OVMF が GOP を点けておらず、`P8 head H: display … SOR`・`WxH of …, clock` の行は出ていない（照合する値無し。host に monitor が繋がっているかは未確認） | .../hw6/R7p8-a.png |

- build（R6・R7）は rc 0、約 7 分。試験は 1 run 約 4 分。QEMU は host の 1 つだけ、ローカルの QEMU は起動していない。
- 後始末: host の QEMU を止め、host を reboot 済み。
- 未実施: R7(P8a) の head の解像度の照合（lit 0）。
