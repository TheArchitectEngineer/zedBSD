<!-- awesome-plan project=zedbsd record=ws075p015 -->

# ws075-p015: BUG-085 の再試験（BUG-091 の修正の後の kernel）と H4 の撮影の live buffer

Phase ID: `ws075-p015`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28 の再開。修正の後の kernel で egltest6・p005 を各 5 回、BUG-085 の形は 0。BUG-094 の原因を gdbstub で特定し
HAL の実装を直した。`h4-ctl.py shot` の live の選択は実機で確認。H4 の提案 2 は [ws075-p016](../phase016/phase.md) へ。
最初の試み（同日、main の wrap-up で中断、有効な run 0）は下の「最初の試み」）
Phase disposition: normal
承認: 2026-09-28 main の依頼（WS075 の i915 の subagent、デモの頑健さ）。

## 範囲

1. [BUG-085](../../bugs/BUG-085.md) の再試験: BUG-091 の修正（ws075-p014）の入った kernel で、egltest6 と p005 の場面の capture の run を
   各 5 回以上（どちらも wlkill の順序を含む zdesktop の構成）。停止・凍結の数を前後で記録し、再現すれば gdbstub で compositor と
   GPU core の状態を取り原因を探す。
2. 道具: `plan/ws075/tests/hdmi/h4-ctl.py shot` が表示中の（live の）scanout buffer を選ぶ（BUG-091 の agent の発見: H4 の黒い greeter の
   画像は表示していない buffer だった）。
3. 時間が残れば: H4 の提案 2（login・logout の lease の替わり目で HDMI を暗くしない）。先に今の暗転の時間を測る。

## 作ったもの

| 所 | 内容 |
| --- | --- |
| `plan/ws075/tests/bug085-hw.sh IMAGE OUTDIR [SCENARIO]` | vkloop-hw.sh の作った zdesktop の capture の image を、lock の下で 5330 の passthrough で 1 回走らせる。QEMU に gdbstub（5330 の 127.0.0.1:1235、接続するまで guest は止まらない）を付け、`bug085/b085-watch.py` が capture の frame の止まり（25 秒）・kernel の fault を印の file にし、通常の終わり（init の power-off）から 8 秒で QEMU を終える。停止・fault が 420 秒を過ぎても続くと QEMU と lock を保ったまま gdb の手順を出し、`OUTDIR/release` で終える。OUTDIR に debugcon の kernel の全 log・serial・harness・capture の画像と result.json・guest の disk の log |
| `plan/ws075/tests/bug085/b085-qemu.sh`・`b085-watch.py` | 上の 5330 側（run-parity-vk.sh の基準の条件 + QMP + USB + gdbstub、i915-capture.py を並べて走らせる） |
| `plan/ws075/tests/hdmi/h4-ctl.py`（`shot`） | 表示中の buffer を選ぶ: まず QEMU の monitor の `info pci` から passthrough の iGPU の BAR0 を取り、`xp` で pipe A・B の primary plane の `PLANE_SURFLIVE`（0x701ac・0x711ac）を読んで A・B の surface と照合する。読めなければ kernel の log の今の lease の `picture up`・`flip N` の最後（kernel は lease の最初の 3 flip だけを log するので、3 flip を超えた lease では「古いかもしれない」と注記）。NAME.json に `live`・`live_source`・`surfaces` |
| `plan/ws075/tests/hdmi/h4-png.py` | NAME.json の `live` の buffer を NAME-live.png にも書く |

build（worktree、BUG-089 の回避: `build/{NoctLang,distfiles,llvm,llvm-source,sources,ws035-fonts,ws035-wallpaper}` と
`toolchain/llvm/distfiles` を main への symlink、patch の mtime を main に合わせた。`make -n` で LLVM の configure・install が無いことを
確かめた。sysroot は自分の `build/amd64/sysroot`）: `CAPTURE=zdesktop-egltest KEILAND_APP=egltest6 BUILD=build/b085-e6 VKLOOP_BUILD_ONLY=1
plan/ws031/tests/vkloop-hw.sh zdesktop`、同じく `KEILAND_APP=egltest BUILD=build/b085-e5`。warning は外部の Noct の既存の 1 件だけ。
kernel は main の 06abe378（BUG-091 の修正 = workqueue.c の timer queue・request-queue.c の retire の `irq_lock` ほかの irqsave を含む）。

## 再開（2026-09-28 の夜、WS075 の i915 の subagent）

### 道具の追加

- `bug085/b085-watch.py`: (1) 最初の capture の frame が 180 秒で来ない（起動の停止）も `stall` の印に。(2) kernel の log の
  `A64 TIMER CAL READY ticks=N` が 100000 を超えたら較正の誤り（BUG-094）として印を付け QEMU を終える。(3) compositor が capture の
  lease を返したら session の終わりとし、60 秒後に gdb で CPU が halt したかを `poweroff` の印に書いて QEMU を終える（capture の image の
  power-off は完了しない: [BUG-095](../../bugs/BUG-095.md)。init の `executing system action` の行は debugcon にも serial にも出ない）。
- `bug085/procs.py`（gdb、DWARF 無し: `all_processes` から process・thread の状態と待ちの queue の名前）、`kstack.py`（眠る thread の kernel の
  stack の return address）、`ustack.py`（thread の page table を direct map で辿って user の stack を読む）、`summary.py`（run ごとの 1 行）。
  struct の offset は `src/kern/process.c` を `-g` で compile して `ptype /o` で取った（2026-09-28 の tree）。

### BUG-094 の原因と修正（HAL の実装、hal.h は不変）

main の kernel の image の 4 回（e6-1・e5-1・e6-2・e5-2）のうち 3 回で compositor が始まらなかった。gdbstub で 2 つの形を特定した
（詳細は [BUG-094](../../bugs/BUG-094.md)）:

1. **APIC timer の較正**: e5-1 は `A64 TIMER CAL READY ticks=1069954`（正常 62755 の 17 倍）、e6-2 は 875803（14 倍）。gdb で
   `kernel_ticks` は壁時計 5 秒に 296（約 59 Hz）。`sleep 45` が 10 分以上かかる。`lapic.c` は APIC の count を PIT の窓の準備の前に始めていた。
2. **AP の timecounter の probe の期限切れ**: e5-2 は `TIMECOUNTER AP FAIL cpu=2 reason=ready-timeout` → `TIMECOUNTER UNAVAILABLE` →
   i915 の start が `time_base_anomaly: 5`（p006 の r6 と同じ）→ `ZWL EXIT frames=0 error=6`。

どちらも起動の時間の測定が vCPU の停止（推定: capture の harness の `pmemsave` の走査が QEMU の BQL を持つ）で狂う。修正:

- `src/hal/amd64/bsp-pcat/lapic.c`: APIC の count を gate を上げる直前と OUT の high を見た直後で読み（TSC と同じ括り）、窓を 3 回測って
  最短を TSC の組と一緒に使う。`A64 TIMER CAL WINDOW n elapsed=` を log。
- `src/hal/amd64/bsp-pcat/timecounter.c`: `wait_probe_value()` は poll の数に加え、TSC の rate が分かっていれば最低 2 秒待つ。

修正の後の 10 回の run で、3 つの窓のうち 1〜2 つが 10〜14 倍に伸びた run が 5 回（fe6-1 の窓 1 = 7714012、fe5-1 の窓 0 = 6600578、fe6-2、
fe5-4、fe5-5）あり、どれも最短の窓（約 625400）が選ばれ、tick は正常（`ticks=62533〜62626`）。AP の probe の失敗は 0。**BUG-094 の
形は 10 回で 0**（前は 4 回で 3）。

### BUG-085 の再試験（修正の後の kernel = main 8b6fec35 + 上の HAL の修正、`build/b085-e6f`・`build/b085-e5f`）

実機（5330、VFIO passthrough の QEMU guest）。gdb は印の前に attach していない。証拠は worktree の `build/b085-runs/fe{6,5}-{1..5}/`、
一覧は `summary.py`。

| run | harness | 最後の frame | compositor の終わり | egltest | 絵の種類 |
| --- | --- | --- | --- | --- | --- |
| fe6-1〜5（egltest6） | 5 回とも pass | 3344・2971・2993・3308・3332 | 5 回とも `ZWL EXIT error=0`（power-off の SIGTERM） | SCENES DONE、CHECK 4 つ failures 0 | 4〜5 |
| fe5-1〜5（p005 の 7 場面） | pass 1（fe5-2）、`scenes_shown` false 4 | 2921・6515・3007・3120・3201 | 5 回とも `ZWL EXIT error=0` | SCENES DONE、CHECK 8 つ failures 0 | 2〜7 |

- **wlkill の後の compositor の停止: 0/10。guest の全体の凍結: 0/10。capture の frame の止まり（stall の印）: 0/10。fault・fatal: 0。**
- fe5 の `scenes_shown` false 4 回は画面の停止ではない: harness（`i915-capture.py` の `keiland_egltest`）は「desktop」の絵との差で場面を
  数えるが、4 回とも desktop の絵を撮る前に最初の場面の窓が出ていた（sheet: 全ての絵に egltest の窓）。GPU の submit あたりの時間が
  run によって 16〜22 ms と違い、遅い run では最初の場面が 35 秒以上続いて絵の種類が少ない（fe5-5 は 12 枚で 2 種、capture の frame は
  約 7 枚/秒で進み続けた）。p006 の r5（場面が画面に出ない）も perf の形（submit あたり約 34 ms）が似ており、同じ説明が当てはまる
  可能性がある（未検証）。harness の判定の直しは WS031 の道具のため行っていない。
- 修正の前の kernel の有効な run（e6-1、main 8b6fec35）も BUG-085 の形なし（harness pass、`ZWL EXIT error=0`）。

**BUG-085 は 11 回の有効な run で再現せず**（修正の前 1、後 10）。BUG-091 の修正で直ったとは言えない（元の率は 12 回で 2〜4）が、
今の kernel では観察されない。ticket は tracking のまま。

### `h4-ctl.py shot` の live の選択（実機）

`hdmi-h4-hw.sh` の run（`build/h4-base/`）で、`xp` が passthrough の BAR（0x7010000000）の `PLANE_SURFLIVE` を読めた:
`greeter1: live buffer B (PLANE_SURFLIVE pipe B = 0xbcb80000)`、`session1: live buffer B`、以後の全ての撮影も register から選んだ
（`build/ws075-shots/ws075-p015-h4ctl-shot-live-greeter.png`）。同じ読みを 20 ms ごとの register の標本（`h4-ctl.py watch`、p016）にも使った。

### 確認

- build: `CAPTURE=zdesktop-egltest KEILAND_APP=egltest6|egltest BUILD=build/b085-e6f|e5f VKLOOP_BUILD_ONLY=1 plan/ws031/tests/vkloop-hw.sh zdesktop`、
  `plan/ws075/demo/build-demo-image.sh build/h4-hold2 passthrough`: compiler の warning 0（worktree、`build/{llvm,llvm-source,llvm-build,NoctLang,
  distfiles,sources,ws035-*}` は main への symlink、Noct の patch の mtime を main に合わせた、自分の `build/amd64/sysroot`）。
- QEMU の boot test（GPU の無い q35、`plan/tools/boot-test.sh`、`build/h4-hold2` の image = 両方の HAL の修正を含む）: PASS
  （`build/ws075-shots/ws075-p015-boottest-nogpu-login.png`）。
- 規約: `plan/tools/style-check.py` で変えた行（`lapic.c`・`timecounter.c`）の指摘 0。diff の空白の検査は清浄。
- 未実施: bare metal の起動（HAL の修正は QEMU の KVM の passthrough と GPU の無い QEMU だけ）。他の arch の HAL は変えていない。

## 最初の試み（2026-09-28 の午後、中断）

実機（5330、VFIO passthrough の QEMU guest。QEMU の emulation の証拠ではない）。証拠は worktree の `build/b085-runs/e6-1/`。

| run | image | 結果 |
| --- | --- | --- |
| e6-1（16:22〜16:27） | `build/b085-e6`（egltest6） | **compositor が起動しなかった**（BUG-085 の形とは別）。kernel の log は i915 の起動（`resident: GPU node published; serving`）で終わり、capture の frame は 0。guest の disk に zdesktop.log・wlkill.log・mview.log・dmesg.log が無い（zdesktop の service は走っていない）、`/var/log/messages` は空 |

e6-1 の gdbstub の観察（boot から約 180 秒、kernel の tick は進む: `kernel_ticks` 0x3aa2 → 0x3b81 → 0x4779）: 4 つの CPU が全て
`sched_idle` の `hlt`、kernel の log の ring（`klog_used` 0x22dc）は boot の記録だけ、`next_pid` は 6（作られた process は 5 つ）。rc.conf の
service の順は vkwait1（`sleep 45`、syslogd の後）→ zdesktop → … なので、**`sleep 45` が終わらないか、init がその終わりを受け取らず、
service の連鎖が止まった**と見られる（全ての thread が眠っていて、割込みは届いている）。**注意: 最初の gdb の attach（VM を数秒止めた）は
この `sleep 45` の最中だった**ので、attach が原因の可能性を除けない（kernel の log が止まったのは attach の約 25 秒前だが、その後に
kernel の log を出すものは compositor までない）。process の状態（どの thread が何を待つか）は DWARF が無く struct の offset を
調べる時間が無かったので未読。QEMU は `pkill -TERM` で終えた（harness は BrokenPipe）。

停止・凍結の数（BUG-085 の形）:

| | wlkill の後の compositor の停止 | guest の全体の凍結（egltest の途中） | 場面が画面に出ない（compositor は動く） | 分母 |
| --- | --- | --- | --- | --- |
| 前（ws075-p006、[BUG-085](../../bugs/BUG-085.md) の記録） | 2（st1・bisect1） | 1（fix1-p005scenes） | 1（r5） | ticket の記録の zdesktop の run（st1・st3・ms1・ms1-p005scenes・st3-p005scenes・bisect1・bisect1b・fix1-p005scenes・fix1b-p005scenes・fix1-egltest6・r5・r6b の 12 回。r6 は i915 の start の time_base_anomaly で別） |
| 後（この Phase） | 0 | 0 | 0 | **0 回**（e6-1 は compositor の前で止まり、BUG-085 の判定に使えない） |

**BUG-085 が BUG-091 の修正で直ったかは未確認**（有効な run が 0）。e6-1 の「service の連鎖の停止」は新しい観察（1 回、gdb の attach の
影響を除けない）。

## 最初の試みの時点の残り（再開で済んだ）

- BUG-085: egltest6 をあと 4 回以上、p005 の 7 場面（`build/b085-e5`）を 5 回以上:
  `plan/ws075/tests/bug085-hw.sh build/b085-e6/hdd-image.img build/b085-runs/e6-N`、
  `plan/ws075/tests/bug085-hw.sh build/b085-e5/hdd-image.img build/b085-runs/e5-N`（1 回約 6 分、image は worktree で上の手順で作り直す）。
  停止したら script の出す手順で gdb（`thread apply all bt`、compositor の thread、GPU core の session の予約・fence・待ち）。
  判定: `capture/result.json` の scenes_shown、`guest-logs.txt` の zdesktop.log の最後の frame と `WLKILL`、`EGLTEST SCENES DONE`、`stall`・`fault` の印。
- `h4-ctl.py shot` の live の選択を実機（`hdmi-h4-hw.sh`）で確かめる（register の `xp` が vfio の BAR を読めるか。読めなければ log の選択になる）。
- H4 の提案 2（lease の替わり目で HDMI を点けたまま）: 未着手。まず `hdmi-h4-hw.sh` で login・logout の暗転の時間を測る。
