<!-- awesome-plan project=zedbsd record=ws141-p012 -->

# ws141-p012: KeilandのARM64 counter-read例外

Status: cleared
Disposition: normal
Parent: [WS141](../ws.md)
Queue: ws141-el0-counter-20261011 / finished / counter-i01 / Codex

## Scope / authority / criteria

WS141をKeiland表示まで完成させる継続指示に従い、R0成功後の次の実機障害を有限scopeで修復する。範囲はEL0の仮想counter読出しに必要な既存ARM64初期化の補完、AArch64実命令の照合、arm64 warning0 kernel build、p007最終差分review、承認済み自動main統合。HAL APIは不変、既存規則のHAL実装修正許可を適用。main/共有toolchain/build/計画を直接編集しない。実機配置/rebootは今回行わない。物理Keiland表示は未確認として保持する。

依存: p011のR0実機成功、p005/p006のV3D登録とuserspace初期化。user/kernel/ELF三者の既存codeが証拠。counter-i01のsoftware部分と物理再起動後の例外消失を区別する。

## Evidence / design

実機SSH read-onlyでGPU0/1登録、greeter.logのdevice=`zedBSD V3D 4.2 (Keiland graphics)`、KWL READYまで到達を確認。sessiondはstatus10で再試行し、現在dmesgはpid23/39/40/43/45/46/53がsignal10/vector24/pc0x29e3c/far0で終了。

実機/bin/waylandをown build/ws141-rpi4-waylandへ読出し、SHA256=16b305cdd2be434a0cdabd4c55dbd6cd5b64ea5428b8be508da8fb9096001bc4。PIEのload biasはuser minimumの0x1000、ELF offset0x28e3c + bias=実機PC0x29e3c。llvm-objdumpでこの命令は `d53be05b mrs x27,CNTVCT_EL0`、mainのkwl_os_poll_fill後にinlineされたkwl_cycles。例外EC24=0x18はsystem register trap。src/hal/arm64/locore.SはEL2 CNTHCTLを設定しているが、EL1 CNTKCTLの初期化が無い。

[Arm Generic Timer guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Generic%20Timer.pdf?revision=c710e7a7-9f52-4901-8c9d-91b19f44f9c7)と[Linux arm_arch_timer.c arch_counter_set_user_access](https://raw.githubusercontent.com/torvalds/linux/master/drivers/clocksource/arm_arch_timer.c)を参照。EL1でCNTKCTL_EL1を2（EL0VCTENのみ）に確定しISBで同期、EL0のCNTVCT/CNTFRQ読出しを成立させる。timer controlへのuser書込み、event streamは有効化しない。EL1直入とEL2から降りる両経路が合流するlocoreで設定する。現行RPi4は1CPUでsecondaryはparkのまま。

## Verification bounds / rules

AGENTS/GuardrailとC全文の適用範囲、既存assemblyの規約を使用。変更はassemblyの初期値設定のみで外部code転記無し。新mock試験/ツールは作らず、実機ELF命令照合、生成kernelのCNTKCTL命令列/両entry経路review、image checker/warning0 build、diff-checkを行う。QEMU/実機再起動は未実施。共有投影はQ1へ保持。

## counter-i01結果（software部分cleared、実機待ち）

locoreのEL1共通経路にCNTKCTL_EL1=2とISBを追加。scratch x1のみ、stack/MMU確立前の既存system-register初期化と並べ、EL1直入とEL2経由の双方を覆う。hal.h/UAPI/ユーザー側/driverの変更無し。primary資料を意味の照合に使いsource転記無し。

`make -j16 build/arm64/vmunix > build/ws141-el0-counter-build.log 2>&1` exit0、warning/error0、image checker PASS。生成kernelを `build/llvm/bin/llvm-objdump -d --disassemble-symbols=arm64_el1_low build/arm64/kernel.elf` で確認し、MOV x1,#2 → MSR CNTKCTL_EL1,x1 → ISBを確認。EL2のCNTVOFF=0/CNTHCTL設定とEL1経路を目視review、git diff --check PASS。assemblyはC formatter適用外。追加のmock試験/QEMU/実機更新/rebootは未実施。

```text
56c11753594045c9edce375c3eefa3dd0c3f9bfb50de3e5ea81867d380760c06  config.mk
72a3b5edbc371ca7c5a8b9c6967758bc9d3e6a819cd39ec9b02c9406bd780483  src/hal/arm64/locore.S
2c674080e48cf499786decf72a8e337b64ae0eeaf3c2a95e4737f6daaf4da1f1  build/arm64/vmunix
```

限定software scope counter-i01はcleared、Phase全体は実機のSIGBUS消失/Keiland進行待ちでuncleared。次回カーネル更新・起動後に同じfault PC消失とKWL表示を確認する。p005/p006の実機受入れを肩代わりしない。自動main統合を行い共有投影はQ1に残す。

## main統合（2026-10-11）

修正 `823f116f5` をclean main `9c05da58a` からfast-forward統合。変更9path/config.mkの一致とmain cleanをread-back確認。実機更新/reboot/pushは未実施、次は新kernelでのKeiland例外消失確認。

## counter-deploy-i01: 実機配置の承認（2026-10-11）

Queue: ws141-counter-deploy-20261011 / active / counter-deploy-i01 / in-progress / Codex。
ユーザー「カーネルは実機に送ってください。私が電源OFF/ONで再起動します。」を正確な配置承認として記録。上記の未配置方針はこの追加指示で更新。範囲はビルド済み823f116f5のkernelをkei@10.0.30.2へ転送、現行kernelをUFSへ退避、FATへ段階的配置、sync、read-back照合まで。agentは再起動しない。物理再起動とKeiland結果はユーザー待ちでPhase unclearedを維持。
FAT空き13645KiB、UFS空き99880KiBを確認したため、現行kernelの追加バックアップはUFSへ置く。既存FATのr0-notify前backupは維持。新kernel SHA256=2c674080e48cf499786decf72a8e337b64ae0eeaf3c2a95e4737f6daaf4da1f1、8077312bytes。

### 配置結果: cleared

counter-deploy-i01 / Queue ws141-counter-deploy-20261011はfinished/cleared。転送先/tmp/vmunix-823f116f5.newのcksum=3003282929 size=8077312でown build一致。旧kernel（cksum4051373307 size8077312）をUFS /var/backups/vmunix.before-el0-counter-20261011にコピーしcmp/sync成功。FAT /boot/boot0/vmunix-823f116f5.newへcopy/cmp/sync後にvmunixへrename、sync/cmp成功。起動設定kernel=vmunix、kernel_address=0x80000を確認済み。
実機/boot/boot0/vmunixをown build/rpi4-vmunix-after-el0-counter-20261011へread-backし、build/arm64/vmunixとのcmp exit0、SHA256=2c674080e48cf499786decf72a8e337b64ae0eeaf3c2a95e4737f6daaf4da1f1一致を確認。全操作exit0。既存FAT backupも保持。agentによる再起動は未実施。次はユーザーの電源OFF/ON後にSIGBUS消失とKeiland表示を確認する。Phase全体uncleared、WS incompleteを維持。

## counter-verify-i01: 再起動後のSSH確認（2026-10-11）

Queue ws141-counter-verify-20261011 / finished / counter-verify-i01 / cleared。ユーザー「起動しました。SSHで確認をお願いします。」によるread-only確認。起動partitionのcksum3003282929 size8077312が配置版と一致。現在dmesgにsignal10/vector24のcounter trap無し。自動ログインpid23はKWL READYに続きHANDOFF ready=1、OUTPUT open 1920x1080/images3/format37と描画送信まで到達。counter例外を実行した経路の通過を確認したためp012をclearedとする。

一方、実画面の描画は未達。/run/user/1000/session.logのpid23はoperation=submit result=-4、site=compose_draw errno5、frames0/error5/cleanup_failed1、exit status256。greeter pid39/53も同じsubmit失敗。現在dmesgではV3D識別registerの再出力（回復経路）を確認するが、原因のjob/register値は記録されておらず、timeout/コマンド/compiler等の原因確定はできない。旧greeter.log/sessiond.logには以前のsignal10も残るため、今回の起動境界とpid/新規出力を区別した。kernel/SSHは応答継続。Vulkan描画失敗は既存p006の未達条件として保持し、WS全体はincomplete。

生のread-only証拠はown build/ws141-counter-boot-ssh-20261011.log。確認のみで実機のファイル更新・サービス再起動・再bootは行っていない。共有投影はQ1。
