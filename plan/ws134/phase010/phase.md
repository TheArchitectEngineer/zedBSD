<!-- awesome-plan project=zedbsd record=ws134-p010 -->
# ws134-p010: M4 全文規約・回帰・デモの通し

Status: in-progress（q663、P2 generation8、2026-10-04。今できる分（全文規約の見直しと修正、QEMU の回帰の依頼）を実施。実機の要る分は実機待ち）
Disposition: normal
Parent: [WS134](../ws.md)
依存: p002〜p009。2026-10-04 Q1: 今できる分を進め、実機の要る p003（fps）・p007（i915）・p009（ACPI）は「実機待ち」として残す（WS134 の完了は実機の後）。

## 全文規約の見直し（plan/coding-style.md の全文）

対象: WS134 で書いた・変えた source の全体。app（`userland/desktop/monitor/*.c`）、compositor の `wayland/sysmon.c` と `system.c` の追加、
libkeiland の `system/system-monitor.c`・`system-monitor-rate.c` と `system.c` の追加、backend の `monitor-zedbsd.c`・`monitor-linux.c`・
`monitor-freebsd.c`、kernel の追加（`sched.c` の `charge_cpu_time`・`sched_cpu_time`、`sysctl.c` の 3 つの leaf と登録の口、`disk.c` の統計、
i915 の `drv_i915_rps_telemetry_read`）、`sysctl`・`top` の追加、試験の道具（`monitor-probe`、`keiland-system` の monitor、host の試験と preview）。

- 道具: `plan/tools/style-check.py`（違反 0）と、それが見ない規則の臨時の走査（関数の最後の return の直前の comment、1 行に 3 つ以上の
  `&&`/`||`、初期化子の関数呼び出し、static・public の関数の comment）。残りは読んで確かめた（comment が文を言い換えていないか、
  allocation と確かめの 1 つずつ、critical section の空行と comment、`goto` 無し、条件の中の呼び出し無し）。
- 直した所: 関数の最後の return の直前に結果の comment（app 23 か所、sysmon 3、backend 10、試験 4）。3 つ以上の節の条件を節ごとの行に
  （app 9、backend 2、試験 1）。`sysctl` の `fetch_value` と backend の `monitor_fetch` を、成功の return が最後になる形に（失敗の
  EAGAIN が最後だった）。`sysctl`・`top` の free の後の成功の return を分けた。host の試験（host-test.c・preview.c）の段落の comment と
  条件の中の呼び出し（memcmp・strncmp）。
- 既存の違反（WS134 の前からある `system.c`・`sched.c`・`sysctl.c`・`disk.c`・`gt-power.c`・`sysctl/main.c` の旧い部分）は範囲の外。
  WS134 の追加の部分の違反は 0。
- 確かめ: zedBSD の monitor・compositor・libkeiland・sysctl・top・monitor-probe・keiland-system（-Werror）exit 0 warning 0、Linux の
  compositor と monitor（gcc）exit 0、host 試験 3 本 PASS、style-check 違反 0。

## 回帰（QEMU、T に依頼）

image（build-monitor-image.sh）で monitor-p002・p004・p005（cputimes）・p006（diskstats）・p008（backend）・p012（system の monitor）・p013
（app の system）の試験と、FreeBSD の `backend-test.sh`・`monitor-backend-p011.sh freebsd`（monitor-freebsd.c の `monitor_fetch` を直した）。
結果は未着。

## 実機待ち（WS134 の完了は実機の後）

- p003: fps の判定（QEMU は GPU の無い host で compositor が律速、T1-057）。5330 の i915 で呼吸 30 fps・操作 60 fps。
- p007: i915 の `hw.gputelemetry`（busy・周波数が負荷で動く）と monitor の GPU の使用率。
- p009: ACPI の thermal・電池（K4）。
- デモの通し: 5330 で monitor を App Home から起こし、本物の値・操作（touch）を通す。

## monitor の sim の段の FAIL（T1-070・T1-071・T1-072）の直し（2026-10-04）

- 症状: monitor-p003・p002 の sim の段で READY の後に数秒で `monitor: vkAllocateMemory failed (-2)`・`ZMON DONE frames=18`、窓が消える。
  replay（固定の時計）・p013（system、pointer を動かさない）・p004（touch と key）は PASS。
- 原因: p004 で pointer の位置（`motion.pointer_x/y`）が初めて配線され、camera の傾きの spring（2 Hz、減衰 2ω）が動き始めた。
  `spring` は 1 frame の時間（最大 0.1 秒）を 1 段で積んでいて、ωdt ≈ 1.26 で半陰の Euler が不安定（固有値の絶対値 2.66）、QEMU の
  4〜5 fps では傾きが指数的に発散する。傾きは層の視差で背景の格子の offset になり、`for (x = step + offset; x < width; x += step)` が
  巨大な |x| で x + step == x になって終わらず、矩形を足し続けて vertex buffer の確保が host の memory を使い切った。
- 直し: `space.c` の `spring` を 1/120 秒以下の小さな段で積む（どの frame rate でも安定）。`scene.c` の背景の格子の線を 64 本で打ち切る（守り）。
- 確かめ: host で live の時計・220 ms の frame・pointer を左右に振る harness（preview の改造、scratch）で、直す前は scene の build が終わらず
  （120 秒の timeout）、直した後は 100 frame の間 vertex 40134〜40200 で安定。zedBSD の monitor -Werror warning 0、Linux の object、host 試験 3 本 PASS、
  style 0。QEMU は T1 に monitor-p002・p003 の再試験を依頼。
