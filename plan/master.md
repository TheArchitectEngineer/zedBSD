<!-- awesome-plan project=zedbsd record=master -->

<!-- awesome-plan-current:start -->
Active Queue: q495（ws068-p024: GLES 3.0 の API（1））。直近: q494（ws068-p023 cleared: cube map）。直近: q493（ws068-p022 cleared: framebuffer object）。直近: q492（ws069-p006 cleared: WS069 completed）。直近: q491（ws069-p011 cleared: X の窓を Vulkan で表示）。直近: q490（ws069-p009 cleared: Xzed をレトロ用に戻した）。直近: q489（ws069-p010 cleared: BUG-057 は kernel の socket の待ち）。直近: q488（ws069-p008 uncleared: zdesktop-x11server）。直近: q487（ws035-p074 cleared: 非公開の header と libzdesktop の役割）。直近: q486（ws035-p073 cleared: zwl を zdesktop（`/bin/zdesktop`）へ改名）。直近: q485（ws069-p007 canceled: X server を zdesktop-x11server として作り直す判断）。直近: q484（ws068-p006 cleared: i915 実機で GLX の zgears・X terminal・仮想デスクトップ。間欠の止まりは BUG-057）。直近: q477（ws069-p004 cleared: GLX の核）。直近: q476（ws068-p010 cleared: EGL の pbuffer。GLX の描画先）。直近: q475（ws068-p008 cleared: GLES 2.0 の描画の核。SPIR-V の shader で buffer・texture・blend・depth・cull、Venus で窓と display 直接）。直近: q474（ws069-p003 cleared: Xzed の rootless。X の zterm が Wiseman の窓）。直近: q473（ws069-p002 cleared: Xzed の rootful の Wayland backend。zwl の窓で X の zterm）。直近: q472（ws069-p001 cleared: X11 の設計）。直近: q471（ws068-p002 cleared: EGL の核、libwayland-egl、clear だけの GLES。Venus で Wayland と display 直接）。直近: q470（ws068-p001 cleared: EGL/GLES の設計。**GLES の方式はユーザーの判断待ち**、plan/ws068/design.md §4）。直近: q469（ws035-p069 cleared: App Home の PoC。実機用 demo image `plan/ws035/demo/build-demo-image.sh`）。直近: q468（ws035-p068 cleared: zdesktop-terminal。Venus と i915 実機）。直近: q467（ws031-p050 cleared: session の close で残った Vulkan の object を解放。実機で確認）。直近: q466（ws035-p067 cleared: zdesktop で mview（Vulkan の client）の窓を i915 実機の GPU で合成）。直近: q465（ws035-p066 cleared: Intel GPU（i915）で Wiseman Mode。GPU の client は F-022）。直近: q464（ws035-p063 cleared: Wiseview。WM の名は Wiseman）。直近: q463（ws035-p062 cleared: タイトルバーのドッキング）。直近: q462（ws035-p061 cleared: 絵の壁紙と透ける窓）。直近: q461（ws035-p060 cleared: mview を glass の窓で Vulkan 描画）。直近: q460（ws035-p059 cleared: `zwl --glass` の浮いたタイトルバーとすりガラス）。直近: q459（ws035-p054 cleared: acquire fence。受け入れ 3 はユーザーの判断で読み替え）。直近: q458（ws035-p053 cleared: `wl_shm` と cursor）。直近: q456（ws062-p003 cleared: amd64 の既定を native・2 GiB に、CI は gzip）。直近の終了: q453（ws065-p003 cleared: sh の builtin の bash 拡張）
Current Focused Goal: fg010 — Wayland デスクトップ（2026-10-17 の OSC Tokyo Fall のデモ）。fg011（expat の configure と compile を Linux と同等に）は達成して終了（2026-09-26 ユーザー「パフォーマンス問題はいったん終了しましょう」。configure 7.1〜7.5 秒・host 10.7 秒、`make -j1` 9.9〜10.4 秒・host 15.5 秒、`make -j4` 4.0〜4.1 秒・host 5.1 秒、`cc t.c -o t` 75〜85 ms・host 83〜85 ms）。`ld.so` の最適化は WS066（後で）
Next: 2026-09-26（夜）ユーザー指示「ホーム画面、EGL/GLES、WaylandコンポジタのX11機能など、デスクトップ関連の作業を優先しつつ、幅広く残っている作業を実施してください。…私が止めるまで自走を続けてほしい」。順: ws035-p068（zdesktop-terminal）→ ws035-p069（App Home の PoC）→ WS068（EGL/GLES）→ zwl の X11 対応（新 WS。2026-09-26 ユーザー追加「WaylandコンポジタのX11サーバ機能については、GLX拡張も実装しておいてください」: GLX 拡張も実装する。GLX は desktop OpenGL の文脈を要するので WS068 の GL の土台に依存）→ 他の残り。判断が要る Phase は uncleared にして理由を記録し先へ進む。
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

- **合成の設計（ws035-p051、[compositing-design.md](ws035/compositing-design.md)）の承認**。p052〜p055・p057 は承認待ち。
  2026-09-24 の 2 回のレビュー（2 つのモード、D1 の swapchain、wl_shm の CPU copy、Vulkan 経路の最適化）は反映済み。

## Workstream registry

| WS | Primary | 内容 | 状態 | 再開点 |
| --- | --- | --- | --- | --- |
| [WS001](ws001/ws.md) | MG002 | POSIX.1-2024 準拠 | incomplete | p033〜p039 cleared（patch、df・du、who、stty、dirname、mktemp・install・base64、xargs）、main へ merge。p040 mesg は実装を merge、uncleared（console の case は BUG-067 待ち）。以後はユーザーの指示のときだけ |
| [WS002](ws002/ws.md) | MG005 | システムサービス | completed | — |
| [WS003](ws003/ws.md) | MG003 | 旧実機 bring-up（終了・再利用禁止） | completed（ユーザー判断で終了） | 未完了は WS027・WS028・F-004 へ |
| [WS004](ws004/ws.md) | MG003 | ハードウェア拡張 | incomplete | NVMe 実機・転送・driver 共通化 |
| [WS005](ws005/ws.md) | MG005 | ネットワーク・WLAN | incomplete | 有線 LAN の常駐管理と起動時の待機（p013〜p017） |
| [WS006](ws006/ws.md) | MG006 | 入力と evdev | completed | — |
| [WS007](ws007/ws.md) | MG006 | グラフィックス・デスクトップ（旧） | incomplete | p004 の再現条件、amd64 の残件 |
| [WS008](ws008/ws.md) | MG006 | Noct と BeUI | completed | — |
| [WS009](ws009/ws.md) | MG001 | 文書 | incomplete | DOC-54（GPU の文書） |
| [WS010](ws010/ws.md) | MG001 | Noct の script と build tool | completed | — |
| [WS011](ws011/ws.md) | MG005 | ネットワーク設定 console | completed | — |
| [WS012](ws012/ws.md) | MG005 | サービス管理 console | completed | — |
| [WS013](ws013/ws.md) | MG007 | CPAR（container 分割） | incomplete（Future Work F-002 に保留） | 昇格まで再開しない |
| [WS014](ws014/ws.md) | MG006 | GPU framework・virtio-gpu・Wayland の土台 | incomplete | p004（最終 API と規約の確認） |
| [WS015](ws015/ws.md) | MG007 | μITRON リアルタイム領域 | planning（Future Work F-003 に保留） | 昇格まで再開しない |
| [WS016](ws016/ws.md) | MG004 | 実行時の swap 制御 | completed | — |
| [WS017](ws017/ws.md) | MG006 | LFB 描画の高速化 | planned | mmap・Xzed の高速描画 |
| [WS018](ws018/ws.md) | MG008 | kernel の source 所有と interface の統合 | completed | — |
| [WS019](ws019/ws.md) | MG003 | インストールとディスク管理 | completed | — |
| [WS020](ws020/ws.md) | MG003 | Intel Mac の UEFI 起動 | completed | — |
| [WS021](ws021/ws.md) | MG001 | x86 LLVM toolchain と sysroot | completed | — |
| [WS022](ws022/ws.md) | MG002 | ELF の TLS | completed | — |
| [WS023](ws023/ws.md) | MG001 | x86 HAL の規約準拠 | completed | — |
| [WS024](ws024/ws.md) | MG004 | 64-bit UFS の一本化 | completed | — |
| [WS025](ws025/ws.md) | MG004 | I/O・cache・物理メモリの再設計 | completed | — |
| [WS026](ws026/ws.md) | MG001 | 試験資産の整理 | planning | Phase 未定義 |
| [WS027](ws027/ws.md) | MG008 | PowerPC 移植 | planned | p001〜p007 |
| [WS028](ws028/ws.md) | MG003 | インストーラの実機動作（4 機種） | planning | NVMe の未動作の切り分け |
| [WS029](ws029/ws.md) | MG006 | i915 native GPU driver | incomplete | cold VFIO attach の間欠的な停止ほか |
| [WS030](ws030/ws.md) | MG006 | 標準 Vulkan 1.0 と直接表示 | completed | — |
| [WS031](ws031/ws.md) | MG006 | i915 native Vulkan 実行器 | incomplete | p015〜p048 planning |
| [WS032](ws032/ws.md) | MG002 | 外部 package のクロスビルド（clang・OpenSSL・OpenSSH） | completed | — |
| [WS033](ws033/ws.md) | MG005 | networking サービスと有線インタフェースの管理 | incomplete | 抜き差しの実機確認 |
| [WS034](ws034/ws.md) | MG002 | アプリケーション拡充と kernel・libc の是正 | incomplete | package の導入 |
| [WS035](ws035/ws.md) | MG006 | デスクトップ環境とアプリケーション | incomplete | **fg010**: 2026-09-27 に p076〜p083・p055・p057・p058 cleared（sq001 の締め）、p028 を閉じた、p082 ログインマネージャの設計（ユーザーの判断 5 点待ち）。残り: p005/p024（GPU scanout）、音声、Chromium の計画、p015/p017 など |
| [WS036](ws036/ws.md) | MG008 | amd64 の成果を他 platform へ（aarch64 を含む） | completed | 2026-09-27 完了（p021 全 platform の回帰と規約、p026〜p029、p027 は案 A: boot の parameter の parser を緩めた）。実機は未実施。toolchain の cache（zedbsd8）は 2026-09-27 に rev-0 へ upload 済み |
| [WS037](ws037/ws.md) | MG006 | NVIDIA GPU（予約） | planning | 番号のみ |
| [WS038](ws038/ws.md) | MG006 | Intel Arc dGPU（予約） | planning | 番号のみ |
| [WS039](ws039/ws.md) | MG006 | AMD RDNA GPU（予約） | planning | 番号のみ |
| [WS040](ws040/ws.md) | MG008 | 時間の単位を tick 周期から導く | completed | — |
| [WS041](ws041/ws.md) | MG006 | 起きた thread の即時実行 | completed | — |
| [WS042](ws042/ws.md) | MG002 | `/bin/sh` の POSIX 互換性 | completed | — |
| [WS043](ws043/ws.md) | MG002 | base の utility を POSIX に（sed・grep・awk ほか） | completed | — |
| [WS045](ws045/ws.md) | MG002 | base の text utility の GNU 拡張（sed・awk・grep ほか） | incomplete | p001〜p009 cleared（サブエージェント、2026-09-27 に main へ merge）。GNU の case 515/515、POSIX 492/492、7 package の configure の比較が同じ。**判断待ち 3 点**（dirname の複数 operand、mktemp・install・base64 の追加、xargs の持ち主）。amd64 以外の image は未実施 |
| [WS046](ws046/ws.md) | MG002 | GNU 互換の make（autotools の出力を実行できる範囲。並列・jobserver は WS064） | incomplete | p002〜p004・p006 cleared。p007 uncleared（BUG-033 の主因を直した）。p009・p012 cleared（BUG-033: configure 204〜252 → 91 秒、link 0.36 秒、file の fault 15 µs/page）。p013 cleared（libc の mount の一覧の API。coreutils の cross build が通った）。次は p014（p011 の当て直し）・p005 |
| [WS047](ws047/ws.md) | MG001 | build.sh と Noct による build system（TUI・kernel・base・packages を別の system に。Makefile は当面残す） | planning | p001 調査と設計 |
| [WS048](ws048/ws.md) | MG008 | Raspberry Pi 4 の USB（PCIe・VL805 の xHCI・USB キーボード） | incomplete | p001〜p003 cleared（FDT、brcmstb の PCIe、firmware の mailbox と VL805 の firmware。host 試験と QEMU の起動、実機は未実施）。**p004 は hal.h の差分（`hal_pmem_map_uncached`）の承認待ち**（ws048/proposed/）。2026-09-27 サブエージェント、main へ merge |
| [WS044](ws044/ws.md) | MG008 | rpi4 を開発に使える形に（console の font、FAT32 の boot、lldb） | incomplete | p001 font・p002 FAT32 の boot partition（QEMU）・p005 cleared。p003（lldb）ほかは WS036 の agent。実機は未実施 |
| [WS049](ws049/ws.md) | MG003 | kernel 内の ACPI AML interpreter | incomplete | p001〜p006・p010〜p015 cleared（p006 kernel への統合: 承認済みの `acpi.rsdp` の差分を適用、amd64 の既定で ACPI の driver が起動、guest の `/dev/acpi` が host の dump と一致。2026-09-27 merge）。次は p007。ACPI はデスクトップが片付くかリミットが余るとき（2026-09-27 方針） |
| [WS050](ws050/ws.md) | MG003 | USB-C の UCSI driver | planning | WS049 が前提 |
| [WS051](ws051/ws.md) | MG006 | USB-C の DisplayPort Alternate Mode | planning | WS050 と i915 の display が前提 |
| [WS052](ws052/ws.md) | MG003 | 電源管理（S0i3、modern standby、`/dev/system` で制御。S3・S4 は対応しない） | planning | WS049 が前提 |
| [WS053](ws053/ws.md) | MG001 | clang/LLVM の LTO を vmunix に安全に適用する（優先度高め） | completed | 4 platform の vmunix は既定で full LTO（HAL を含む）。実機はユーザー |
| [WS054](ws054/ws.md) | MG004 | UFS の directory を複数の block に育てる（BUG-038） | completed | 直接の 12 block まで。実機はユーザー |
| [WS055](ws055/ws.md) | MG001 | zedBSD の clang が link に `--undefined-version` を既定で渡す（F-009） | completed | 2026-09-27 完了: zedBSD の clang の linker に `--undefined-version` を既定で（LLVM の patch を zedbsd8 へ）。main の toolchain を zedbsd8 に切り替えた |
| [WS056](ws056/ws.md) | MG002 | POSIX の試験と utility の不具合を直す（BUG-034・035・037、実行中に見つけた BUG-042〜044） | completed | 2026-09-27 完了（p001・p002 cleared。BUG-046 は console の `POSIX-R2.ELF` 10 回連続 status 0 で閉じた、BUG-068・069 は WS073）。試験は plan/tools/posix/ |
| [WS057](ws057/ws.md) | MG004 | 仮想メモリの reserve と commit の分離と commit の swap の裏打ち（over commit 禁止）の確認と修正（design policy 10） | completed | 分離と拒否は実装済み、BUG-048 を修正。裏打ちは物理 + swap のまま（ユーザーの決定） |
| [WS058](ws058/ws.md) | MG004 | cache の大きさを現代の機械向けに見直す（主記憶 4 GB・swap 16 GB 前提、design policy 10） | completed | p001・p002 cleared。buffer 物理/8、page cache 物理/2、object cache 256、file 2048、inode 2048、overlay 4096、I/O pool 64 MiB。8192 級は F-013（動的確保と hash）の後 |
| [WS059](ws059/ws.md) | MG004 | disk の無い mount にも `st_dev` を与える（BUG-047） | completed | p001 cleared。`mount_device_number()`。`df` が全 mount を出す |
| [WS060](ws060/ws.md) | MG004 | UFS の journal の commit を batch にして名前の操作を速くする（BUG-040）。journal を既定にする前提（WS063） | completed | 2026-09-27 完了（規約は WS063-p002 で）。p001 は p002・p003 に置き換えて canceled |
| [WS061](ws061/ws.md) | MG002 | expat の configure と compile を Linux と同等の水準にする（fg011） | incomplete | 受け入れの計測は達成（q449 の後）: configure 8.2〜8.9 秒（host 10.7）、make（直列）11.3 秒（host `-j1` 15.5）、`cc t.c -o t` 76〜84 ms（host 83〜85）。残り: 規約の Phase ws061-p011（最後） |
| [WS062](ws062/ws.md) | MG004 | amd64 の disk image を ESP の vmunix・UFS の root partition・swap partition に（2026-09-25 ユーザー指示） | completed | 2026-09-27 完了（p004: 規約の全文。zedimage-host の出力が同じ） |
| [WS063](ws063/ws.md) | MG004 | UFS の journal を既定にする（journal の無い image は mount の時に作る、`nojournal`）（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p002: 規約の全文と回帰、crash の試験 v3・v2・root）。v2 の tail の journal は v2 のまま（判断待ち、既定）。制限: transaction ごとの解放 block の追跡は 8192 まで |
| [WS064](ws064/ws.md) | MG002 | base の make の並列（`-j`）と、並列の make の時間を host と同等以上に（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p003: 規約の全文、make・sh・kernel の lock・vmspace の fork・libc の posix_spawn。guest の make-diff 100/100、fork・vfork・posix_spawn の試験、expat の configure が同じ。時間は未測定） |
| [WS065](ws065/ws.md) | MG002 | `/bin/sh` に POSIX が未規定とする bash 拡張を足す（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p004: 規約の全文、host の sh-diff と guest の expat の configure が同じ） |
| [WS067](ws067/ws.md) | MG002 | `/dev/fd` を呼んだ process の descriptor に合わせる（BUG-054、2026-09-26 ユーザー「最優先」） | completed | BUG-054 resolved（QEMU）。p001・p002 cleared |
| [WS066](ws066/ws.md) | MG002 | 動的 link の program の起動を速くする（`ld.so` の最適化）（2026-09-26 ユーザー「あとでやるリスト」） | planning | p001（費用の内訳と設計）。優先度は低い |
| [WS068](ws068/ws.md) | MG006 | EGL と OpenGL ES（と desktop GL 3.0〜4.6）を Vulkan と display 拡張の上に実装する（Wayland とディスプレイ直接の両方）（2026-09-26・27 ユーザー指示） | incomplete | 自前の GLSL compiler（p003 = p015〜p019、GLSL 1.40〜3.30・ES 3.00 と uniform block の p012 = p020・p021）cleared（2026-09-27、サブエージェント、main へ merge）。次は p013（desktop GL 3.0 の context）・p005（GLES 3.0 の API）・p014（GL 3.3〜4.6）。p002・p008・p010・p006 cleared |
| [WS069](ws069/ws.md) | MG006 | zdesktop で X11 の app を動かす（単体の `zdesktop-x11server`、rootless、GLX）（2026-09-26・27 ユーザー指示） | completed | 2026-09-27 完了（q492）。zdesktop-x11server（rootless、窓は Vulkan、GLX）、BUG-057 の修正、Xzed はレトロ用に戻した。残りは F-021・F-024・F-030 |
| [WS070](ws070/ws.md) | MG006 | zdesktop の System Menu: client がメニューの意味を渡し、zdesktop が浮いたタイトルバーとシステムバーに描く（`xdg_toplevel_menu_v1`、libzdesktop で包む）（2026-09-27 ユーザー指示） | incomplete | 全 Phase cleared（2026-09-27、p011 TABS・p006・締め p012）。completed の形への書き直しはユーザーの判断待ち（TABS の硬化: docked の非選択のタブの見え方、題の切り方、animation、keyboard） |
| [WS071](ws071/ws.md) | MG006 | zedBSD File Manager: Finder 風で zedBSD らしいファイルマネージャ（ホームのダッシュボード、サイドバー、タグ、Quick Look、System Menu）（2026-09-27 ユーザー指示、仕様案は ws071/spec.md） | incomplete | 全 Phase cleared（2026-09-27、締め ws071-p011）。completed の形への書き直しはユーザーの判断待ち（硬化の Phase を先にするか: App Home から開いた窓が画面の下にはみ出す、ほか） |
| [WS072](ws072/ws.md) | MG004 | write cached の UFS の format の lease（BUG-060）と、NVMe の timeout の後の回復で root の mount が ETIMEDOUT になる（BUG-059）（2026-09-27、サブエージェント） | completed | 2026-09-27 完了（p001 BUG-060: write cached の format の lease、p002 BUG-059: NVMe の timeout の後の再発行） |
| [WS073](ws073/ws.md) | MG002 | Bug Board の bug の解消（2026-09-27 ユーザー「バグリストに載っているものを解決するサブエージェントを1つ追加しましょう。」）。WS072・WS056（BUG-046）・WS001（BUG-050）の担当と性能の bug（BUG-027・033）を除く | incomplete | p001〜p018 cleared（2026-09-27、BUG-061・065・062・063・028・067・068・069・071・072・070・029・073・074・076・026 を修正、036 は緩和、031 は修正の確認が一部、050・040 は解決済みを確認）。21:00 に停止（使用量）。再開は BUG-075 の設計の Phase から（ws.md の残りの表） |
| [WS074](ws074/ws.md) | MG006 | zedBSD の Web ブラウザ `userland/base/zdesktop-browser`（HTML5 の layout engine → 最適化にこだわらない JavaScript engine の接続 → CSS の準拠と Chrome との比較で目標値を段階的に上げる。JS と Wasm の実行 engine を共通化。画像は libpng-compat・新しい libjpeg-compat、TLS は当面 OpenSSL）（2026-09-27 ユーザー指示） | incomplete | 2026-09-27 19 時からサブエージェントが p001（全体の設計）を実行 |
| [WS075](ws075/ws.md) | MG006 | i915 の高度化: 今日のデスクトップ（zdesktop の glass・backdrop のぼかし・タブ）とグラフィックス（GLES 2/3、GL 3.0〜3.2）を Latitude 5330 の i915 のネイティブ実行器で動かす（compiler の inlining・F-022・F-023 の不足、性能と安定）（2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」） | incomplete | p001（調査: host の shader の検査の全不足の一覧、client の実行器の opcode、zdesktop の実機の capture）から。WS031 の planned の Phase の一部を移す。サブエージェント（WS068 から続けて） |

完了した WS の Phase の記録は 2026-09-24 に plan から削除した（git の履歴に残る）。

## WS の優先順位

依存による実行順とは別のもの。Queue の権限は変えない。

**2026-09-27 からの運用（ユーザー）**: 作業用のサブエージェントを常に N 個（N=3 から、5 時間の rate limit に合わせて調整）。割り当ての優先は
(1) **デスクトップ**（WS071 ファイラー、WS035・WS070 Wayland コンポジター zdesktop、WS069 の後の X11 server）と**グラフィック**（WS068 GLES・desktop GL、
WS031・WS029 i915）、(2) WS073（bug の解消）、(3) **ACPI（WS049〜WS052）と Arm64（WS044・WS048）**はデスクトップ周りが片付くか limit が余るとき、
(4) **WS001 はユーザーが指示したとき**だけ（limit を使い切れないとき）。以下の番号付きの一覧はそれ以前の順で、上の運用が優先する。

0. **WS067**（BUG-054、2026-09-26 ユーザー「最優先」）→ **WS062**（p003: disk image の既定を native に）。WS061・WS064・WS065・WS063 の規約の Phase は問題が出たときだけ（同日のユーザー指示「問題が生じなければ後回しでOK」）。
1. **WS035**（fg010: Wayland デスクトップ）。zdesktop の合成（p051 の承認 → p052〜p055）、タイトル・フレーム（p025）、タスクバー（p013）、
   文字の libtruetype 化（p027）。GPU の土台の問題は WS014・WS031 で直す。
2. **WS014・WS031**（デスクトップが使う GPU の土台。fg010 で必要になった分）。
3. **WS046**・**WS054**・**WS055**（2026-09-24 ユーザー決定「すべて承認します。」: WS054 の実装（p002〜p004）と、zedBSD の clang の既定に `--undefined-version`（WS055）。WS054 は WS046 の coreutils の build が待つ UFS の不具合 BUG-038 を直す。WS046 は 2026-09-24 ユーザー指示で優先度を上げた sh の互換性（WS042、完了）と POSIX の utility（WS043、完了）を実 package で使うための GNU 互換の make）。GNU 拡張（WS045）はその後。
4. **WS036**（aarch64 は主対象。LLVM の AArch64 zedbsd target と sysroot（p026）、起動 parameter（p027、HAL 承認待ち））。
5. **WS034**（アプリの導入）、WS005・WS033（有線 LAN）。
6. **WS049・WS050・WS051・WS052**（2026-09-24 ユーザー指示で追加: ACPI の AML interpreter、UCSI、DP Alt Mode、S0i3。WS049 が他の 3 つの前提。優先度はユーザーの指示を待つ）。
7. その他（WS001、WS004、WS007、WS009、WS017、WS026〜WS029）。WS037〜WS039 は番号の予約のみ。
8. **WS066**（`ld.so` の最適化。2026-09-26 ユーザー「あとでやるリスト」）。
9. **WS068**（EGL と GLES を Vulkan の上に）・**WS069**（X11）: 2026-09-26 ユーザー「デスクトップ関連を優先」で WS035 と並ぶ。
   2026-09-27 の順: ws035-p073（改名）→ libzdesktop（ws035-p074）→ WS069（zdesktop-x11server、Xzed の復元）→ **WS070（System Menu）** → WS068（GLSL compiler、desktop GL）。
   **WS071**（File Manager、2026-09-27 追加）は WS070 の System Menu を使うので、その後。WS068 との前後は指示を待つ（それまでは WS068 を先に読む）。

## Upcoming Work Outlook

見込みであって、約束や実行許可ではない。

| 候補 | 理由 | 準備 |
| --- | --- | --- |
| ws056-p001 の clear か p002（console の `POSIX-R2.ELF` の EINTR、BUG-046） | 残りは 1 点 | ユーザーの判断 |
| ws046-p012 private の mapping の共有の設計（BUG-033 の残り） | configure が 96 秒でまだ host の数十倍 | p011 の差分と失敗の再現手順がある |
| ws046-p013 libc の mount の一覧の API | coreutils の cross build の残り | ioctl は kernel にある |
| ws035-p052 合成の 2 つのモードの核 | fg010 の中心 | **p051 の設計の承認待ち** |
| ws035-p025 タイトル・フレームの描画 | fg010 | 前提は揃っている |
| ws035-p013 タスクバーと WiFi | fg010 | p025 の後 |
| ws035-p027 文字の libtruetype 化 | fg010 | p025 の後 |
| ws036-p026 AArch64 の LLVM target | aarch64 は主対象 | 前提は揃っている |

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
| System Menu の実機（WS070 から） | `plan/tools/titlebar/menu-hw.sh`（i915 の capture の zdesktop-menu、11 検査） | `flock /tmp/i915-hw.lock` の下で |
| libwayland の host 試験（WS035 p075） | `plan/ws035/tests/p075/run-host.sh`（host の libwayland-server と試験の protocol で、生成された protocol の event と server の作る object） | host で実行 |
| xdg-shell の popup と toplevel の試験（WS035 p076） | `plan/ws035/tests/zdesktop-p076.sh`（Venus の guest、`/bin/popup-probe`（`config-amd64-menu.mk`）で menu・submenu・flip・reposition・dismiss、toplevel の move・resize・min/max size、ping の無応答の表示を QMP で操作し画面を撮る） | 先頭の使い方。PNG は `build/ws035-p076/` |
| sub-surface と seat の試験（WS035 p077・p078） | `plan/ws035/tests/zdesktop-p077.sh`（`/bin/subsurface-probe`: 位置・sync・desync・place_above/below・破棄）、`plan/ws035/tests/zdesktop-p078.sh`（`/bin/seat-probe`: XKB keymap・repeat_info・lock の modifier・wl_output v4）、`plan/ws035/tests/p078/run-host.sh`（host の libxkbcommon で zdesktop の keymap を compile し modifier と keysym を照合） | 先頭の使い方。Venus の guest |
| POSIX の console の試験（[tools/posix](tools/posix/)、WS056 から移した） | `console-posix-r2.sh IMAGE ELF [N]`（serial mirror の kernel `config-amd64-serial.mk` の guest の console で `POSIX-R2.ELF` を N 回、`AS_SH=1` で /bin/sh としても）、`guest-sigev.sh`＋`sigev-thread-mask.c`（SIGEV_THREAD と置き換えの mask の EINTR）、`guest-spawn-probe.sh`＋`spawn-probe.c`、`console-probe.sh`、`guest-pax-test.sh`・`make-pax-archives.sh`（pax・gnu・ustar の展開の比較） | 各 script の先頭の使い方 |
| [kbench/](tools/kbench/) | kernel の microbenchmark（system call、pipe の往復、fork、exec、cached の read、anonymous と file の fault）。kernel の build（LTO・最適化）の比較に使う。WS053 から移した | `kbench/build.sh BUILD OUTPUT`（amd64 の guest 用）で作って guest で `kbench [file]`。予熱の 1 回の後に数回走らせ、中央値で比べる |
| [driver-fragments/prepare.py](tools/driver-fragments/prepare.py) | 統合した driver の source から host 試験用の断片を切り出す（出力は `build/driver-fragments`）。WS025 から移した | WS004 の AX211・xHCI と WS001 の UFS の host 試験が呼ぶ |
| [packages/](tools/packages/) | 外部 package の試験: ライセンス監査、未解決 symbol、取得機構とクロスビルドの host 試験。WS032 から移した | `audit-licenses.sh`、`check-unresolved-symbols.py`、`run-external-host-test.sh`、`run-cross-host-test.sh` |
| [menuconfig-target-host-test.py](tools/menuconfig-target-host-test.py) | menuconfig の target の選択の host 試験。WS020 から移した | `make menuconfig-host-test` |
| [boot-parameter-image-tool.c](tools/boot-parameter-image-tool.c) | image の boot parameter の読み書きと、pc98 の text VRAM の解読（`decode-pc98-vram`）。WS003 から移した | WS005・WS013 の試験が compile して使う |
| [venus-console.c](tools/venus-console.c) | Venus の console の試験 client。WS030 から移した | WS014 p009 の試験が build する |
| [sync.py](tools/sync.py)（[README](tools/README.md)） | GitHub との同期（GitHub mode） | `plan/tools/README.md` |
| sh の試験（[tools/sh](tools/sh/)） | `/bin/sh` を dash と比べる（oils の spec と自前の case）。guest では 40 件ずつ。対話（serial console）と行編集（host の pty） | `build-host-sh.sh`、`sh-diff.py --shell build/ws042/host-sh`（`fetch-oils.sh` で oils を取得）。guest は `--export build/ws042/guest-export` の後 `guest-batches.sh`（中で `guest-diff.sh`）。対話は `sh-interactive.py SOCKET`、行編集は `vi-host.py build/ws042/host-sh`。WS065 から: `build-guest-sh.sh`（この tree の sh を guest の image の libc.so で build）、`guest-batches.sh` の `GUEST_SH=FILE`（guest の copy の /bin/sh を置き換える）、`guest-expat.sh SH`（guest で expat の configure・make・runtests を走らせ configure の生成物の checksum を出す） |
| utility の差分試験（[tools/utils](tools/utils/)） | base の utility を GNU（POSIX mode）と比べる（`cases/` の 484 件、guest へは `--export` と `plan/tools/sh/guest-diff.sh`）。実際の configure（expat・coreutils）を GNU の道具と我々の道具で走らせて生成物を比べる。libc の浮動小数の書式を glibc と比べる | `build-host-utils.sh`、`util-diff.py`、`configure-diff.sh`、`float-format.c`。書き直しの前後の ls の比較は `ls-compare.sh OLD NEW` |
| X11 の回帰（[tools/x11](tools/x11/)） | zdesktop-x11server の上の X11 の app（Venus、`plan/ws035/tests/zdesktop-guest.sh start` の guest）: x11-p003（zterm の rootless の窓、入力、docked）、x11-p004（glxtest の GLX、docked の大きさの変化）、x11-p005（zgears 300 frame、回る、fps）。WS069 から移した | `sh plan/tools/x11/x11-p00N.sh [OUTDIR]`（`GUEST_RUNTIME` の既定は build/ws035-sq-run）。画面を目で確かめる |
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
