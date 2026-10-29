<!-- awesome-plan project=zedbsd record=ws250-p004 -->

# ws250-p004: 実機の scenario の script

Status: cleared（2026-10-29 P5、q1676-i02。実機 10.0.10.3 の RTX 2070 で流した。下の「実機」）
Parent: [WS250](../ws.md)
Approval: p002 と同じ。

## 範囲

[design.md](../design.md) 6.3 の S0〜S6 を流す `plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX`（`plan/tools/gpu-perf/measure.sh` の段を使う、GPU_PERF_MACHINE=rtx）。

## 受け入れ

`sh -n`。実機で流すのは Q1 の割当で。

## 実装（2026-10-29 P5）

- `plan/ws250/tests/rtx-hang-check.sh OUT VMUNIX [--probe]`: `measure.sh deploy --kernel` → S0（mview scene-5000 12 s と vkhang --loops）→ S2（mview の
  中で vkhang、その後 vkhang --loops と keiland-shot）→ S3（vkhang 5 回と対照）→ S4（vkhang 2 つと mview）→ S5（vkhang を 0.5 s で kill -9、終わるまでの秒）→
  終わりの shot →（`--probe` の時だけ）S1（`measure.sh cfg nvrtx.kill 0` で再起動、mview の中で vkhang を 10 s、GSP の channel error の行、その後 line を外す）
  → `measure.sh linux`。各 scenario の log は OUT/sN.log、要約は OUT/summary.txt。mview と vkhang は machine の conf の USB_MVIEW・USB_VKHANG
  （plan/ws039/tests/vkhang-build.sh で作る）。
- 確かめた事: `sh -n` だけ。未実施: 実機での実行（Q1 の割当で、p005 の後）。S1 は GR が詰まったままになりうる（GSP が RC しなければ）ので最後に置いた。

## 実機（2026-10-29 13:50〜14:18 P5、q1676-i02、10.0.10.3 の RTX 2070、RDNA 機の時間の枠と lock の中）

実機の証拠だけ（QEMU は使っていない）。kernel は `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws250/k build/ws250/k/vmunix`（warning 0）、
mview・libvulkan・libc は同じ config の `BUILD=build/ws250/usb`、vkhang は `plan/ws039/tests/vkhang-build.sh build/ws250/usb`（今の tree、warning 0）。
コマンドは `PERF_BUILD=build/ws250 GPU_PERF_MACHINE=rtx sh plan/ws250/tests/rtx-hang-check.sh OUT build/ws250/k/vmunix [--probe]`。log は worktree p5-ws250 の
`build/ws250/hang/r1`（51da3274b の tree、--probe）・`r2`（stop の時間の log を足した物）・`r3`（S4 の直しの後、3ad5d28fc の kernel cksum 3326163690）・`s5-probe2.log`。

| scenario | r1（51da3274b） | r3（直しの後） |
| --- | --- | --- |
| S0 対照 | mview 49.50 fps、`VKHANG ok`、`session channel` 3 行 | 49.17 fps、ok、3 行（p011 の 49.1〜49.5 の内） |
| S2 取り消し | vkhang lost 2109 ms、mview 完走 48.97 fps（※ mview の spin が vkhang の後に始まっていた: script の誤り）、`stopped and freed`、control ok | vkhang lost 2001 ms、mview 完走 30.91 fps（停滞 4334 ms の 1 回）、control ok、stopped 1・stuck 0 |
| S3 繰り返し | 5/5 lost（2052〜2053 ms）、chid 24〜28 と回る、control ok | 同じ（5/5、24〜28、control ok） |
| S4 同時 | 2/2 lost（2054・6362 ms）、mview 完走（※ 同じ script の誤り） | 2/2 lost（2000・6317 ms）、mview 完走 12.90 fps（停滞 4333 ms×2）。r2（直しの前）は mview が `vkWaitForFences -4` で巻き添えに lost |
| S5 close の途中 | ※ pkill が無く kill されていない | `s5-probe2.log`: kill -9 の直後に process は終わる（Z、shell の wait 待ち）、reaper が閉じた session の channel を見つけて止める、control ok（4301 ms、GR の停滞の後ろ） |
| S1 probe（`nvrtx.kill=0`） | GSP が自分で RC（`channel error … type 109`）を 1 回出した（10 s の内）。mview の frame は数えられなかった（script の grep の誤り） | 流していない |
| S6 段 2 | `stuck` は 1 度も出ていない（流す条件に当たらない） | 同じ |
| 終わりの shot | `end.png` 1920x1080（desktop、新しい時刻） | 同じ（worktree の build/review/ws250-r3-end.jpg） |

- script の直し（3ad5d28fc と次の commit）: measure.sh を `sh` で呼ぶ（実行の bit が無い）、S2・S4・S1 は mview の最初の `MVIEW FPS` を待ってから vkhang（r1 は
  mview の準備の間に vkhang が終わっていた）、mview の FPS・HITCH の行も残す、S5 は `ps -A -o pid,state,comm` で選んで kill（pkill・pgrep が無い）、zombie を数えない、
  KLINES に `rm: `。S2 の shot は 3 回とも 0 byte だったので終わりの shot と同じ形（zs で keiland-shot）に替えた（この形は実機で未実行）。
- 終わり: Debian 6.12.111、RTX は vfio-pci（lspci -k）、NVMe の zedBSD の kernel は vmunix-va1（deploy の sha256 先頭 6a06e796a7b9b632 が va1 と一致、cksum 1463007375）、
  cfg の nvrtx.kill の行は無し。lock は 14:18 に返した。
