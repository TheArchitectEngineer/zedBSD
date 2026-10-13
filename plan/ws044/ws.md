<!-- awesome-plan project=zedbsd record=ws044 -->

# WS044: rpi4 を開発に使える形にする（console の font、FAT32 の boot、lldb）

<!-- awesome-plan-current:start -->
Status: closed（2026-10-13 ユーザー「WS044、WS048、WS118はクローズします。」。旧: planning（要検討・ブロック、2026-10-05）、その前は incomplete）
Primary Milestone: MG008
Related Milestones: MG001, MG003
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

aarch64 は主対象（2026-09-24 ユーザー決定）。rpi4 を、画面で読める console、標準的な boot partition、user program の debugger を持つ開発機として使えるようにする。2026-09-24 ユーザー:「rpi4 でlldbが動くようにしてほしいです。rpi4ではbootパーティションはFAT32にしてください。…rpi4のコンソールのフォントはPC/ATのものを複製して使ってください。」

## 結果（2026-10-13 close）

- console の font（p001）: PC/AT の 8x16（`vgafont`、public domain）の複製に置き換えた（HAL、2026-09-24 承認）。QEMU raspi4b で login prompt を読めた。
- FAT32 の boot partition（p002）: `make-rpi4-hdd-image.py`・検査の script・kernel の FAT の読み取り。QEMU で起動と mount。実機は未実施。
- 実機の起動（p005〜p009、HAL の差分はいずれも 2026-09-24 承認、Guardrail の表）: firmware の読み込み番地と EMMC2 の 32-bit access、framebuffer の data cache の書き出し、1920x1080 の固定、起動の診断（ACT LED・テスト模様・SCTLR_EL1）、HDMI0 の hotplug の設定の誤りの特定、命令 cache の同期と EL0 の SCTLR の権限（init の SIGILL の修正）。実機で HDMI0 の表示と login prompt まで（ユーザー確認）。
- aarch64 の ptrace（p010）: `struct reg`・`fpreg`、HAL の register・step・hardware の breakpoint と watchpoint（`include/hal/arch/aarch64.h` の追加は 2026-09-27 承認）。ptrace の試験 29/29 を QEMU raspi4b と amd64 で。
- 外部 package の aarch64 の cross build の基盤と libcxx（p011）: `ZEDBSD_EXTERNAL_TRIPLE`、arch ごとの work tree、aarch64 の libc++・libc++abi・libunwind。

## 制限・残り

- lldb の aarch64（p003）は未着手のまま canceled。rpi4 で lldb は動かない。
- p001・p005〜p009 の一部（HDMI1・UART、cache・SIGILL の修正の実機の確認）はユーザーの実機の結果待ちのまま。p002 の FAT32 は実機で未確認。
- 規約の全文の確認と rpi4 の回帰（p004）は canceled。
- 残りを続けるなら rpi4 の新しい WS の候補（勝手に移していない）。USB は WS048（同日 close）。
- HAL の承認済みの差分 `proposed/*.diff` は Guardrail の承認の表（`plan/guardrail.md` 78〜81 行）から参照される。消すか残すかは Q1。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws044-p001 | console の font を PC/AT の 8x16 の複製へ（HAL、承認済み） | cleared（q377-i01、QEMU） |
| ws044-p002 | SD の boot partition を FAT32 に | cleared（2026-09-27、QEMU。実機は未実施） |
| ws044-p003 | aarch64 で lldb（plugin の register context、clang＋lldb の aarch64 の package、guest での breakpoint） | canceled（2026-10-13、ユーザーの指示で WS を close。旧: planning） |
| ws044-p004 | 変えた source の規約の全文の確認と rpi4 の回帰 | canceled（2026-10-13、同上。旧: planning） |
| ws044-p005 | 実機の起動の準備（firmware の読み込み番地、EMMC2 の 32-bit access） | cleared（q378-i01、QEMU。実機はユーザー確認待ちだった） |
| ws044-p006 | framebuffer の data cache の書き出し（HAL、承認済み） | cleared（QEMU。実機はユーザー確認待ちだった） |
| ws044-p007 | 実機の表示を 1920x1080 に固定（HAL、承認済み） | cleared（QEMU。実機はユーザー確認待ちだった） |
| ws044-p008 | 実機の起動の診断と boot の設定の洗い直し（HAL、承認済み） | cleared（HDMI0 で表示を確認。HDMI1・UART は実機の結果待ちだった） |
| ws044-p009 | 実機の init の SIGILL: 命令 cache の同期と EL0 の SCTLR（HAL、承認済み） | cleared（QEMU。実機はユーザー確認待ちだった） |
| ws044-p010 | aarch64 の ptrace（HAL の追加は 2026-09-27 承認） | cleared（2026-09-27、QEMU raspi4b と amd64） |
| ws044-p011 | 外部 package の aarch64 の cross build の基盤と libcxx | cleared（2026-09-27） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
