<!-- awesome-plan project=zedbsd record=ws141-p011 -->

# ws141-p011: R0の拒否とブラックアウトの修正

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

## main統合（2026-10-11）

自動統合の継続承認に基づきsource `0494ed96d` をclean main `21050e5c0` からfast-forward統合。変更5pathとconfig.mkが検証worktreeと一致することをread-back確認。p007の今回診断sourceの規約/build/統合確認はcleared。p011は原因実値・修復待ちでuncleared、WS incompleteを保持する。次回起動の `R0 reject ...` と続く境界error行が再開証拠。push/実機更新/再起動は未実施、共有投影はQ1。

## r0-i02開始・実機証拠と修正設計（2026-10-11）

ユーザーの追加ログで `R0 reject deep-color=00000204` → `R0 capture error 21` が確定。既存のdisplay修復・自動main統合承認内で有限attempt r0-i02を開始する。範囲はこの誤拒否の修正、実captureの短いhost確認、arm64 warning0 build、p007変更範囲規約確認、main統合まで。r0-i01の診断完了は保持する。実機scanout成功は別の未達条件。

Linux vc4_hdmi.cのvc5設定はCOLOR_DEPTHをbits3:0、INIT_PACK_PHASEをbits10:8と定義し、8bpcにCD=0、10bpcに5、12bpcに6を設定する。Linuxの書く唯一の値に限定していたreadoutが誤り。Altera一次資料 [GCP encoding table38](https://docs.altera.com/r/docs/823533/25.1/gts-hdmi-ip-user-guide/sink-general-control-packet-gcp) でCD=4も明示的8bpc/24bpp、0は未指定、5/6/7は10/12/16bpc、他はreservedと確認。実機0x204はphase2/CD4でRGB8の対象内。CD=0または4だけを許容し、別field・AVI RGB検査・TMDS検査・初期化順は維持する。再設定時は従来どおりLinux同様phase2/CD0へ設定する。GPL実装を転記せず、既存guardと説明だけを変更する。

依存はr0-i01の診断と上記実機値で充足。新しいHAL/API/driver境界変更無し。共有Master/Queue/Guardrail/history/cacheの投影はQ1へ保持。実機更新/reboot/pushは範囲外。

## r0-i02結果（修正software scope cleared、実機scanout未確認）

実際のdisplay-start.cをincludeする一時host fixtureをown build/ws141-r0-depth-host.cへ作成。HSM/PV/videoを成立させ、実機と同じphase2を保ってCD全16値をcapture_modeへ投入し、直後のscrambler guardへの到達で検査した。0/4のみ到達、5/6/7とreservedは事前拒否。beforeは旧guardを戻した一時sourceでcode4が失敗(exit2)、afterは16値すべてPASS(exit0)。scramblerは意図したsentinelであり、完全なmode capture/実機scanoutの成功を示す試験ではない。

コマンド: `cc -std=c99 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Iinclude -Isrc -I. build/ws141-r0-depth-host.c -Wl,--gc-sections -o build/ws141-r0-depth-host`、`build/ws141-r0-depth-host`。今回だけのguard確認fixtureは恒久試験にせずown buildに保持。既存display suiteはcaptureを呼ばないため再実行しない。

`make -j16 build/arm64/vmunix > build/ws141-r0-depth-build.log 2>&1`: exit0、warning0/error0、arm64 image checker PASS。clang-format19.1.7で変更範囲を確認し、C全文規約のコメント/guard/段落/副作用順を確認。git diff --check PASS。変更は説明コメントと1条件のみ、field mask維持、追加MMIO/外部code転記/ABI変更無し。再設定側0x170=phase2/CD0、0x17cのGCP設定も変更不要と確認。

```text
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
a77ee65bd528b01c7560f7e8557ca0f80f12dc29666844bf0bf2fcb1c69ebc6b  src/drivers/gpu/bcm2711/display-start.c
1bb6df3c4b10d24f0444484455bc1b1cc88d10312e42346e7d3ec21a98a6296d  build/arm64/vmunix
```

誤拒否修正の限定attempt r0-i02はcleared。p011全体は実機R0成功未確認のためuncleared、WS incomplete。次はこの版で起動しR0 ok、または次のreject/error項目を観測する。push/実機更新/reboot未実施。共有投影はQ1に保留。

## r0-i02 main統合結果（2026-10-11）

source/検証記録 `75813e358` をclean main `acf20e54c` からfast-forward統合。変更4pathとconfig.mkが検証worktreeと一致しmain cleanをread-back確認。自動統合承認を適用。実機修復は未確認のまま、次回起動ログで継続する。共有投影はQ1、push未実施。

## r0-i03開始: 表示終了通知後のブラックアウト（2026-10-11）

ユーザー「R0 begin ... このあとブラックアウトしてフリーズ」に基づき、同じdisplay修復を継続する。SSH read-onlyは成功、init/V3D render nodeまで進んでおり全カーネル停止ではなかった。host keyは既存承認の範囲で更新。生ログはown build/ws141-r0-blackout-dmesg.txt、必要箇所:

```text
rpi4gpu: R0 mode 1920x1080 tmds 148500000 order 3 format 1
rpi4gpu: R0 porches h 88/44/148 v 4/5/36
rpi4gpu: R0 begin hdmi0 1920x1080 tmds 148500000
rpi4gpu: R0 waits 3000 ms before writing
rpi4-firmware: tag 30066 not supported
rpi4gpu: R0 failed op 0 error 21
rpi4gpu: R0 not started (21)
```

p011を実機で判明したR0通知段階へ進める。r0-i01/i02の履歴と部分clearanceは保持。有限範囲は通知応答の扱い、既存executor host確認、arm64 warning0 build、p007差分規約確認、自動main統合。実機更新/reboot/pushは行わない。表示開始・Keiland実機関門は保持。

transport read_answerはmessage全体SUCCESSを確認した後、tagのresponse bitが無ければENOTSUPを返す。したがって今回はmailbox timeout/全体拒否ではない。Linux [rpi-6.12.y raspberrypi.c](https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.12.y/drivers/firmware/raspberrypi.c) のproperty_listは全体statusだけで成功とし、固定vc4_drv.cもdisplay-done送信後component bindへ進む。今回の通知はpayload0なのでtagから値を読む必要も無い。display-doneの呼出箇所だけENOTSUPを非致命として明示logし、geometry確認と再初期化へ進める。transport/他tagの意味は変えず、全体拒否EIO・timeout等は引き続き停止する。Linuxと同じ再初期化という既存ユーザー方針内の応答互換修正。表示終了通知でfirmware出力が消えた後に誤って中断したことが今回のブラックアウトに整合するが、修正版での実機復旧は未確認。

## r0-i03結果（software部分cleared、実機R0未確認）

変更はdisplay-execute.cの通知呼出後の判定と診断のみ。ENOTSUPの場合も既存verify_framebufferを通し、変化ならEIOで停止。通知の再送はしない。初期化操作列・全体エラー時停止・mailbox本体は変更無し。

未完了WSの初期scanoutで継続使用する `tests/display-execute-host-test.c` に成功message/tag応答無し、EIO、timeout、応答無し後のframebuffer変化を追加。実executor/生成器/IRQのhostモデルでPASS。旧executorで同じ試験を実行すると今回の経路がFAIL3（終了1）、修正版でPASS（終了0）。実機の電気的なscanoutは証明しない。全display suiteやQEMUは未実施。

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -include time.h -Wall -Wextra -Werror -Iinclude -Isrc -I. plan/ws141/tests/display-execute-host-test.c src/drivers/gpu/bcm2711/display-program.c src/drivers/gpu/bcm2711/display-execute.c src/drivers/gpu/bcm2711/display-irq.c src/drivers/gpu/bcm2711/display-flip.c plan/ws141/tests/display-lock-host.c -o build/ws141-r0-notify-host
build/ws141-r0-notify-host
make -j16 build/arm64/vmunix > build/ws141-r0-notify-build.log 2>&1
```

arm64 build exit0、warning/error0、image checker PASS。clang-format19.1.7範囲出力を比較し、規約の一引数一行/既存定義を優先して無関係なformat差分を避けた。C全文規約で追加guard/comment/短絡順・所有・returnを確認、git diff --check PASS。HAL/UAPI/toolchain変更無し。

```text
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
0b96e8eb1178912b17fe26690e2a90a99e282b3422b333d6e49a32a118cbab49  src/drivers/gpu/bcm2711/display-execute.c
428ddc80c9d312a14810d944b56517c3297304927a263a6451b9fd2691dcdaa1  plan/ws141/tests/display-execute-host-test.c
28fdba0a3a9df120fe9cf8863127801718b93fd61e787be267e8c0cca1b2b775  build/arm64/vmunix
```

r0-i03の限定software scopeはcleared。p011は次回実機R0 ok（または次の失敗段階）待ちでuncleared。SSHで実機が生存していた証拠と表示修正後の実機検証は別であり、後者は未実施。共有投影はQ1へ保留。自動main統合を行う。
