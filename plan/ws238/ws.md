<!-- awesome-plan project=zedbsd record=ws238 -->

# WS238: amd64 の native の image を ESP（loader だけ）・FAT32（kernel と boot の設定）・UFS の root・swap の構成にする

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 夜 ユーザー指示、ベータ2 に入れる）
Primary Milestone: MG004
Parent: [Master](../master.md)
Related: [WS062](../ws062/ws.md)（今の native の layout: ESP に vmunix・zedbsd.cfg）、[F-014](../future-work.md)
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「1. native の layout を、ESP（BOOTX64.EFI だけ）・FAT32（vmunix・zedbsd.cfg・logo.ppm）・UFS の root・swap にする。zedimage-host の native と、platform/amd64/vmunix.mk を変えます。2. loader の探索の順を確かめる。ESP の次に FAT32 を見つけて、そこから load できることを試験します。3. 文書を直す。利用の手引きの「vmunix と zedbsd.cfg のある小さな FAT partition」の説明や、i915.gen9=off の手順も、FAT32 の側を指すように直します。4. QEMU（UEFI）と、5330 の素の起動で確かめる。

この修正をお願いします。配布イメージの変更がメインで、ブートローダは運がよければ変更なしだと思います。
修正の理由は、ネットワークが使えなくなったときに、dmesgをFAT32にコピーしたいのですが、ESPだとWindowsでマウントが難しいからです。」

## 目標

- 上の 1〜4。FAT32 は Windows・macOS で普通に mount できる partition（GPT の Basic Data の type、volume label 例 `ZEDBSD`）。zedBSD の中から FAT32 に書ける（dmesg を写せる）ことも確かめる。
- 追加（2026-10-14 ユーザー）: 「/boot/boot0がESPからFAT32に変更されたことも確認してください。boot0に加えて、ESPが/boot/efi でマウントされたらうれしいです。ESPがboot0のとき、可能ならefi, boot0で同じディスクをマウントするのがいいです。でも、もし難しければ、boot0だけでもいいです。」

## Phase

| Phase | 目的 | Status |
| --- | --- | --- |
| [ws238-p001](phase001/phase.md) | native の layout（ESP・FAT32 `ZEDBSD`・UFS・swap）、`/boot/boot0` と `/boot/efi`、文書と道具の追従、試験の手順 | in-progress（実装と host の確認まで。T1・5330 待ち） |
- 2026-10-15 T1-638（T2、QEMU UEFI）PASS: ESP・ZEDBSD（FAT32）・UFS・swap、/boot/boot0 と /boot/efi の mount、dmesg を FAT32 に書いて host の mtools で読める、fsck.fat 両方 rc 0、AAT smoke 8/8。残りは 5330 の素の起動（ユーザー: Windows で ZEDBSD の drive に dmesg.txt）。
