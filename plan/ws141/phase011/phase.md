<!-- awesome-plan project=zedbsd record=ws141-p011 -->

# ws141-p011: R0 preflight ENOTSUP の識別

Status: uncleared
Disposition: normal
Parent: [WS141](../ws.md)
Queue: ws141-r0-preflight-20261011 / finished / Codex

## Scope / authority

ユーザー「R0 not started 21」と全N0ログにより、同じWS141のdisplay修正を継続する。2026-10-11の「今後は自動で統合してください」によりmain統合も担当が行う。原因未特定のため、今回有限attempt r0-i01はsource照合・拒否点の診断補完・arm64 warning0 build・変更規約確認・main統合まで。実機拒否点が判明するまで、条件を推測で緩める変更はしない。p011の不具合解消criterionは未達として保持する。

## Evidence / investigation bounds

SSH kei@10.0.30.2でdmesgをread-only取得（既存承認により再生成されたhost key更新）。N0 clocks ready HSM120MHz/core200MHz、hdmi0→ch1、1920x1080 RGB32 order3、plane8words、PV video on、screen1:1でreadout成功。R0 not started(21)を確認。以後はfirmware ownership通知前のcapture/program validationが候補。map/IRQ preparationの境界も診断に含める。

Linux固定vc4_hdmi.c/vc4_hdmi_phy.c/vc4_hdmi_regs.h/vc4_regs.hとPV interlace/pixel repetition/deep color/scrambler、RM format/PLL divider、AVI設定を照合。複数のENOTSUP guardが同じerrorを返し実値が既存logに無いため、特定原因とは断定できない。今回のcodeは各refusalで項目とraw値を出し、capture済みmodeとprogram-validation failureを分離する。正常経路と拒否条件、MMIOアクセス順、firmware通知前の拒否を維持する。

## Rules / acceptance / next

AGENTS/Guardrail/C全文規約、WS141内のsource/記録と自分のbuildのみ。HAL/UAPI/toolchain/共有Master等は不変。診断のみの小変更なので新mock suiteは作らず、制御flow/フォーマット/全文規約レビュー、warning0 build、diff-checkを確認する。実機更新/再起動は行わない。

実機修復は未確認。次回起動の `R0 reject ...` と `R0 capture error ...` または `R0 program error ...` が再開証拠。partial software attemptとPhase未解消を区別。共有投影はQ1。

## 診断変更のレビュー

R0のmap/IRQ/capture/programを区別し、captureの全ENOTSUP出口へ項目・raw値を追加。正常capture時にはTMDS rate/porches/order/format/AVI headerを記録するため、programのENOTSUP条件も追跡可能。値は既に読んだlocal値を使い、追加MMIO read/writeや拒否条件変更は無い。型/printf引数/失敗return/firmware通知前という境界を目視確認。clang-format19.1.7範囲出力を確認し、規約の多行定義・引数・コメント形式を保持。新試験は追加せず（低影響の診断補完）。

コマンド: `make -j16 build/arm64/vmunix > build/ws141-r0-preflight-final.log 2>&1`、`git diff --check`。SSHの生dmesgはown build/ws141-r0-dmesg.txtのみ、下記は必要なdisplay行だけを抜粋。既存host試験はcapture本体を実行しないため再実行しても本変更の根拠にならず、今回は未実施。QEMU/実機更新/再起動未実施。

```text
rpi4gpu: N0 begin
rpi4gpu: N0 firmware revision 6a7a16af
rpi4gpu: N0 fb 1920x1080 32bpp order 1 pitch 7680
rpi4gpu: N0 fb at 3e402000+7f8000 (handoff 1920x1080 pitch 7680)
rpi4gpu: N0 clocks ready hdmi 120000000 core 200000000 Hz
rpi4gpu: N0 hvs on hdmi0<-ch 1 hdmi1<-ch 3 (3 none)
rpi4gpu: N0 ch0 off mode 0 0x0 list next 0 now 0
rpi4gpu: N0 ch1 on mode 2 1920x1080 list next 1636 now 1636
rpi4gpu: N0 ch2 off mode 0 0x0 list next 0 now 0
rpi4gpu: N0 list words 1636..1644 planes 1
rpi4gpu: N0 plane0 fmt 7 ord 3 1920x1080 at 0,0 ptr fe402000 p 7680
rpi4gpu: N0 pv of hdmi0 on video on 1920x1080
rpi4gpu: N0 pv of hdmi1 off video off 0x0
rpi4gpu: N0 ok hdmi0 ch 1 shows fb 1:1
rpi4gpu: R0 not started (21)
```

## r0-i01の終了（部分scope cleared、Phase uncleared）

最終arm64 build exit0、warning0/error0、ELF/raw-image checker PASS、diff-check PASS。診断補完の有限部分scopeはcleared。p011の原因確定/修復は未達のためuncleared、次回起動の拒否項目/実値で再開する。mainへは承認済み自動統合を実施する。

```text
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
8af016ca5476c4f2ff5e92685d0c86b865b8a32d0446a08502435f044e4683ca  src/drivers/gpu/bcm2711/display-start.c
7acec83ee799251efedc5959367c980ccd335aa7dd9bbb45e82eb41e1bdd532a  build/arm64/vmunix
```
