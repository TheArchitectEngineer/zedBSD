<!-- awesome-plan project=zedbsd record=ws288-p010 -->

# ws288-p010 compiler の backend（共有の IR → GSI1）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §6.6（IR の op の対応表）、§3.4.4（binary）、§8 の p010 の行、§16（p030: U50・U51 で compiler に足した物）、§15（p027 の fragment の入出力 r6〜r9・exp 0x105）。
- 状態: in-progress（2026-10-25 05:56 q1564 P2）
- 依存: p008（GSI1）、p009（texture の命令の全部）、p027（MSAA の入出力、main に merge 済み）、p030（format・MRT・point/line などの GHCI の口。RTL は並行で実装中、compiler は GHCI の draft の仕様に合わせる）。
- 承認: 2026-10-25 ユーザー「SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…N=3で、P3を立ててOKです。」

## 範囲

- zedBSD の kernel の共有の compiler（`src/drivers/gpu/compiler/`: `ir.h`・`spirv.c`・`optimize.c`）の IR から GSI1 への backend（design §6.6 の `isa/gsi1/`、置き場所は既存の backend（nvrtx・amdrdna・powervr）の形に合わせる）。§6.6 の対応表の全部の op、register の割り当て（r0〜r62、r63 は rq の予約）、EXEC の制御の流れ（loop・skip・kill）、texture の命令の全部（`.c`・`.3`・offset・gather・query・`texsz.l/.d/.s`）、vertex の出力と fragment の入力（lda、MSAA の r6〜r9、exp 0x105 の終わりの規則）、MRT の export（COLOR k = 0x100+16k+c、p030 の GHCI §12.12）。
- **`gsi1_isa.h` の binary の header と interface の表の確定**（C1、p018 が依存）: `docs/ghci.md` §12.10 の「未確定」の 4 語目の配置を決め、amaterrace-gpu の GHCI の draft も直す（amaterrace-gpu の worktree で）。
- p030 の U50（RTL の外の core の欠け: storage image・texel buffer・FragDepth・SampleMask の SPIR-V の front end）と U51（input attachment は memory の読み `lw`）の compiler の側。
- host の試験: 符号化の往復（amaterrace-gpu の `tools/gsi1dis.py` と同じ結果）、model で実行して CPU の参照と比べる（amaterrace-gpu の `tools/gsi1model.py`）、interface の表の検査。

## 受け入れ

host の試験が PASS（件数を記録）、zedBSD の vmunix の build が warning 0（名指しの target）、plan/coding-style.md の全文に合う。QEMU は使わない（p011 で Verilator の上の端から端）。

## 記録

- 2026-10-25 05:56 Q1: 新設、P2 に（q1564）。
- 2026-10-25 06:58 P2 → Q1: 段 1（GSI1 の backend の本体）zedBSD agent/p2-p010 6fd59cc1f、amaterrace p2-p010 e609dc7。`src/drivers/gpu/ghci/compiler/`（gsi1_isa.h の写し、encode・compile・io・memory・texture・image）、共有の front end に feature の bit つきの `drv_gpu_shader_parse_features`（既存の `drv_gpu_shader_parse` は features 0 で従来どおり）、SampleMask・FragDepth・Centroid・QuerySamples・storage image・texel buffer・input attachment を GHCI だけに開放。host 試験 `plan/ws288/tests/gsi1-host-test.sh` 26/26 PASS（P2 の申告）。P2 の判断: skip の block は分岐しない（SELECT の parser の約束のため）、noperspective・FragCoord.w は vertex が clip w を余分の varying に出す、VertexIndex・InstanceIndex は interface kind 13。
