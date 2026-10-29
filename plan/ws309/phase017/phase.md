<!-- awesome-plan project=zedbsd record=ws309-p017 -->

# ws309-p017 QEMU の回帰と遅さの注入の host 試験

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §10（試験）。Queue: q1662-i10（P1）。
- 状態: cleared（2026-10-30 q1662-i10 P1。C9 の p052・p076 は WS309 の前の image でも同じ load で FAIL（下）。load の低い時の再試行と実機は未実施）。
- 依存: p003〜p014（cleared）、p016（同じ branch の source で流す）。

## 手順

1. host: WS309 の host 試験の全部（`plan/ws309/tests/*/run.sh`、ASan+UBSan と TSan）と、新しい遅さの注入の試験 `plan/ws309/tests/slow/run.sh`
   （lane の io-file・io-device・present・daemon・hand の pool に 2 s の flush・80 ms の open ×3・2 s の present・accept しない daemon・500 ms の照合 ×4 を
   与えたまま、main の側の give と take が 5 ms 未満で返る事、全ての job が戻る事）。既存の遅さの注入: `io-file`（fsync を遅くする）、
   `input-find`（open 80 ms・EIO）、`daemon-link`（volumed が accept しない間の event loop の呼び出し 5 ms）。
2. QEMU（Venus）: この branch の tree から作った image（`plan/tools/guest/test-image.sh plan/ws309/tests/config-amd64-p1.mk build/ws309/img16 --file …`）で、
   起動（`plan/tools/boot-test.sh`）、WS309 の guest の試験（grave・resize・hand・scanout・rotate・spawn・io-device2・daemon・font・task・io-file・io-read）、
   GPU の境界（`plan/tools/gpu-boundary/forge-guest.sh`・`fence-guest.sh`）、`KWL THREADS` の行と終わりの `KWL PERF` の pass の最大。
   WS099 の基準の C1・C2・C9 は基準の image（`plan/ws099/tests/build-criteria-image.sh`）で `plan/ws099/tests/criteria.sh IMAGE OUT C1 C2 C9`。
3. design §10 の thread の検査（`KWL_THREAD_CHECKS`、main の thread で `vkQueueSubmit`・present・fsync・open・fork を assert）は、compositor を host で
   link する試験が無いので、この Phase では作らない（理由と代わりを「結果」に書く）。

## 結果（2026-10-30 q1662-i10 P1、host の load average は 15〜23 のまま（他の session の Vivado）、Q1 の指示で load の下がるのを待たない）

- host: `plan/ws309/tests/*/run.sh`（lane・io-file・input-find・daemon-link・render-task・hand-pool・clip-write・tr-read・print-file・seat-answer・slow）PASS。
  新しい `slow/run.sh`: lane に 2 s の flush・80 ms の open ×3・2 s の present・accept しない daemon・500 ms の照合 ×4 を与えた 2.5 s の間、give の最大 88 µs（TSan 316 µs）、
  take の最大 189 µs（TSan 122 µs）、全ての job が戻る（PASS）。
- io-file の不安定（`wake byte within 5 s` が load 17〜22 で 3/5・1/6 の FAIL、p018 の前の lane.c でも 1/6）: 原因は wrap した fsync が host の本物の fsync も呼び、
  host の disk の負荷で job が 5 s を超えていた事。直し（acf9bcd32）: wrap は本物の fsync を呼ばない（順序の試験で、耐久は見ない）、wake byte は give からでなく
  job の run の終わりから 1 s 以内を判定（全体の上限 120 s は判定でない）。load 18〜23 で 6/6 PASS。
- QEMU（Venus、image `build/ws309/img16` はこの tree から `test-image.sh plan/ws309/tests/config-amd64-p1.mk`、`plan/ws309/tests/regress-guest.sh`）:
  grave・resize・hand・scanout・rotate・task・io-device・io-device2・io-file・io-read・daemon・spawn-closefrom・spawn・forge・fence PASS（fence は 600 個の fence が
  全て generation 1）。font は 1 回目 FAIL（compositor の 5 s 後に起こした probe が ENXIO、load 17）、単独の再試行で PASS。
  `KWL THREADS count=6 names=io-file,io-device,font,blur,present,display`（daemon の接続の後 11 本）。`KWL PERF pass` の pass_max_us は 57 回の中央値 378667・最大 1373804
  （load 15〜17 の QEMU の数で、基準との比較はしていない）。
- WS099 の基準（image `build/ws309/crit17`、`criteria.sh … C1 C2 C9`）: C1 boot-shutdown PASS、C2 PASS（14/14）、C9 p053・p128 PASS。
  C1/C9 の p126 FAIL: WS309 の退行ではない。539a8c25a（2026-10-29 02:28、他の WS）が uid 1000 を kei から user に改名し、試験が kei の password を打っていた
  （greeter は READY の直後に keyboard を見つけ、打った password は sessiond に `AUTH fail user=user … reason=bad-secret` で届いた）。Q1 の指示で kei を打つ・期待する試験を
  user に直した（b72cd7847）: `plan/ws035/tests/zdesktop-p126.sh`・`zdesktop-p101.sh`・`zdesktop-p102.sh`・`zdesktop-p104.sh`・`zdesktop-p095.sh`・`demo-walk.sh`・
  `p120-kei-mkdir.sh`・`build-demo-venus-image.sh`（comment）・`plan/tools/accounts/user-commands-guest.sh`。実機の image（beta2 の kei）の試験
  （`plan/ws099/tests/resize-hw.sh`・`c5-hw.sh`・`plan/tools/hw5330/*`）と mail の偽の account は変えていない。
  直した p126 を `plan/ws309/tests/p126-probe.sh build/ws309/p126b build/ws309/crit17/hdd-image.img 1` で流して PASS（AUTH ok user=user、HANDOFF go 2 回）。
  C9 の p052（fullscreen の画面が窓の大きさのまま）・p076（popup の reposition と move）・p138（窓の位置）は criteria の中で FAIL、p134・p137・cursor-owner は PASS。
- WS309 の前の image との比べ（WS309 の前の main 69741cc20 の tree を `build/tmp/ws309-base` に展開し、自分の worktree の sysroot を写して
  `build-criteria-image.sh build/crit-base`。各試験は自分の runtime の guest で old と new を続けて、`plan/ws309/tests/zdesktop-compare.sh`）:
  p052 は old も new も FAIL（old は windows.png の時に既に fullscreen、new は 2 つの client の map の順が入れ替わり b が中央、どちらも時間の競争）。
  p076 は old も new も FAIL（`request: invalid initial count ()`、guest の command の答えが空）。p138 は old FAIL・new PASS。
  load 15〜23 の下で old にも出るので WS309 の退行とは見ない。
- p018 の後の compositor（`build/ws309/wayland-p018`）を img16 に入れて grave・resize・scanout・rotate・forge PASS、hand は 1 回目 FAIL（3 画の答えが stale で捨てられた、
  pass_max 2.2 s）・再試行 PASS。
- design §10 の thread の検査（`KWL_THREAD_CHECKS`）は作っていない: compositor の全体を host で link する試験が無く、Linux の build では main の thread の open（seat の
  scan、起動）が正しい使い方として残るため、assert の対象を分けられない。代わりに遅さの注入（slow・io-file・input-find・daemon-link）で main の側の時間を測った。
