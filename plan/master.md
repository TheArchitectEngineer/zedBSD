<!-- awesome-plan project=zedbsd record=master -->

<!-- awesome-plan-current:start -->
Active Queue: なし（2026-09-27 から subagent の運用。実行の状況は [queue.md](queue.md) の Executor の行）。
Current Focused Goal: fg010 — Kei Operating System の Keiland（Wayland）を 2026-10-17 の OSC Tokyo Fall のデモに向けて仕上げる。
Next（2026-09-28 の周期の終わりに整理）: リセットの後に N≈3 で再開 — Keiland（F-044 → F-041、network の group）、WS075 i915（p006 の stencil を wip.patch から）、
WS074 ブラウザ（p019 の guest の確認）、WS073（BUG-082 の hang → BUG-080 の Desktop の分類）。main は WS078 の残り（Venus の確認、p006・p002・p004）。
<!-- awesome-plan-current:end -->

# zedBSD Master

[GitHub Project](https://github.com/users/awemorris/projects/2) ·
[Queue](queue.md) · [Guardrail](guardrail.md) · [Future Work](future-work.md) ·
[Bug Board](known-bugs.md) · [Past Log](history/index.md) · [設定](config.md)

## 目的・利用者・最終成果

- **目的**: 寛容なライセンスで企業が自由に使える UNIX 互換 OS を、GPL の Linux kernel に依存せずに作る。
- **利用者**: OS を組み込んで独自のディストリビューションを作る開発者・企業と、デスクトップ・ラップトップ・SBC で使う個人。
- **最終成果**: Linux/Android を置き換えられる水準のカーネルとユーザランド、最小の HAL による移植契約、用途別に構成・配布できる仕組み。
- **範囲**: kernel、HAL、driver、libc、base の userland、デスクトップ（zdesktop）、外部 package のクロスビルド、インストーラ、文書。
- **範囲外**: Linux の kernel ABI・DRM の互換、Mesa 流の user mode driver、正式な UNIX 認証・Vulkan CTS 認証の取得（主張しない）。
- **制約**: HAL の変更は差分ごとの事前承認（[Guardrail](guardrail.md)）。独立実装とライセンスの境界（[設計方針](master-design-policy.md)）。

## Objectives

- **O1**: 寛容なライセンスで企業が自由に使いやすい UNIX 互換システムを、GPL の Linux kernel に依存せず、Linux/Android を置き換え可能な水準で提供する。
- **O2**: デスクトップ、ラップトップ、SBC、タブレット、モバイルなど様々な規模で動くカーネルとユーザランドを提供し、開発者が独自ディストリビューションを自由にカスタマイズ・リブランディング・配布できるようにする。
- **O3**: UNIX/BSD/Linux の遺産から現代のシステムに必要なエッセンスを抽出し、networkd、netconf、service などをシンプルで一貫した仕組みとして再実装する。
- **O4**: ページベース MMU を備える 32bit/64bit コンピュータへ UNIX 互換 OS を確実に移植できる、明確で最小限の HAL を定義し、人類の共有知とする。
- **O5**: AI 時代の OSS のあり方を、大規模な AI 活用開発を通じて探索し、成果・失敗・人間の判断を再利用可能な知見として共有する。

## Milestone Goals

Milestone の達成は所属 WS の完了数ではなく、到達点の証拠で判定する。現時点で completed の Milestone は無い。

| Milestone | Objective | 受け入れの核 | 進捗 | Primary WS |
| --- | --- | --- | --- | --- |
| **MG001** 継続開発できる基盤 | O4, O5 | 文書化した環境で build でき、設計境界・規約・試験・制限を追跡できる | toolchain（WS021）・build tool（WS010）・x86 HAL の規約（WS023）は完了。文書（WS009）と試験資産の整理（WS026）が残る。vmunix の LTO（WS053）は完了 | WS009, WS010, WS021, WS023, WS026, WS047, WS053 |
| **MG002** UNIX アプリケーションの実行基盤 | O1 | process・memory・libc・loader/TLS の対応範囲を互換性台帳と代表アプリで確認できる | TLS（WS022）と外部 package の導入（WS032）は完了。base の utility の POSIX 化（WS043）は完了。POSIX 台帳（WS001）、アプリ導入（WS034）、sh（WS042）が進行中 | WS001, WS022, WS032, WS034, WS042, WS043, WS045, WS046, WS061 |
| **MG003** 対象機へ導入して単独起動 | O2, O4 | 合意した機種・媒体でインストール後の単独起動と login を確認できる。実機と QEMU の証拠を分ける | インストーラ（WS019）と Intel Mac（WS020）は完了。4 機種の実機受け入れ（WS028）が残る | WS003, WS004, WS019, WS020, WS028 |
| **MG004** データの保持とメモリ/ストレージの実用 | O1, O2 | 永続化、低メモリ時の進行、媒体世代、既定構成の性能を確認できる | swap（WS016）、UFS（WS024）、I/O・cache（WS025）は完了。実機の性能の一部は未測定。UFS の directory は 12 block まで育つ（WS054、完了） | WS016, WS024, WS025, WS054, WS057, WS058, WS059, WS060 |
| **MG005** 一貫したネットワーク/サービス管理 | O1, O2, O3 | networkd・netconf・service の責務・設定・操作が一貫し、永続化と失敗後の復旧を確認できる | サービス（WS002）、net console（WS011）、service console（WS012）は完了。有線 LAN の常駐管理（WS005・WS033）が残る | WS002, WS005, WS011, WS012, WS033 |
| **MG006** グラフィカルな操作環境 | O2 | 入力・描画・ウィンドウ・端末・GUI ツールの一連の操作を確認できる | 入力（WS006）、Noct/BeUI（WS008）、標準 Vulkan（WS030）、即時起床（WS041）は完了。**Wayland デスクトップ（WS035）が fg010 の中心** | WS006, WS007, WS008, WS014, WS017, WS029, WS030, WS031, WS035, WS037〜WS039, WS041, WS068 |
| **MG007** 用途別の独自ディストリビューション | O1, O2 | 第三者が用途別に構成し、独自ブランドで build・配布できる | 担う作業は一部だけ（WS013・WS015 は Future Work に保留）。未充足 | WS013, WS015 |
| **MG008** 最小 HAL の移植契約と異種機での実証 | O4 | HAL 契約・移植手順と異種/レトロ機での実証を公開する | source の所有の整理（WS018）と時間の単位（WS040）は完了。他 platform への反映（WS036、aarch64 を含む）と PowerPC（WS027）、rpi4 の開発環境（WS044）が残る | WS018, WS027, WS036, WS040, WS044 |
| **MG009** AI 活用 OSS 開発の知見の公開 | O5 | 設計権限・レビュー・変更追跡・失敗からの回復の事例と根拠を公開する | 担う作業が未定義 | なし |

## Current Focused Goals

| Goal | 当面の成果 | Milestone | 担当 | 出典 |
| --- | --- | --- | --- | --- |
| **fg010** | **2026-10-17 の Open Source Conference Tokyo Fall のデモに向けて、Wayland デスクトップ（zdesktop）を完成させる** | MG006 | [WS035](ws035/ws.md)（zdesktop・合成・タスクバー）。GPU の土台は [WS014](ws014/ws.md)・[WS031](ws031/ws.md) | 2026-09-24 ユーザー指示 |

デモの platform は amd64（QEMU と実機）と想定している（仮定。ユーザーの確認が要る）。以前の focus（fg004 インストーラの実機、
fg005 有線 LAN、fg007 HAL の可読性、fg009 PowerPC）は定義を残すが、現在は優先しない。

### fg010 に必要な判断

なし（2026-09-28 の時点）。合成の設計（p051）は承認済み、HAL の quiet console の diff は 2026-09-28 に承認・適用済み。

## WS の優先順位

依存による実行順とは別のもの。Queue の権限は変えない。2026-09-28 に整理（それ以前の順は git の履歴にある）。

**運用（ユーザー、2026-09-27〜28）**: 作業用のサブエージェントを N=1〜4（通常は 3〜4）。5 時間の枠を 1 周期とし、枠の終わりに N を減らし、
N=0 になったら実装をまとめて計画（master・ws.md・queue・Future Work・Bug Board）を整理する。試験は amd64 だけ、Phase の終わりに。

1. **Keiland と名前**: WS035（Keiland の compositor・システムバー・login・lock）、WS078（Kei Operating System への改名）、
   WS071（File Manager、完了。続きは WS035 の Phase と Future Work）。
2. **グラフィック**: WS075（i915 の高度化: p006 の MRT・query・storage buffer の後、stencil・multisample）、
   WS068（GL 3.2 まで。3.3 以降は保留）。
3. **ブラウザ**: WS074（HTTP・HTTPS まで。p019 libjpeg-compat、p050 非同期の loader）。
4. **bug**: WS073（BUG-082 の hang、BUG-080 の Desktop の分類、BUG-039・031・051）。BUG-027・033 は低い優先度（計測して閉じる）。
5. **ACPI（WS049〜WS052）と Arm64（WS044・WS048・WS036）**: デスクトップが片付くか limit が余るとき。
6. **WS001** はユーザーが指示したときだけ。WS077（PC-98 の PCI）・WS066（ld.so の最適化）は低い優先度。

## Upcoming Work Outlook

見込みであって、約束や実行許可ではない（2026-09-28 に整理）。

| 候補 | 理由 | 準備 |
| --- | --- | --- |
| WS078 の Venus の確認（graphical な login、`/bin/wayland`、App Home から terminal・files・browser） | p003 の未実施の確認 | image は build できる |
| ws035: session の user を `network` の group に（p013 の残り）→ F-044 → F-041 の名前の衝突の dialog | fg010 | ユーザーの決定あり |
| ws075-p006: stencil（`phase006/wip.patch`）→ multisample・resolve | i915 のグラフィック | 実機の VFIO |
| ws074-p019: guest の比較と boot test → p020 以降 | ブラウザ | resume の手順は phase.md |
| WS073: BUG-082（`bugs/BUG-082-wip.patch`、sync と cat の hang）→ BUG-080（Desktop の分類） | kernel の panic と menu | 再現の script がある |
| WS078: p006（data の path・API・`keiland_` の protocol）、p002（kernel・UAPI・libc・bootloader の識別子）、p004（見える文字列と Kei の logo） | 改名 | 対応表は ws.md |

## Tools

回帰と観察の道具は `plan/tools/` に置く。完了した WS の試験は、ここへ移したもの以外を削除した。Phase に固有の試験は各 WS の `tests/` にある。

| tool | 用途 | 使い方 |
| --- | --- | --- |
| [boot-test.sh](tools/boot-test.sh)（`boot-test.py`） | 起動の確認。OVMF の USB（amd64）か BIOS の IDE（i386）で起動し、画面を QMP で撮って login prompt を読む | `plan/tools/boot-test.sh [IMAGE]`。`OUTPUT`（既定 `build/boot-test`）、`BOOT_TIMEOUT`、`BOOT_MODE=uefi-usb` か `bios-ide` |
| [pc98-boot.py](tools/pc98-boot.py) | pc98 の起動の確認（`boot-test.sh` に PC-98 の mode が無いため）。PC-98 fork の QEMU で起動し、text VRAM で login prompt を読み、root で login して `uname -a`。画面を text と PNG で残す。WS053 から移した | `pc98-boot.py ~/qemu-pc98/build/qemu-system-i386 IMAGE OUTPUT`（`clock/pc98-sleep.py` の `Guest` を使う） |
| [guest/guest.sh](tools/guest/guest.sh) | SSH による guest の操作（USB CDC-ECM、KVM）。コマンドの実行・file の送受・lldb・kgdb・画面 | `start IMAGE`・`wait`・`run CMD`・`put`・`get`・`lldb`・`kgdb`・`screenshot`・`stop`。image は `extra-files` の出力を eval して作る。`GUEST_RUNTIME=<dir>` で別の guest を並べて動かせる（既定 `build/guest`） |
| [guest/serial.py](tools/guest/serial.py) | シリアルの console と対話する（sshd が上がる前。`CONFIG_PCAT_SERIAL_MIRROR=y`） | `serial.py --socket S run 'CMD'`（終了状態を返す）、`login` |
| [qmp.py](tools/qmp.py) | QMP の command を送る | `qmp.py SOCKET quit` など |
| [latency/](tools/latency/) | interactive の応答の測定（起床の遅れ、端末の echo）。WS041 から移した | `run-echo-qemu.sh`、`run-wakebench-qemu.sh`、`pc98-wakebench.py`、`config-*-bench.mk` |
| [clock/](tools/clock/) | guest の時計の進み（`sleep 5` の実時間）。WS040 から移した | `clock-check.py`、`pc98-sleep.py` |
| [ufs/](tools/ufs/) | UFS の directory の試験と volume の検査。`dir-grow.sh` は mount した volume で directory を 12 block まで育て（作成・削除・rename・rmdir・上限）、`verify` で確かめる（`LONG`・`SHORT`・`MOVE`・`GONE` で数）。`check-volume.py` は guest が書いた volume を host で fsck 相当に検査する。`crash-test.sh` は journal の volume の成長の途中で QEMU を止めて replay を確かめる。WS054 から移した | `sh dir-grow.sh DIR make\|verify`（guest）、`check-volume.py IMAGE`、`crash-test.sh IMAGE SECONDS...`（host）。作業の volume は `zedimage-host ufs SIZE EMPTYDIR IMAGE --inodes=16384 [--profile=journal-snapshot]` で作り、NVMe（`-device nvme`）でつなぐ |
| UFS の journal の試験（[tools/ufs](tools/ufs/)、WS063 から移した） | `crash-test.sh`（既定は v3・NVMe の作業 volume、`PROFILE=journal-snapshot` で v2）、`journal-func.sh`＋`journal-guest.sh`（guest での journal の機能: 隠しの `.ufs-journal`、最初の mount での作成、`nojournal`・`writethru`）、`root-crash.sh`（root の強制終了と replay）、`zedimage-compare.sh`（2 つの zedimage-host の UFS の出力の byte 比較） | 各 script の先頭の使い方。`GUEST_RUNTIME`・`VOLUME` を上書きできる |
| SSH の guest image（[guest/](tools/guest/)、WS063 から） | clang の無い SSH の guest image: `config-amd64-ssh.mk`、`build-ssh-image.sh`（package が build/amd64/dynamic に link するので build/amd64 に作る） | `plan/tools/guest/build-ssh-image.sh` |
| 組み合わせの guest image と process の試験（WS064 から） | `guest/hybrid-image.sh BUILD OUT [BASE]`（full の guest image にこの tree の vmunix・BOOTX64.EFI・libc.so・make・sh を入れる）、`guest/make-cases.sh IMAGE`（guest の make-diff）、`process/vfork-test.c`＋`guest-vfork.sh IMAGE`（fork の COW、vfork、posix_spawn、並行の fork） | 各 script の先頭 |
| NVMe と lease の試験（WS072 から） | `nvme/timeout-retry.sh`（QMP の block_set_io_throttle で 2 台目の NVMe を絞り、timeout の後の再発行を確かめる）、`ufs/format-lease-probe.c`（format の lease の下の fsync） | 各 file の先頭 |
| toolchain の試験（WS055 から） | `toolchain/link-undefined-version.sh CLANG`（version script の未定義の symbol の link）、`toolchain/zlib-shared-configure.sh CLANG SYSROOT`（zlib の configure が共有 library を作れること） | host で実行 |
| rpi4 と amd64 の serial の guest（WS036 から） | `guest/rpi4-serial.sh`（raspi4b の guest に serial で login して command を実行、`APPEND` で /chosen/bootargs）、`guest/amd64-serial.sh`（amd64 の UEFI・NVMe・KVM、image に `CONFIG_PCAT_SERIAL_MIRROR=y`）、`rpi4/bootargs-rpi4.sh`（rpi4 の boot の parameter の試験）、`rpi4/noct-rpi4.sh`（rpi4 の Noct の JIT と API） | 各 script の先頭 |
| File Manager の試験（[tools/files](tools/files/)、WS071 から移した） | lean な Venus の guest image（`build-files-image.sh`・`config-amd64-files.mk`）と guest（`files-guest.sh`、runtime `build/ws071-run`、`build/ws035-sq-venus` の renderer が要る）。`files-regress.sh [OUTDIR] [PHASE...]`（zdesktop-files の guest 試験 14 本）、`files-p011.sh`（App Home）、`files-p018.sh`（configure_bounds と置き場所）、`files-lag.sh`。host の files-render（`host-build.sh`・`host-run.sh`・`host-p009/p010/p013/p014.sh`）、`host-png.sh`（libz-compat・libpng-compat を Python と比べる）。`make-home.sh`・`qmp-input.py` | 各 script の先頭の使い方 |
| System Menu と Titlebar の試験（[tools/titlebar](tools/titlebar/)、WS070 から移した） | lean な guest image（`build-menu-image.sh`・`config-amd64-menu.mk`、probe 入り。WS035・WS071・WS074 の image の元）と guest（`menu-guest.sh`、runtime `build/ws070-run`）。`menu-p002.sh`（protocol の error）、`menu-p003.sh`（terminal の menu）、`menu-occlude.sh`、`menu-regress.sh OUTDIR TEST...`（WS035 の zdesktop の試験）、`titlebar-p008/p009/p010/p011/p013.sh`（model、glyph、CONTROLS、TABS、tab の key と wheel）、`icons-host.c`、`style-compare.sh REV FILE...`、`menu-hw.sh`（i915 実機、`flock /tmp/i915-hw.lock` の下で） | 各 script の先頭。files の guest で走らせるときは `GUEST_RUNTIME=build/ws071-run` |
| i915 の実機の試験の場面（WS075） | `plan/ws075/tests/test-hw.sh`（`flock /tmp/i915-hw.lock` の下で試験の場面（vke1・vke2・vkx・vkc ほか）を走らせ、共有の /tmp から log を写す）、`capture-hw.sh`（ZDESKTOP_APP ごとの build の directory で zdesktop の capture）、`config-test-hw.mk`（zdesktop の実機の config と serial の mirror）、`shader-survey/run.sh`（host で 122 の module を i915 の compiler の不足と照合）、`vk-calls.py`（client の Vulkan の command と実行器の対応）。注意: i915 の試験の build の kernel は 16 MiB の上限（AMD64_KERNEL_MAX_BYTES、.bss を含む）の近く。2026-09-28 に 28 KiB 超えたので vkx の場面の state（約 240 KiB）を heap へ移した | 各 script の先頭 |
| libwayland の host 試験（WS035 p075） | `plan/ws035/tests/p075/run-host.sh`（host の libwayland-server と試験の protocol で、生成された protocol の event と server の作る object、client が壊した server 側の object（zombie）への event と fd、id の再利用（p089）） | host で実行 |
| xdg-shell の popup と toplevel の試験（WS035 p076） | `plan/ws035/tests/zdesktop-p076.sh`（Venus の guest、`/bin/popup-probe`（`config-amd64-menu.mk`）で menu・submenu・flip・reposition・dismiss、toplevel の move・resize・min/max size、ping の無応答の表示を QMP で操作し画面を撮る） | 先頭の使い方。PNG は `build/ws035-p076/` |
| sub-surface と seat の試験（WS035 p077・p078） | `plan/ws035/tests/zdesktop-p077.sh`（`/bin/subsurface-probe`: 位置・sync・desync・place_above/below・破棄）、`plan/ws035/tests/zdesktop-p078.sh`（`/bin/seat-probe`: XKB keymap・repeat_info・lock の modifier・wl_output v4）、`plan/ws035/tests/p078/run-host.sh`（host の libxkbcommon で zdesktop の keymap を compile し modifier と keysym を照合） | 先頭の使い方。Venus の guest |
| POSIX の console の試験（[tools/posix](tools/posix/)、WS056 から移した） | `console-posix-r2.sh IMAGE ELF [N]`（serial mirror の kernel `config-amd64-serial.mk` の guest の console で `POSIX-R2.ELF` を N 回、`AS_SH=1` で /bin/sh としても）、`guest-sigev.sh`＋`sigev-thread-mask.c`（SIGEV_THREAD と置き換えの mask の EINTR）、`guest-spawn-probe.sh`＋`spawn-probe.c`、`console-probe.sh`、`guest-pax-test.sh`・`make-pax-archives.sh`（pax・gnu・ustar の展開の比較） | 各 script の先頭の使い方 |
| [kbench/](tools/kbench/) | kernel の microbenchmark（system call、pipe の往復、fork、exec、cached の read、anonymous と file の fault）。kernel の build（LTO・最適化）の比較に使う。WS053 から移した | `kbench/build.sh BUILD OUTPUT`（amd64 の guest 用）で作って guest で `kbench [file]`。予熱の 1 回の後に数回走らせ、中央値で比べる |
| [driver-fragments/prepare.py](tools/driver-fragments/prepare.py) | 統合した driver の source から host 試験用の断片を切り出す（出力は `build/driver-fragments`）。WS025 から移した | WS004 の AX211・xHCI と WS001 の UFS の host 試験が呼ぶ |
| [packages/](tools/packages/) | 外部 package の試験: ライセンス監査、未解決 symbol、取得機構とクロスビルドの host 試験。WS032 から移した | `audit-licenses.sh`、`check-unresolved-symbols.py`、`run-external-host-test.sh`、`run-cross-host-test.sh` |
| [menuconfig-target-host-test.py](tools/menuconfig-target-host-test.py) | menuconfig の target の選択の host 試験。WS020 から移した | `make menuconfig-host-test` |
| [boot-parameter-image-tool.c](tools/boot-parameter-image-tool.c) | image の boot parameter の読み書きと、pc98 の text VRAM の解読（`decode-pc98-vram`）。WS003 から移した | WS005・WS013 の試験が compile して使う |
| [sync.py](tools/sync.py)（[README](tools/README.md)） | GitHub との同期（GitHub mode） | `plan/tools/README.md` |
| sh の試験（[tools/sh](tools/sh/)） | `/bin/sh` を dash と比べる（oils の spec と自前の case）。guest では 40 件ずつ。対話（serial console）と行編集（host の pty） | `build-host-sh.sh`、`sh-diff.py --shell build/ws042/host-sh`（`fetch-oils.sh` で oils を取得）。guest は `--export build/ws042/guest-export` の後 `guest-batches.sh`（中で `guest-diff.sh`）。対話は `sh-interactive.py SOCKET`、行編集は `vi-host.py build/ws042/host-sh`。WS065 から: `build-guest-sh.sh`（この tree の sh を guest の image の libc.so で build）、`guest-batches.sh` の `GUEST_SH=FILE`（guest の copy の /bin/sh を置き換える）、`guest-expat.sh SH`（guest で expat の configure・make・runtests を走らせ configure の生成物の checksum を出す） |
| utility の差分試験（[tools/utils](tools/utils/)） | base の utility を GNU（POSIX mode）と比べる（`cases/` の 484 件、guest へは `--export` と `plan/tools/sh/guest-diff.sh`）。実際の configure（expat・coreutils）を GNU の道具と我々の道具で走らせて生成物を比べる。libc の浮動小数の書式を glibc と比べる | `build-host-utils.sh`、`util-diff.py`、`configure-diff.sh`、`float-format.c`。書き直しの前後の ls の比較は `ls-compare.sh OLD NEW` |
| X11 の回帰（[tools/x11](tools/x11/)） | zdesktop-x11server の上の X11 の app（Venus、`plan/ws035/tests/zdesktop-guest.sh start` の guest）: x11-p003（zterm の rootless の窓、入力、docked）、x11-p004（glxtest の GLX、docked の大きさの変化）、x11-p005（zgears 300 frame、回る、fps）。WS069 から移した | `sh plan/tools/x11/x11-p00N.sh [OUTDIR]`（`GUEST_RUNTIME` の既定は build/ws035-sq-run）。画面を目で確かめる |
| libm の試験（[tools/libm](tools/libm/)、WS076） | libc の libm（`src/libc/math/`）を MPFR（gmpy2）の参照値と比べ、関数ごとの最大・平均の ulp 誤差、正確であるべき結果の不一致、C11 Annex F の特殊な値・errno・例外を出す。host（host の clang、libm を link しない）と guest（amd64、image の libc.so、serial で実行） | `plan/tools/libm/host-test.sh [--count N] [NAME...]`、`plan/tools/libm/guest-test.sh [--count N] [NAME...]`（`BUILD` 既定 `build/ws076-amd64`）。参照の生成は `gen-reference.py OUT.bin`。ブラウザの JS（ws074 の試験と `js/libm.js`）を guest で Chromium と比べる `browser-js.sh`（`js-reference.py --reference` で期待値） |
| 規約の検査（[style-check.py](tools/style-check.py)） | `plan/coding-style.md` のうち機械的に確かめられる規則（条件の中の呼び出し、閉じ括弧の後の空行、段落の comment、入れ子の宣言、条件演算子、goto、前方宣言、comment の形、名前、複数行の本体の括弧） | `python3 plan/tools/style-check.py FILE... [--summary] [--rule NAME]` |

QEMU の不具合は log を読まずに、QEMU のデバッグ機能で解析する:

- **gdbstub**: `-S -gdb tcp::<port>` で止めて起動し、host の `gdb` で `target remote :<port>`。`vmunix` は strip されていない。
- **map**: link で作る `$(BUILD)/vmunix.map` で address から関数を引く（`-g` は付けない）。
- **monitor/QMP**: `info registers`・`info mem`・`info tlb`・`x/`・`xp/`・`pmemsave`。
- **trace**: `-d int,cpu_reset,guest_errors -D <file>`（例外と reset だけ）。

pc98 は QEMU の PC-98 fork（`~/qemu-pc98/build/qemu-system-i386`、`-M pc9821,pegc=off,coregraph=on`）で起動し、
`pmemsave 0xa0000 0x2000` で取り出した text VRAM を `boot-parameter-image-tool decode-pc98-vram` で読む。
回帰試験では GPU を使わず、標準 VGA の framebuffer で login prompt だけを確かめる。

guest の memory（2026-09-24 ユーザー決定「ゲストのメモリはamd64とarm64では8GBでテストしましょう」）: amd64 は 8 GiB（`plan/tools/guest/guest.py` の既定と
`boot-test.sh` の `uefi-usb`）。arm64 の QEMU raspi4b は board の model が 2 GiB しか受け付けない（`Invalid RAM size, should be 2 GiB`）ので 2 GiB（2026-09-24 ユーザー決定「raspi4bは2GBでOKです。」）。i386 は変えない。

## プロジェクト固有の情報

エージェントの守る規則は [AGENTS.md](../AGENTS.md) の「プロジェクトの規則」節にある。ここには計画に要る事実と決定を置く。

### 対象 platform（2026-09-24 ユーザー決定）

| platform | 位置付け | tick 周期 |
| --- | --- | --- |
| amd64 | **主対象**。デスクトップ・GPU・アプリケーション。fg010 のデモ | 1000 Hz |
| aarch64（rpi4 ほか） | **主対象** | 1000 Hz |
| i386（pcat・pc98） | デモ用のおまけ。基本のコマンドと Xzed が動けばよく、性能は考えない | 100 Hz |
| sparcv9（sun4u）、m68k（x68k） | サポート外。コードは残す | 100 Hz |

tick 周期は `include/hal/arch/<arch>.h` の `HAL_TIMER_FREQUENCY`。時間の計算は `kern_ms_to_ticks()`・`kern_ticks_to_ms()`・
`KERN_MS_TO_TICKS()` で行い、tick の数を数字で書かない（WS040）。

### 2026-09-24 のユーザーの判断（有効なもの）

| 項目 | 決定 | 記録先 |
| --- | --- | --- |
| autotools の package | package ごとに patch する | WS034 |
| audiod | unix socket の interface。`shm_open` の直後に `shm_unlink` した fd を SCM_RIGHTS で渡す共有メモリ。`/dev/dsp` の OSS の mmap に対応 | ws035-p050・p049・p009 |
| デスクトップの合成 | 設計を出し、ユーザーが微調整して承認する。2 つのモード（全画面は scanout、ウィンドウは Vulkan で合成）、D1 は swapchain、wl_shm は CPU copy で補助 | ws035-p051 |
| epoll・timerfd・signalfd | POSIX の範囲で Wayland を作れるか調べる | ws034-p050 |
| git の package | `NO_RUST=1` でよい | ws034-p009 |
| `FD_SETSIZE` | 1024 | ws034-p048 |
| HDA の実機確認 | ユーザーが後で USB boot のベアメタルで試す | ws035-p008 |
| 動かない試験 | 書き直さず削除する | ws034-p049 |
| `/bin/sh` の互換性 | 優先度を上げて徹底的に直す | WS042・WS043（完了） |

### 2026-09-27 のユーザーの判断（有効なもの）

| 項目 | 決定 | 記録先 |
| --- | --- | --- |
| zdesktop の X11 server | Wayland 用の X server は単体のプログラム `userland/base/zdesktop-x11server`（後で zdesktop に内蔵するかもしれない）。標準の Wayland と Vulkan を使い、zdesktop の非標準の拡張は libzdesktop 経由。Xzed はレトロコンピュータ用の `/dev/graphics` の簡易実装（デモ）なので、ws069 で足した Wayland・rootless・GLX を外して元に戻す | WS069 design.md §0 |
| 改名 | `userland/base/zwl` を `userland/base/zdesktop`（`/bin/zdesktop`）に。以後 zdesktop と呼ぶ。C の識別子（`zwl_*`）と log の接頭辞（`ZWL`）は変えない | ws035-p073 |
| libzdesktop | zdesktop の非標準の Wayland/xdg 拡張（`zed_gpu_buffer_v1` 等）の wrapper と、OS・daemon への道の両方。client は非標準の protocol を直接話さない | WS069 design.md §0、ws035 |
| 非公開の header | `X11/Xzed.h` と `zed-gpu-buffer-v1-client-protocol.h` は公開しない | WS069 design.md §0 |
| GLSL の compiler | 方式 A（自前の C）。前処理・字句・構文・型・SPIR-V 出力の共通の核から、GLSL ES 1.00 と GLSL 1.30 → 3.30/ES 3.00 → 4.x | WS068 design.md §4 |
| desktop GL | ES でない OpenGL 3.0 を実装し、4.6 まで出来る範囲で（API の完全さは求めない）。Vulkan 1.0 の基本以上が要る機能（geometry・tessellation・compute、SSBO 等）は Venus で先に、i915 の実行器の不足は F-023 に記録して後 | WS068 design.md §6 |
| 作業の順 | 改名 → libzdesktop と header の非公開化 → zdesktop-x11server → Xzed の復元 → **OSC のデモ（fg010）の残りの zdesktop の作業**（ユーザー「X server の後にデモの残り」）→ GLSL compiler → desktop GL 3.0・4.x | WS の優先順位 |
| System Menu | ユーザー（同日）:「X11サーバの実装が終わったら、OpenGLよりも、これを先に実装してもらえませんか？あとでGTK4やQt6のネイティブメニューバーとしても利用可能にするつもりです。XDG拡張ではあるものの、libzdesktopでラップします。」→ WS070（仕様案は ws070/spec.md）。順: WS069（X11 server）→ **WS070（System Menu）** → WS068（GLSL・desktop GL） | WS070、WS の優先順位 |
| サブエージェント（例外） | ユーザー（同日）:「GLSLコンパイラとSystem Menu Extensionについて、サブエージェントで実装を進めてもらえますか。週次のクレジットに余裕があるため、今回は例外的に許可したいです。」→ WS068 の GLSL compiler（p003〜）と WS070 を、それぞれ git worktree のサブエージェントが並行して進める。master・queue・history はメインセッションだけが書き、サブエージェントは自分の WS の記録と自分の path だけを commit、メインセッションが merge する。WS069（X11 server、BUG-057）はメインセッション。この例外は WS068 の GLSL と WS070 に限る（同日の拡張は次の行） | AGENTS.md の「サブエージェントは使わない」の例外 |
| サブエージェントの拡張 | ユーザー（同日）:「さらに4つのサブエージェントを起動して、依存関係が満たされているWSに取り組んでください。WS049, WS045, WS048, WS001あたりがいいと思います。個々のサブエージェントはコンテキスト構築のコストが高いので、なるべく長く実行できるといいですね。」→ WS049・WS045・WS048・WS001 をそれぞれ git worktree のサブエージェントで長く進める（記録と merge の分担は上の行と同じ）。HAL の API・責務に触れる差分（WS049 の ACPI の handoff の名前、WS048 の PCIe）は plan に案として置き、承認まで適用しない | AGENTS.md の例外 |
| OSC のデモ | ユーザー（同日、後から）:「私がほしかったWaylandコンポジタが…すでにPoCができており、デモできる状態です。なので、ここから先は具体的なアプリを動かす基盤を整えていき、OSC当日は、すでに完全なデスクトップが動いているデモにできる見込みです。参考まで。」→ 上の「デモの残り」は**アプリを動かす基盤**（GLSL・desktop GL・X11 の app 等）を優先して読む。デモだけのための磨き込みは急がない | fg010、WS の優先順位 |
| zdesktop-x11server の形 | rootless だけ（rootful は持たない）。「zdesktop本体に組み込む可能性が高いので、再利用できるモジュラリティを保っておくと、あとで組み込みが楽です。」 | WS069 design.md §0 |
| libvulkan の版 | desktop GL に要る Vulkan 1.1 以降の機能・拡張は libvulkan に足してよい（Venus で。i915 の実行器は後、F-023） | WS068 design.md §6 |
| GL_VERSION | 実装した版を正直に名乗る（必須の機能が揃った所まで）。上の版の機能は GL_ARB_* の拡張で個別に出す | WS068 design.md §6 |
| File Manager | ユーザー（同日）:「これもWSを追加しておいてください。Finder風だけどzedBSDらしいファイルマネージャとして、仕様書のベースになる形でまとめます。現在の作業は続けてください。」→ WS071（仕様案の原文は ws071/spec.md）。今の作業（WS069）は続ける | WS071、WS の優先順位 |
| サブエージェントの拡大（2026-09-27、後から） | ユーザー:「依存関係を考慮して、最大7つのサブエージェントを併走させて、WSをcompleteさせていってください。なぜなら、あと10時間で次の週次リセットなのに、まだ使用量がいっぱい余っているからです。作業環境はエンタープライズサーバなので、負荷が高くなることは問題ないです。ただ、WS061のようにパフォーマンスを解析するワークロードがあるWSは、単一実行したいので、後回しでよいです。」→ 依存を満たす WS を最大 7 のサブエージェント（worktree）で完了へ進める。性能を測る WS（WS061、WS066、WS046 の BUG-033 の残り等）は後で単独に。サブエージェントは性能の数値を受け入れに使わない（並列の負荷で狂う）。記録と merge の分担は上の行と同じ | AGENTS.md の例外 |
| メインは計画と merge（2026-09-27、後から） | ユーザー:「現在の作業をサブエージェントにまかせましょう。メインエージェントは、サブエージェントたちの成果を、なるべくまとまりのよう単位でこまめにマージする、プランナーの役割にしましょう。」→ WS068（p024〜）もサブエージェントへ。メインは WS の割り当て・依存の管理・master/queue/history の記録・branch の merge（Phase の区切りなどまとまりのよい単位でこまめに）と merge 後の回帰だけを行う | AGENTS.md の例外 |
| bug のサブエージェント（2026-09-27、後から） | ユーザー:「バグリストに載っているものを解決するサブエージェントを1つ追加しましょう。」→ WS073（8 つ目のサブエージェント） | [WS073](ws073/ws.md) |
| ユーザーの判断（2026-09-27 夜） | 「HAL approvalsは3つとも承認します。ws036-p027は、relax the parser on all platformsでOKです。Toolchain cacheは更新してください。WS045は、dirnameはGNU風でOKで、mktemp, install, base64は追加してください。xargsはどのWSでもよく、WS001でもいいです。」「System Menu defaultsは了解です、アイコンはあとで追加を考えましょう。WS049は、Latitude 5330にssh awe@10.0.30.3で入ってパスワードレスsudo で好きなように操作し、acpidumpも行ってよいです。_OSIはまかせます。/dev/acpiはまかせます。詳細な設計判断やだいたいの設計を承認してほしいなら、それを見せて教えてください。」→ HAL の 3 差分は Guardrail の表へ、ws036-p027 は案 A、toolchain の cache は zedbsd の最新の patch level で更新（WS055 の zedbsd8 と一度に）、dirname の複数 operand・mktemp・install・base64・xargs は WS001、System Menu の 5 つの既定は確定（icon は Future Work）、WS049 は 10.0.30.3 で acpidump、`_OSI` と `/dev/acpi` の形はエージェントが決める | Guardrail、WS036、WS044、WS045、WS001、WS049、WS070 |
| ESP の公開の mount（WS073 の既定） | BUG-065 の修正で、kernel が起動の FAT として private に書き込み mount している ESP（`/dev/nvme0n1p1`）の公開の `mount` も EBUSY になった（同じ volume の 2 つ目の FAT の状態を防ぐ）。要るなら kernel の mount を（例 `/boot` に）公開するのが正しい（既定、戻せる） | WS073 |
| サブエージェントの運用（2026-09-27、rate limit の後） | ユーザー:「まず5時間のrate limitの回復を待ってください。…そのあとで、ws071の続きを行うサブエージェント、ws073の続きを行うサブエージェント、ws035の続きを行うサブエージェントを立ててください。メインエージェントであるあなたは、プランニングと、サブエージェントと通信しながらこまめにマージを行うことを担当します。その他の未完了のサブエージェントの作業は、1つのサブエージェントを立てて、コミットできるものはコミットできるようにしてメインエージェントに回してコミットし、コミットできないものは、それぞれのWSの下仕掛かり中のソースコードを格納して、続きを行えるようにPhaseに記録してください。未完了作業のクリーンアップのサブエージェントは今回限りの特別対応です。その他の3つのサブエージェントは、つねに作業用のサブエージェントをN個走らせるという方針を維持して、N=3で開始し、5時間のrate limitに合わせて今後Nを調整していく…N個のサブエージェントの優先作業は、デスクトップ（ファイラー、Waylandコンポジター、X11サーバなど）とグラフィック周り(GLESやi915を含む）が最優先で、WS001はどうしてもリミットを使い切れないときに、指示したときだけ作業しましょう。ACPIとArm64は、デスクトップ周りが片付くか、リミットが余っているときに再度取り組みます。」→ **作業用のサブエージェントは常に N 個（N=3 で開始、5 時間の rate limit に合わせて調整）**。最初の 3 つは WS071・WS073・WS035 の続き。**今回だけ**、止まった作業の片付けのサブエージェントを 1 つ（`salvage/*` のうち commit できるものは検証して main へ回し、できないものは各 WS の `plan/wsNNN/wip/` に仕掛かりの source を置き Phase に再開の手を記録）。メインは計画と、サブエージェントと通信しながらのこまめな merge だけ | WS の優先順位、AGENTS.md の例外 |
| ユーザーの判断（2026-09-27 昼） | 「ツールチェインをGitHubにアップロードしてOKです。」「BUG046は継続してクローズ判断してください。」「ESPは/boot/espにします。/bootはBOOTという名前のFATパーティションですね。UEFIのみのイメージでBOOTパーティションがない場合もあります。」→ toolchain の cache（zedbsd8、`zedbsd-llvm-23.1.0-zedbsd8-x86_64-linux.tar.gz`、sha256 33931880…）を Release rev-0 に追加し `version.mk` を更新（旧 asset は残す、download と `make toolchain-cache` を確認）。BUG-046 は resolved として閉じ、ws056-p001 の clear は BUG-068 の修正後の console の受け入れで main が判断。ESP は `/boot/esp`、BOOT（FAT、label BOOT）は `/boot`、BOOT の無い UEFI だけの image もある → kernel の private な boot の FAT の mount を公開する形で WS073 に Phase を追加 | WS036、WS056、WS073 |
| N=4 と rate limit の調整（2026-09-27 12:47） | ユーザー:「5時間制限のうち1時間で20％使用しましたね。1/5ということでちょうどよかったと思います。ご提案どおり、N=4にして、ws068の処理を行いましょう。もし途中で、明らかにまた5時間制限にぶつかりそうだったら、きりのいいところで作業を終了してコミットすることで、サブエージェントを減らすような調整もお願いしていいですか？」→ **N=4**（WS071・WS073・WS035・WS068）。当たりそうなときは main が一部のサブエージェントに「wrap up」を送り、きりの良い所で commit・記録・報告して止める（優先度の低い順: WS073 → WS071・WS035・WS068）。main は使用量の計を直接見られないので、経過時間とユーザーの知らせで見積もる | WS の優先順位 |
| WS070 への仕様の追加（2026-09-27） | ユーザー:「WS070に仕様追加します。WS071のサブエージェントでスケジューリングするのがいいと思います。」と Titlebar Presentation の仕様案（原文 [ws070/titlebar-spec.md](ws070/titlebar-spec.md)）→ WS070 に p007（設計）を追加し、WS071 の作業用のサブエージェントが WS071 の Phase と組み合わせて計画・実行 | WS070、WS071 |
| ユーザーの判断（2026-09-27 14 時） | 「CONTROLS / TABS モードのウィンドウのアプリメニューの置き場所は、Aの推奨でお願いします。」「amd64 のUEFIおよびハイブリッドのイメージでは、カーネルが特殊な処理で/bootや/boot/espをマウントせず、fstabに任せてください。つまり、デフォルトの配布イメージではマウントしなくていいです。インストーラがfstabに書けば済むことです。」「WS035に、グラフィカルログインマネージャの検討を追加してください。Waylandではなくて、Vulkanを直接叩くのかなあ。」→ ws070 の §13-1 は案 A（右端の「…」に menu の top-level と隠れた control）。amd64 の UEFI・hybrid の image は kernel が `/boot`・`/boot/esp` を公開せず fstab に任せる（配布の image は mount しない。fstab の mount が EBUSY にならないようにする）を WS073 の Phase に。WS035 にグラフィカルなログインマネージャの検討の Phase（Vulkan の直接の描画か Wayland の greeter か） | WS070、WS071、WS073、WS035 |
| boot slot の公開と WS073 の停止（2026-09-27 14 時半） | ユーザー:「/bootですが、boot0:vmunixとかboot0:rootfs.imgみたいに…これを、/boot/boot0/としてマウントするのが自然ではないでしょうか。…ループバック用に指定されたものは自動マウントするのがいいかもしれません。また、partuuid=xxxxx:vmunixみたいな直接指定の場合は、マウントしなくていいと思います。」「amd64 UEFIでも、/boot/boot0みたいなマウントは自動でやりましょう。ESPはfstabです。スワップだけでもマウントします。rootfs.imgは読み込み専用なので、書き込みできなくても、読み込めていいと思います。」「WS073はきりのいいところで終了しましょう。予想よりも5時間制限の消費が多いです。」→ 全 platform で `bootN:` の file（overlay-root・overlay-data・swapN）が参照する boot slot を `/boot/boot0`〜`/boot/boot3` に自動で読み書きの mount、直接の指定だけのものは mount しない、`/boot` はただの directory、ESP は fstab だけ、使用中の file は読めるが変えられない。p009 の `/boot`・`/boot/esp` の公開を置き換える（WS073 の次の Phase として記録、実行は後）。**WS073 は停止、作業用のサブエージェントは N=3**（WS071・WS035・WS068） | WS073、WS の優先順位 |
| N=2（2026-09-27 14 時半） | ユーザー:「WS035, WS0710, WS071は相互に関わり合って調整が必要なので1つのサブエージェントに寄せて、残りを終了しましょう。N=2にします。」→ **作業用のサブエージェントは N=2**: (1) WS071・WS070・WS035 をまとめた 1 つ（WS071 のサブエージェントが引き継ぐ）、(2) WS068。WS035 と WS073 のサブエージェントはきりの良い所で commit・記録して停止 | WS の優先順位 |
| 試験の範囲（2026-09-27 14 時半） | ユーザー:「試験はamd64のみにしましょう。phase内ではビルドが通れば先に進み、phaseの最後にテストしましょう。」→ 試験は amd64 だけ（pcat・pc98・rpi4 は走らせない）。Phase の途中は build が通れば進み、試験（guest の試験・回帰・boot test）は Phase の最後に 1 回。main の merge の後の検証も amd64（デスクトップの image と boot test）だけ | 検証、AGENTS.md の回帰の範囲の例外 |
| 使用量による停止と再開の方針（2026-09-27 15 時半） | 15:31 に 88%（14:25 の 67% から約 19%/h）→ 2 つのサブエージェントに wrap up（WS068 は p013 を wip.patch に、デスクトップは ws035-p081 を wip.patch に）。16:51 の reset の後に N=2 で再開。ユーザー:「そうですね。GL 3.0をラップアップさせるのがいいです。」→ WS068 は再開後に p013（desktop GL 3.0）を仕上げる。同時にファイルマネージャの pane を浮いた付箋＋すりガラスに（ws071-p015 を追加） | WS068、WS071 |
| reset の後の再開（2026-09-27 16 時 50 分） | ユーザー:「ファイラーのタブは、右側のコンテントペインが所有するのがいいと思うなあ。あと、左側のペインと右側のペインで、背景をなくして、付箋メモのようなフローティングにして、すりガラスエフェクトで合成する、っていう指示、すでに出してあるけど、この2つを実装してみてくれる？あと4分でリセットなので、この作業を優先にしたデスクトップ関連実装のサブエージェントと、OpenGL実装関連のサブエージェント、まずはN=2から開始しよう。」→ N=2: (1) デスクトップ（WS071・WS070・WS035）: 最優先は ws071 のタブを content pane の所有に（p013 の直し）と ws071-p015（左右の pane を背景なしの浮いた付箋＋すりガラス、要る ws035-p057 の背後のぼかしと透過・region の仕組みを含む）、その後に元の順序。(2) WS068: p013（desktop GL 3.0）を wip.patch から仕上げる | WS071、WS035、WS068 |
| Web ブラウザの WS（2026-09-27 19 時） | ユーザー:「新しいWSを作ります。Webブラウザを作成します。userland/base/zdesktop-browserです。…」（全文は ws074/ws.md）→ WS074 を planning で登録（Phase の案、依存: libpng-compat は WS071 p010、TABS は ws070-p011）。WebP・動画・音声（libvorbis-compat）・base の libssl/libcrypto・JIT は後 | WS074 |
| N=3 とデスクトップの指示（2026-09-27 19 時） | ユーザー:「3つめのサブエージェントはブラウザにしましょう。カードの間の隙間は意図的です。OKです。また、フローティングタイトルバーと幅を合わせましょう。フローティングタイトルバーにナビゲーションを実装してから、これがスクリーン上部のメニューバーにドッキングできるか、試していない気がします。これもまだならう実装してください。」→ **N=3**（デスクトップ、WS068、WS074 のブラウザ）。デスクトップは p055 の前に: カードの外の端を浮いたタイトルバーの幅に揃える、zdesktop-files の最大化でナビゲーションがシステムバーに docking するかの確認（無ければ実装） | WS071、WS070、WS074、WS の優先順位 |
| サブエージェントの数と停止の規則、GL の保留と i915（2026-09-27 19 時半） | ユーザー:「サブエージェントの数はN=1～4の間で調整してください。…5時間制限の30分ほど前から、制限を使い切って停止しそうであれば、徐々にサブエージェントを停止していって、N=1にしていきます。制限の15分前には、N=1でも使い切ってしまいそうであれば、安全のため、サブエージェントは0にして、メインエージェントで作業を進めます。メインエージェントは制限に達しても停止するだけで継続できるのですが、サブエージェントは終了になってしまうと再開できないからです。リセット後は、再びN=3程度で様子見しましょう。OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」→ **N は 1〜4 で調整**。reset の 30 分前から、使い切りそうなら段階的に止めて N=1、15 分前に N=1 でも使い切りそうなら N=0 にしてメインが作業（サブエージェントは limit で終わると再開できないため）。reset の後は N=3 から。WS068 は GL 3.3 以降を保留し、i915 の高度化（WS031・WS029、F-022・F-023）へ | WS の優先順位、WS068、WS031、WS029 |
| デスクトップの進め方（2026-09-27 20 時半） | ユーザー:「デスクトップのエージェントの進捗がいまいちですね。エッジケースを置いておいて、ひとまずワンパス通すことを目標にして、それが通ったらエッジケースも補強していく方針を伝えてください。また、ビルドが通ったら先に進んで、テストはPhaseの最後だけでいいです。スクリーンショットは毎回撮ってください。ログイン試験はいらないです。直接にデスクトップ描画のテストを行ってください。」→ デスクトップの Phase は主な経路を一通り通すことを先に、edge case は後の補強の Phase へ。build が通れば進み、試験は Phase の最後。画面は毎回撮る。boot test（login）はやめ、Venus の guest で zdesktop と client の描画を直接試す。回帰は変更に近いものだけ、全体は締めの Phase で | WS071、WS070、WS035 |
| ブラウザの進め方（2026-09-27 20 時半） | ユーザー:「ブラウザのサブエージェントにも、正常系でワンパス通すのを優先するように伝えてください。」→ WS074 は正常系を一通り通す（HTML → DOM → style → layout → 描画 → 窓に page、次に JS の接続）ことを先に、準拠率と edge case は後。必要なら Phase の順序を入れ替える | WS074 |
| ブラウザの描画（2026-09-27 20 時半） | ユーザー:「ブラウザはWaylandとVulkanで実装してください。」→ WS074 の D6（wl_shm）を取り消し、Wayland の上の Vulkan（libvulkan の WSI の swapchain）で表示し、描画は GPU（Vulkan の display list）にする。CPU の描画は host の試験用の headless の出力だけに残してよい | WS074 |
| libm の方針（2026-09-28） | ユーザー:「libmは独自に書いてください。libcのツリーに入れてください。」→ 既存の libm を取り込まず自前で、`src/libc/` に。WS076 を立てた（BUG-078） | WS076、BUG-078 |
| ログインマネージャー・グラフィカルな起動・BUG-024・ブラウザ（2026-09-28） | ユーザー:「ログインマネージャーの提案は承認します。1点だけ、コンソールログインでなくグラフィカルログインをデフォルトにします。ブートローダにロゴを表示させます。カーネルパラメータでメッセージをコンソールに出さずにdmesgのような方法で保存だけする指定をします。これにより完全なグラフィカル起動を実現します。これはデフォルトではありますが、カーネル自体の開発のときは無効にして、コンソールにメッセージを表示させ、コンソールログインにします。ブートローダはppmのようなシンプルな画像ファイルを読みます。」「Bug024は、PCIを有効にします。」「ブラウザは、現在の方法で進めてください。文字の表は、生成した表をコミットしていいてす。」→ ログインマネージャーの §8 を承認（(1) zsessiond のときの /dev/gpu0 0600、(2) revoke は別の WS まで 1 user の機械、(4) 自動 login なし、(5) zdesktop の greeter mode）。**(3) は変更: 既定はグラフィカルな login**。加えて完全なグラフィカルな起動: boot loader が PPM 程度の簡単な画像の logo を表示、kernel の parameter で console へ出さず dmesg のような buffer に保存だけ。kernel の開発のときは無効（console に表示・console login）。BUG-024 は PCI を有効に（WS077）。WS074 は既定のまま進め、D4 は生成した表を commit してよい（出典とライセンスの表示付き） | WS035、WS077、WS074 |
| 古いグラフィックの試験の driver（2026-09-28） | ユーザー:「venus-backend-testのような初期のテストドライバは、もう使わなくてOKです。グラフィック関連の古いテストドライバは捨てて、回帰テストは不要です。デスクトップ環境が起動しているからです。どうしても特定の機能をテストしたいときは、そのときにテストを書いてください。i915はまだexecutorを実装する必要があるので、テストドライバは残していいです。」→ 初期のグラフィックの試験の driver（venus-backend-test 等）は削除してよく、回帰の対象から外す。デスクトップの起動が回帰の代わり。特定の機能は必要なときに試験を書く。**i915 の executor の試験の driver（vkx・vke1・vke2・vkc 等）は残す** | 検証、WS030・WS031・WS075 |
| menuconfig の user program の分類（2026-09-28、BUG-080） | ユーザー:「base, desktop, firmware, packagesに分けましょう。」→ Select user programs の分類を Base・Desktop・Firmware・Packages にする。Desktop は desktop・GPU の stack（libvulkan・libwayland・zdesktop*・zsessiond・GPU の試験）と今の X11 の group（役割は変えない）。main の解釈: Base と Packages は全 platform（'*'）、Desktop と Firmware は build の規則のある platform に限ってよい。MAC-T001 をそれに合わせる。WS073 が実施 |
| Kei Operating System（2026-09-28） | ユーザー:「プロジェクトの名前は Kei Operating System とします。カーネルの内部名がzedbsdです。デスクトップの名前はKeilandで、Kei + Waylandなのですが、カーネルからデスクトップまでOSとして垂直統合しているので、デスクトップ環境とかデスクトップみたいにあえて呼ばず、内部名がKeilandです。OSの見えるところからzedBSD, zed, zの名前を徐々に外していきます。zdesktopは/bin/wayland, zdesktop-x11serverは/bin/xserver, zdesktop-browserは /bin/browser にします。以前から、シンボル名にzedbsdを含めないように実装してきましたが、カーネル、ドライバ、UAPIなどで誤って新規実装で混入してしまっているようです。これは一斉に改めましょう。ZEDBSD_ではなくKERN_が望ましいプレフィックスです。ロゴなどでKだけだとKDEの商標を侵害してしまう可能性があるので、かならず Kei と3文字にします。Keiは日本語の軽いという意味です。」→ [WS078](ws078/ws.md) |
| HAL の quiet console・network の group・5 時間の周期（2026-09-28） | ユーザー:「HALのdiffは承認します。セッションユーザはnetworkグループに追加してOKです。」→ `hal-quiet-console.diff` を適用（Guardrail の表）。graphical な session の user を `network` の group に入れる（ws035-p013 の残り、未実装）。ユーザー:「今後はエージェントを調整して減らしていくとき、N=0になったら、実装を続けてもよいのですが、実装をラップアップして、master.mdやws.mdなどの計画書を整理してクリーンアップするのがいいと思います。この5時間1ターンの計画実行、整理を超速いアジャイルみたいにしましょうよ」→ 5 時間の枠を 1 周期（計画 → agent で実行 → 縮小 → 計画の整理）とする |
| Kei の見た目の基準（2026-09-28） | ユーザー:「起動画面、この画像を使えますか？そのままでなく加工や生成をしてもいいです。」「デスクトップの壁紙とか、ファイラーのWelcomeなどにも、これをデザインベースにして進めていきませんか。」→ [kei-identity-design.md](ws035/kei-identity-design.md)（画像は tree に入れず言葉で記述、部品は図形か git の外の壁紙から生成） |
| 画像の library の置き場（2026-09-28） | ユーザー:「JPEGライブラリは、userland/base/libjpeg-compatにして、共有にしましょう。GIFもそうするのがいいです。include/libc/jpeg/みたいな位置にヘッダがあるのがいいです。」→ libjpeg-compat は base の共有 library（desktop の分類でなく base）、header は `include/libc/jpeg/`。GIF も `userland/base/libgif-compat`（WS074 design D5 の案を採る）、header は `include/libc/gif/`。WS074 が実施 |
| System Menu の統合 | WS070 の p001〜p004（サブエージェント、Venus）を 2026-09-27 に main へ merge。既定で進めた 5 点（F10 で menu、shortcut は zdesktop が実行、外の click は下の窓へ渡さない、icon は描かない、label は ASCII）はユーザーの確認待ち（ws070 design.md §11）。WS071 の右 click の menu は protocol の version 2 の追加（design.md §12） | WS070 |
| Raspberry Pi 4 の USB | WS048 の p001〜p003（サブエージェント）を 2026-09-27 に main へ merge。xHCI は PCIe の DMA が cache を snoop しないため、**hal.h の差分 `hal_pmem_map_uncached`・`hal_pmem_unmap_uncached`（plan/ws048/proposed/hal-pmem-uncached.diff、arm64 だけ実装）の承認待ち**。mailbox は起動後は kernel の driver が持つ（hal.h を変えない）ことの確認も待つ。実機の確認（lspci、dmesg の link up と VL805 の firmware）はユーザー | WS048、Guardrail |
| ACPI の統合 | WS049 の p001〜p005・p010〜p015（サブエージェント）を 2026-09-27 に main へ merge（driver は未 link）。**判断待ち**: (1) HAL の差分 `hal_get_arch_handoff("acpi.rsdp")`（ws049/proposed/hal-acpi-rsdp.diff、hal.h は変えないが HAL の責務の追加）の承認、(2) 対象機を Dell Latitude 5330 とし Linux の `sudo acpidump -b` の table を得る、(3) `_OSI` は既定で Windows 2000〜2022 を名乗る（ACPICA と同じ）でよいか、(4) `/dev/acpi` は text の読み書き（UAPI を足さない、device 番号 0x000B0000）か ioctl か `/dev/system` への統合か | WS049、Guardrail |
| aarch64 の toolchain（WS036） | WS036 の p026 を 2026-09-27 に main へ merge: LLVM の patch が zedbsd6 → zedbsd7（AArch64 zedbsd target）。main の `build/llvm` は `build/llvm-zedbsd7`（symlink）、作業中のサブエージェントは main を merge するまで `build/llvm-zedbsd6`。**判断待ち**: (1) GitHub の Release rev-0 の toolchain cache が zedbsd6 のままで、`make toolchain-cache` と CI の identity 検査が落ちる。zedbsd7 の archive（`make llvm-host-archive`）の upload と `ZEDBSD_LLVM_CACHE_SHA256` の更新（push・公開はユーザーの指示で）。(2) ws036-p027: Pi の firmware の bootargs には `=` の無い token（rootwait 等）があり kernel の parser が拒む。案 A（parser を緩める、全 platform）・B（rpi4 の HAL が区切りの後を渡す）・C（boot partition の file を読む）・D（今のまま、既定） | WS036、Guardrail |
| UFS の v2 の journal（WS063） | `--profile=journal-snapshot` の v2 の tail の journal の volume は v2 のまま（v3 へ移さない。v2 の locator が volume の末尾の snapshot の領域と並ぶため）。2026-09-27 ユーザー「じゃあとりあえず今のままでOKです。」→ **案 A（今のまま）で確定**。スナップショットの機能を設計するときに見直す | WS063 |

### 主な依存関係

- WS046（make）→ guest での expat の build（WS042 の残り）。
- ws035-p051（承認）→ p052〜p055・p057（合成）→ fg010。
- WS014・WS031（GPU の土台）→ WS035 の合成とアプリ。
- WS049（AML）→ WS050（UCSI）→ WS051（DP Alt Mode、i915 の display も要る）。WS049 → WS052（S0i3）。
- WS036 p026（AArch64 の LLVM target）→ aarch64 の userland と package。

### 参照資料

- [設計方針・決定の参照資料](master-design-policy.md): 独立実装・ライセンス境界、module の設計、toolchain、個別の設計判断。
- [コーディング規約](coding-style.md)、[Guardrail](guardrail.md)、[Awesome Plan の設定](config.md)。
