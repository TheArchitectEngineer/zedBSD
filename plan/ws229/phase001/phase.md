<!-- awesome-plan project=zedbsd record=ws229-p001 -->
# ws229-p001: Gen9・Gen9.5 対応の調べと設計

Status: planned（2026-10-14 Q1、q976、P1）
Disposition: normal
Parent: [WS229](../ws.md)
Queue: q976
依存: 無し

## 範囲（code は書かない、設計文書だけ）

1. 今の i915（`src/drivers/gpu/i915/`、Gen11・Gen12）の中で graphics の版に依存する所の一覧（device-info・forcewake・MOCS・workaround・context の大きさと layout・execlists（Gen11 以降の ELSQ と Gen9 の ELSP）・GGTT・PPGTT・gt-power・irq・render の state の packet（`intel/genxml.h`）・blit・compute・firmware（GuC・HuC・DMC は要るか））。
2. EU コンパイラ: 前段（`src/drivers/gpu/compiler/`）と後段（`i915/compiler/compile.c`・`eu.c`・`intel/eu-encoding-gen12.h`）のうち、Gen9 で使える物と書き直す物。Gen9 の命令の符号化（native と compact）、SWSB が要らないこと、send の SFID と descriptor（data port・sampler・URB・render target の書き込み）、3 src の形、regioning の制約の違い。ユーザーの問い「命令が 2 つペアになっていた」の答え（hardware の co-issue か、compiler が意識する物があるか）を一次資料で確かめる。
3. GOP の framebuffer のままで GT を起こす方法（display の初期化を飛ばす口、今の takeover の経路との関係）。
4. 一次資料: Linux の i915（tree が派生元にしている v6.8.12）と Mesa の intel の compiler（brw）の source を tarball で取得・検証して読む（取り込まない。license に注意し、写さずに仕様として読む）。Intel の公開の PRM（Skylake・Kaby Lake・Coffee Lake の Volume）。
5. Phase の分け方（例: GT の起動・EU の符号化・3D と compute の state・Vulkan の通し・display controller・規約の見直し）、それぞれの見積もり、試験の方法（QEMU では iGPU を試せないので実機。8 世代の Core の機械の有無は Q1 がユーザーに聞く）。
6. 対応できる Core の世代の一覧（Gen9・9.5 に加え、今の PCI ID に無い Ice Lake（Gen11）・Rocket Lake・Alder Lake-S・Raptor Lake-S の desktop の iGPU を足す手間も一覧に）。

## 確かめ

- 設計文書を design-reviewer に通す（Q1）。HAL の API の変更が要るなら差分を plan に置くだけで適用しない。
