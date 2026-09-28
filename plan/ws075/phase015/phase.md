<!-- awesome-plan project=zedbsd record=ws075p015 -->

# ws075-p015: BUG-085 の再試験（BUG-091 の修正の後の kernel）と H4 の撮影の live buffer

Phase ID: `ws075-p015`
Parent: [WS075](../ws.md)
Status: uncleared（2026-09-28。main の指示で途中で終えた。実機の run は 1 回だけ（受け入れの egltest6・p005 の各 5 回に届かない）。
`h4-ctl.py shot` の live buffer の選択は書いたが実機で未実行。H4 の提案 2（lease の替わり目で HDMI を点けたまま）は未着手）
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

## 結果

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

## 未実施・残り（再開の手順）

- BUG-085: egltest6 をあと 4 回以上、p005 の 7 場面（`build/b085-e5`）を 5 回以上:
  `plan/ws075/tests/bug085-hw.sh build/b085-e6/hdd-image.img build/b085-runs/e6-N`、
  `plan/ws075/tests/bug085-hw.sh build/b085-e5/hdd-image.img build/b085-runs/e5-N`（1 回約 6 分、image は worktree で上の手順で作り直す）。
  停止したら script の出す手順で gdb（`thread apply all bt`、compositor の thread、GPU core の session の予約・fence・待ち）。
  判定: `capture/result.json` の scenes_shown、`guest-logs.txt` の zdesktop.log の最後の frame と `WLKILL`、`EGLTEST SCENES DONE`、`stall`・`fault` の印。
- `h4-ctl.py shot` の live の選択を実機（`hdmi-h4-hw.sh`）で確かめる（register の `xp` が vfio の BAR を読めるか。読めなければ log の選択になる）。
- H4 の提案 2（lease の替わり目で HDMI を点けたまま）: 未着手。まず `hdmi-h4-hw.sh` で login・logout の暗転の時間を測る。
