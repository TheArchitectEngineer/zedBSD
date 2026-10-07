# 5330 を zedBSD で素に起動する確認のまとめ（2026-10-07 Q1）

ユーザーの立会いが要る確認を 1 回の機会にまとめる。image は main の最新（c4129dff1 以降）の kernel に display-control を足した物（例 `FILES_CONFIG=plan/ws113/tests/config-amd64-p012.mk`、または 5330 の普段の config に `ZEDBSD_USER_PROGRAMS += display-control`）、kernel の message を残す（`ZEDBSD_GRAPHICAL_BOOT=n` か `display=edp login=graphical`）。QEMU では Type-C の DP は無い。dmesg・log は SSH か USB の読み戻しで採る。

## A. USB-C の DP（WS051 p004a・p005a・p004b、P1）

1. TC2 に USB-C の DP の monitor を挿したまま起動。dmesg: `i915: DP-ext TC2: connected (step done, rc 0)`、DPCD 000 の行、`rate max 540000 ... lanes 4`、`EDID: 2 block(s) ... DTD1 1920x1280`、`probe 1 held the port in DP-alt mode (FIA lanes 4), <DP-alt 以外> after it`、`hpd DP detect DP-2: connected`、DP-1 は disconnected。eDP（GOP）の画面のまま。takeover の log の `N1 registry: N encoder(s)`。
2. monitor を抜いて起動: DP-2 disconnected、AUX の timeout の log が無い。
3. 起動の後に TC2 へ挿す・抜く・挿す: `i915: hpd: long hpd on DDI E: detecting` → `DP-ext TC2: connected` → `HPD-EVENT DP-2 ... disconnected -> connected (CHANGED)`、抜くと `connected -> disconnected`、各 probe の後に PHY が返る、HPD の storm が無い。
4. 表示: greeter・compositor を止め（`service stop greeter`、wayland を kill）、`display-control --index=N --hold=3` を N=1 から（切断は `done error=6 step=claim` で次へ）。DP-2 の回: `claim lease=L`・`present error=0`・`done error=0`、dmesg `the output moves to connector N (DP…, Keiland's claim)`、`DP on TC2 (DDI …): 1920x1280 … link 162000 kHz x4 at 24 bpp`、`ended PASS`。monitor に緑（0x30c060）、power off で消え on で戻る、eDP は消灯。release の 10 秒後に `the firmware's output (eDP panel) is the output again`、`display-control --index=0 --hold=3` で eDP に緑。hang・device lost 0。
5. 失敗の道: run が失敗した時は `the moved output failed; the firmware's output (eDP panel) is the output again ...`、その回は `present error=5`、`presentation fails from now on` は出ない、その後 `--index=0` で eDP に緑・`present error=0`。
6. 余裕があれば USB-C→HDMI の adapter で 1・4。

## B. HDMI と蓋（WS113 p011a・ws052-p012、P2）

1. 内蔵で起動 → HDMI を挿す（何も変わらない）→ 蓋を閉じる: `i915: resident display: the output moves to connector N (HDMI, Keiland's claim)`・`KWL LID external`・`KWL OUTPUT switch name=...:hdmi:B`、内蔵は消え HDMI で使える。
2. 蓋を開ける: 内蔵に戻る（`KWL LID internal`）。
3. 蓋を閉じたまま HDMI を抜く: 内蔵に戻って lock して眠る。
4. compositor を終えて 10 秒で GOP の出力に戻る（`the firmware's output (eDP panel) is the output again`）。

## C. Sleep（WS052 p010〜p012）

蓋・sleep button・無操作（Settings の Power の時間）で眠り、起きる。中止の理由が lock の画面に出るか。電源ボタンは何もしない（WS182 の口）。

## D. 起動の繰り返し（WS084）

`reboot-loop.sh` の 10 回（ws084 の p003）、その最初の 1 回で ws118-p006 の B（`takeover: rc=0, crtcs stopped 1, still active 0x0`、reference error 無し、picture up、目視）。BUG-249 の reboot の fallback を入れていれば、5330 で reboot が今どおり効くこと。

## E. UCSI（WS050）

dmesg に `ucsi: ... connectors`、`timed out`・`did not start` が無い、/dev/typec。
