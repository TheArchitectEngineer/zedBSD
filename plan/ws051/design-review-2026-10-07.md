# WS051 design.md 第 4 版（§14）の敵対的レビュー（2026-10-07、P2 が起動した design-reviewer の報告の要約）

読みのみ。反映は未（BUG-243 の UAT を先にした、2026-10-07 Q1）。次の作業で §14 と §7・§8・§9・§12、ws.md を直す。

## 高

- H-1: p002 の受け入れの「host の試験」は無い。TC PLL の表と DVO の写像を確かめるのは in-kernel の ktest（`ktest-display-probe.c`、`I915_TESTS=y`、5330 の VFIO）だけで、
  P5A-DPLL（`:2039`・`:2041`）は今の誤った番地を期待値に持つ。P5A の期待値も直し、受け入れを (a) `I915_TESTS=y` の build warning 0 と 5330 が戻った後の T1 の
  ktest、または (b) sed で取り出す本物の host 試験（host-gop.sh の形）に。
- H-2: M5（GOP が USB-C の時の引き継ぎ）を p003 に置くと、`i915_resident_takeover` が firmware の crtc を全部止め、TC の出力は p004b まで点けられないので、
  firmware の USB-C の画面を消す（規則に反する）。M5 は p004b の後。p003 は readout だけで判定は OTHER（absent）のまま。

## 中

- M-1: `intel/vbt-defs.h` は vbt.c 専用（`_INTEL_BIOS_PRIVATE`、Linux の型、static const の表）で takeover.c から include できない。takeover.c の値を Linux の値に
  直して根拠を comment に書くか、vbt.c の `i915_dvo_port_to_port` を `vbt-parse.h` から公開する。code 0〜24 で 2 つの写像が一致する試験を足す。
- M-2: §14.4 の誤り: WS084 の N1 の readout は lcd world の combo の 2 つの pool で、TC PLL を含まない。p002 の表の修正は P5d の probe 時の sanitize
  （`drv_i915_nogem_dpll_sanitize_state`）に効く（WS084 p004 の乖離 2）。
- M-3: 正解値の採取は iGPU を host の i915 に付け替える（`igpu-mode.sh host`）必要があり「host の設定は変えない」と両立しない。手順（lock、切り替え、挿す、
  読む、戻す、kernel の版の記録）をユーザーに確かめる。§7 の「6.8.12」も直す。
- M-4: §7・§8・§9・§12 と ws.md に古い記述（VFIO、向きの受け入れ、p002 の GOP の引き継ぎと実機の受け入れ）が残る。置き換えを明記する。
- M-5: WS113 p011（出力ごとの資源）との重なり、WS113 p002 が未 clear。p004b の依存に ws113-p011、p002 と M5 の依存に ws113-p002 の clearance。
- M-6: typec の listener の unregister が無い（i915 の fini で use after free の恐れ）。`CONFIG_DRIVER_TYPEC` の分岐を i915 が呼ぶ typec の関数の全部に。callback は
  work を積むだけで tc の lock を取らない（M8 に）。
- M-7: GOP が panel と USB-C を clone した場合（推測）の扱いが無い。UAT の (4) に項目を。

## 低

番地の式は「0x46030 + 4(n−1)」。`mreg.h` に `ADLP_PORTTC_PLL_ENABLE` が無く p003 の `dkl_pll_funcs` の移植で ADL-P の分岐を必ず移す。nogem の sanitize の disable は
不完全（TC2 が対象に入る）。QEMU の boot test は p002 の確かめにならない。absent の判定は試験されず、absent の時は session に display が無い（制限として書く）。
P5B の `hdmid == NONE` と Linux の `PORT_D_XELPD` の扱いを決める。WS050 ws.md の p005 の依存は ws051-p002b に（Q1 へ）。WS084 p004 の依存は p003。
記録の追従（ws.md の Resume point、phase001 の Status、p006 の依存の名前、design.md:39 の §9 → §7）。

## 問題の無かった点

TC PLL の番地（0x46038 + 8(n−1)、ADL-P に TC1〜4）、他に番地を使う所は無い（takeover.c の表と diagnostics.c の 2 つ）、DVO の値、WS113 p002 part A の実装、
§14.3 の口の宣言、§14.1 の要旨。
