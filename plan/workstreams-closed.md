# 完了・取り下げた WS

completed・canceled・closed の WS の一覧。未完の WS は [master.md](master.md)。状態の本体は各 ws.md、ここは写し。
WS が完了・取り下げになったら、行を master.md からここへ移す（番号の順）。完了した WS の Phase の記録は plan から削除してあり、git の履歴に残る。

| WS | Primary | 内容 | 状態 | 再開点・結果 |
| --- | --- | --- | --- | --- |
| [WS002](ws002/ws.md) | MG005 | システムサービス | completed | — |
| [WS003](ws003/ws.md) | MG003 | 旧実機 bring-up（終了・再利用禁止） | completed（ユーザー判断で終了） | 未完了は WS027・WS028・F-004 へ |
| [WS005](ws005/ws.md) | MG005 | ネットワーク・WLAN | completed | 2026-10-08 ユーザーの判断（記録のミス、完了済み） |
| [WS006](ws006/ws.md) | MG006 | 入力と evdev | completed | — |
| [WS007](ws007/ws.md) | MG006 | グラフィックス・デスクトップ（旧） | canceled（2026-10-05） | p004 の再現条件、amd64 の残件 |
| [WS008](ws008/ws.md) | MG006 | Noct と BeUI | completed | — |
| [WS010](ws010/ws.md) | MG001 | Noct の script と build tool | completed | — |
| [WS011](ws011/ws.md) | MG005 | ネットワーク設定 console | completed | — |
| [WS012](ws012/ws.md) | MG005 | サービス管理 console | completed | — |
| [WS014](ws014/ws.md) | MG006 | GPU framework・virtio-gpu・Wayland の土台 | closed（2026-10-13 ユーザー） | p004（最終 API と規約の確認） |
| [WS015](ws015/ws.md) | MG007 | μITRON リアルタイム領域 | canceled（2026-10-05 ユーザー） | 昇格まで再開しない |
| [WS016](ws016/ws.md) | MG004 | 実行時の swap 制御 | completed | — |
| [WS017](ws017/ws.md) | MG006 | LFB 描画の高速化 | canceled（2026-10-05 ユーザー） | mmap・Xzed の高速描画 |
| [WS018](ws018/ws.md) | MG008 | kernel の source 所有と interface の統合 | completed | — |
| [WS019](ws019/ws.md) | MG003 | インストールとディスク管理 | completed | — |
| [WS020](ws020/ws.md) | MG003 | Intel Mac の UEFI 起動 | completed | — |
| [WS021](ws021/ws.md) | MG001 | x86 LLVM toolchain と sysroot | completed | — |
| [WS022](ws022/ws.md) | MG002 | ELF の TLS | completed | — |
| [WS023](ws023/ws.md) | MG001 | x86 HAL の規約準拠 | completed | — |
| [WS024](ws024/ws.md) | MG004 | 64-bit UFS の一本化 | completed | — |
| [WS025](ws025/ws.md) | MG004 | I/O・cache・物理メモリの再設計 | completed | — |
| [WS027](ws027/ws.md) | MG008 | PowerPC 移植 | canceled（2026-10-05 ユーザー） | p001〜p007 |
| [WS028](ws028/ws.md) | MG003 | インストーラの実機動作（4 機種） | canceled（2026-10-05 ユーザー） | NVMe の未動作の切り分け |
| [WS029](ws029/ws.md) | MG006 | i915 native GPU driver | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | cold VFIO attach の間欠的な停止ほか |
| [WS030](ws030/ws.md) | MG006 | 標準 Vulkan 1.0 と直接表示 | completed | — |
| [WS031](ws031/ws.md) | MG006 | i915 native Vulkan 実行器 | completed（2026-10-27 00:09 ユーザーの判断、未完の Phase は取り下げ） | — |
| [WS032](ws032/ws.md) | MG002 | 外部 package のクロスビルド（clang・OpenSSL・OpenSSH） | completed | — |
| [WS034](ws034/ws.md) | MG002 | アプリケーション拡充と kernel・libc の是正 | canceled（2026-10-05） | package の導入 |
| [WS035](ws035/ws.md) | MG006 | デスクトップ環境とアプリケーション | completed | 2026-09-30 ユーザーの判断で閉じた（目標が 2026-09-23 のまま古く、ゴールが不明確）。p001〜p138: compositor・sessiond・greeter・lock・Keiland の app・audiod・起動の短縮ほか。Chromium は取り消し（ユーザー「独自にBrowserを書いているから」）。デモまでの仕上げは WS099。`plan/ws035/tests/` は共有の道具として残す（plan/tools への移動は後の整理） |
| [WS036](ws036/ws.md) | MG008 | amd64 の成果を他 platform へ（aarch64 を含む） | completed | 2026-09-27 完了（p021 全 platform の回帰と規約、p026〜p029、p027 は案 A: boot の parameter の parser を緩めた）。実機は未実施。toolchain の cache（zedbsd8）は 2026-09-27 に rev-0 へ upload 済み |
| [WS038](ws038/ws.md) | MG006 | Intel Arc dGPU（予約） | canceled（2026-10-27 00:18、WS295 の xe に含める） | — |
| [WS040](ws040/ws.md) | MG008 | 時間の単位を tick 周期から導く | completed | — |
| [WS041](ws041/ws.md) | MG006 | 起きた thread の即時実行 | completed | — |
| [WS042](ws042/ws.md) | MG002 | `/bin/sh` の POSIX 互換性 | completed | — |
| [WS043](ws043/ws.md) | MG002 | base の utility を POSIX に（sed・grep・awk ほか） | completed | — |
| [WS044](ws044/ws.md) | MG008 | rpi4 を開発に使える形に（console の font、FAT32 の boot、lldb） | closed（2026-10-13 ユーザー） | p001 font・p002 FAT32 の boot partition（QEMU）・p005 cleared。p003（lldb）ほかは WS036 の agent。実機は未実施 |
| [WS045](ws045/ws.md) | MG002 | base の text utility の GNU 拡張（sed・awk・grep ほか） | completed | 2026-10-05 完了（GNU の差分 518/518・POSIX 1080/1080・amd64 の boot、試験は plan/tools/gnu-utils） |
| [WS047](ws047/ws.md) | MG001 | build.sh と Noct による build system（TUI・kernel・base・packages を別の system に。Makefile は当面残す） | canceled（2026-10-05 ユーザー） | p001 調査と設計 |
| [WS048](ws048/ws.md) | MG008 | Raspberry Pi 4 の USB（PCIe・VL805 の xHCI・USB キーボード） | closed（2026-10-13 ユーザー） | p001〜p003 cleared（FDT、brcmstb の PCIe、firmware の mailbox と VL805 の firmware。host 試験と QEMU の起動、実機は未実施）。p004 cleared（承認済みの hal.h の差分 `hal_pmem_map_uncached` を適用、実機は未実施）。p005 は config の有効化が残り uncleared。2026-09-27 サブエージェント、main へ merge |
| [WS049](ws049/ws.md) | MG003 | kernel 内の ACPI AML interpreter | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001〜p006・p010〜p015 cleared（p006 kernel への統合: 承認済みの `acpi.rsdp` の差分を適用、amd64 の既定で ACPI の driver が起動、guest の `/dev/acpi` が host の dump と一致。2026-09-27 merge）。次は p007。ACPI はデスクトップが片付くかリミットが余るとき（2026-09-27 方針） |
| [WS053](ws053/ws.md) | MG001 | clang/LLVM の LTO を vmunix に安全に適用する（優先度高め） | completed | 4 platform の vmunix は既定で full LTO（HAL を含む）。実機はユーザー |
| [WS054](ws054/ws.md) | MG004 | UFS の directory を複数の block に育てる（BUG-038） | completed | 直接の 12 block まで。実機はユーザー |
| [WS055](ws055/ws.md) | MG001 | zedBSD の clang が link に `--undefined-version` を既定で渡す（F-009） | completed | 2026-09-27 完了: zedBSD の clang の linker に `--undefined-version` を既定で（LLVM の patch を zedbsd8 へ）。main の toolchain を zedbsd8 に切り替えた |
| [WS056](ws056/ws.md) | MG002 | POSIX の試験と utility の不具合を直す（BUG-034・035・037、実行中に見つけた BUG-042〜044） | completed | 2026-09-27 完了（p001・p002 cleared。BUG-046 は console の `POSIX-R2.ELF` 10 回連続 status 0 で閉じた、BUG-068・069 は WS073）。試験は plan/tools/posix/ |
| [WS057](ws057/ws.md) | MG004 | 仮想メモリの reserve と commit の分離と commit の swap の裏打ち（over commit 禁止）の確認と修正（design policy 10） | completed | 分離と拒否は実装済み、BUG-048 を修正。裏打ちは物理 + swap のまま（ユーザーの決定） |
| [WS058](ws058/ws.md) | MG004 | cache の大きさを現代の機械向けに見直す（主記憶 4 GB・swap 16 GB 前提、design policy 10） | completed | p001・p002 cleared。buffer 物理/8、page cache 物理/2、object cache 256、file 2048、inode 2048、overlay 4096、I/O pool 64 MiB。8192 級は F-013（動的確保と hash）の後 |
| [WS059](ws059/ws.md) | MG004 | disk の無い mount にも `st_dev` を与える（BUG-047） | completed | p001 cleared。`mount_device_number()`。`df` が全 mount を出す |
| [WS060](ws060/ws.md) | MG004 | UFS の journal の commit を batch にして名前の操作を速くする（BUG-040）。journal を既定にする前提（WS063） | completed | 2026-09-27 完了（規約は WS063-p002 で）。p001 は p002・p003 に置き換えて canceled |
| [WS061](ws061/ws.md) | MG002 | expat の configure と compile を Linux と同等の水準にする（fg011） | completed（2026-10-05 ユーザー「clearedにする：WS061」） | 受け入れの計測は達成（q449 の後）: configure 8.2〜8.9 秒（host 10.7）、make（直列）11.3 秒（host `-j1` 15.5）、`cc t.c -o t` 76〜84 ms（host 83〜85）。残り: 規約の Phase ws061-p011（最後） |
| [WS062](ws062/ws.md) | MG004 | amd64 の disk image を ESP の vmunix・UFS の root partition・swap partition に（2026-09-25 ユーザー指示） | completed | 2026-09-27 完了（p004: 規約の全文。zedimage-host の出力が同じ） |
| [WS063](ws063/ws.md) | MG004 | UFS の journal を既定にする（journal の無い image は mount の時に作る、`nojournal`）（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p002: 規約の全文と回帰、crash の試験 v3・v2・root）。v2 の tail の journal は v2 のまま（2026-09-27 ユーザーが案 A で確定）。制限: transaction ごとの解放 block の追跡は 8192 まで |
| [WS064](ws064/ws.md) | MG002 | base の make の並列（`-j`）と、並列の make の時間を host と同等以上に（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p003: 規約の全文、make・sh・kernel の lock・vmspace の fork・libc の posix_spawn。guest の make-diff 100/100、fork・vfork・posix_spawn の試験、expat の configure が同じ。時間は未測定） |
| [WS065](ws065/ws.md) | MG002 | `/bin/sh` に POSIX が未規定とする bash 拡張を足す（2026-09-26 ユーザー指示） | completed | 2026-09-27 完了（p004: 規約の全文、host の sh-diff と guest の expat の configure が同じ） |
| [WS066](ws066/ws.md) | MG002 | 動的 link の program の起動を速くする（`ld.so` の最適化）（2026-09-26 ユーザー「あとでやるリスト」） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | p001（費用の内訳と設計）。優先度は低い |
| [WS067](ws067/ws.md) | MG002 | `/dev/fd` を呼んだ process の descriptor に合わせる（BUG-054、2026-09-26 ユーザー「最優先」） | completed | BUG-054 resolved（QEMU）。p001・p002 cleared |
| [WS069](ws069/ws.md) | MG006 | zdesktop で X11 の app を動かす（単体の `zdesktop-x11server`、rootless、GLX）（2026-09-26・27 ユーザー指示） | completed | 2026-09-27 完了（q492）。zdesktop-x11server（rootless、窓は Vulkan、GLX）、BUG-057 の修正、Xzed はレトロ用に戻した。残りは F-021・F-024・F-030 |
| [WS070](ws070/ws.md) | MG006 | zdesktop の System Menu: client がメニューの意味を渡し、zdesktop が浮いたタイトルバーとシステムバーに描く（`xdg_toplevel_menu_v1`、libzdesktop で包む）（2026-09-27 ユーザー指示） | completed | completed（2026-09-27）: System Menu と Titlebar（MENU・CONTROLS・TABS）。残りは Future Work（F-042・F-043・F-045）、i915 実機は WS075 |
| [WS071](ws071/ws.md) | MG006 | zedBSD File Manager: Finder 風で zedBSD らしいファイルマネージャ（ホームのダッシュボード、サイドバー、タグ、Quick Look、System Menu）（2026-09-27 ユーザー指示、仕様案は ws071/spec.md） | completed | completed（2026-09-27）: zdesktop-files の最初の版（すりガラスの付箋の pane、タブ、titlebar の CONTROLS、context menu、PNG の thumbnail、DnD、configure_bounds）。残りは Future Work（F-032〜F-041・F-044）、i915 実機は WS075、窓の外への DnD は WS035 |
| [WS072](ws072/ws.md) | MG004 | write cached の UFS の format の lease（BUG-060）と、NVMe の timeout の後の回復で root の mount が ETIMEDOUT になる（BUG-059）（2026-09-27、サブエージェント） | completed | 2026-09-27 完了（p001 BUG-060: write cached の format の lease、p002 BUG-059: NVMe の timeout の後の再発行） |
| [WS073](ws073/ws.md) | MG002 | Bug Board のbug解消（2026-09-27の対象境界を保持）。2026-10-02のP8はWS073に限らず、mainが各bugの既存handling WS/Phaseを照合して配属 | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.3）で閉じた） | p045（BUG-135、UFS の namespace_lock と journal の commit の待ち）を修正（停止 10→2 回）。残りは WS131 p005・p006 の後 |
| [WS076](ws076/ws.md) | MG002 | libc の libm を自前で正しく書き直す（src/libc、誤差 1 ulp 以内、fmod 等は正確）（2026-09-28 ユーザー「libmは独自に書いてください。libcのツリーに入れてください。」） | completed | 2026-09-28 完了（`src/libc/math/`、群 B は全件で正しく丸め、BUG-078 解決）。F-046・F-047 へ移管 |
| [WS077](ws077/ws.md) | MG001 | PC-98 の PCI を有効にする（BUG-024、2026-09-28 ユーザー「Bug024は、PCIを有効にします。」） | canceled（2026-10-05 ユーザー） | **優先度を下げた（2026-09-28 ユーザー「Bug024は優先度を下げます。」）**。p001（調査と設計）。HAL の差分は承認が要る。PC-98 の試験が要るので着手の前に確認 |
| [WS078](ws078/ws.md) | MG006 | Kei Operating System への名前の移行（2026-09-28 ユーザーの決定: OS の名前 Kei、カーネルの内部名 zedbsd、Keiland、`/bin/wayland`・`/bin/xserver`・`/bin/browser`、`KERN_` の接頭辞、ロゴは Kei の 3 文字） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | 2026-09-28: p002・p003・p006・BUG-080・retro への移動は済み、p004 はほぼ済み。残り: 注釈と log の名前、p005 |
| [WS085](ws085/ws.md) | MG006 | Windows版WINQ-EMUのVenusでデスクトップを表示する（2026-09-29 ユーザー） | completed（2026-10-06 夜 ユーザー「WS085、WS088、WS114はComplete、はすでにできており、Completeにしてください） | p001 実行中。mapped blob scanoutでデスクトップを表示。SDL→仮想USB HIDタッチを実装しQMP 2指注入でメニューを確認。Files起動停止の共有画像通信を修正し開閉・再起動とTerminal同時起動を確認。物理タッチと表示所有者切替は未試験 |
| [WS086](ws086/ws.md) | MG002 | ls の出力を GNU ls と同じにする（端末なら既定で列、端末の幅）（2026-09-29 ユーザー） | completed | 2026-09-29 完了（端末で GNU と同じ列、GNU ls 9.7 と host 3362 件・guest 180 件で差 0、名前は常に UTF-8）。残りは F-055 |
| [WS087](ws087/ws.md) | MG002 | /bin/sh の対話の行編集: 矢印キーの履歴（BUG-103）と Tab の補完（2026-09-29 ユーザー） | completed | 2026-09-29 完了（QEMU・host）: 履歴の file（~/.sh_history）、矢印の履歴の上限、PS/2 の E0 の key の capability（矢印が届かなかった原因）、prompt の ~、Tab の補完（GNU Readline の名前で libedit に）。実機の確認と BUG-103 の resolved は実機の後 |
| [WS088](ws088/ws.md) | MG006 | Windows で動く Kei-nightly.zip を CI で配布する（元の zip を clang の cache と同じ Release `rev-0` に置いて再利用し、CI が hdd-image.img を入れる）（2026-09-29 ユーザー） | completed（2026-10-06 夜 ユーザー「WS085、WS088、WS114はComplete、はすでにできており、Completeにしてください） | 2026-09-29: p001 は draft（整理した元の zip 56 MB、LICENSES・THIRD-PARTY、DLL は MSYS2 と一致）。fork の commit と zip の中身の確認がユーザー待ち。次: p003 の準備 |
| [WS091](ws091/ws.md) | MG006 | 画像 viewer（2026-09-29 ユーザー） | completed | 2026-09-29 完了（QEMU）: `/bin/imageview`、PNG・JPEG（EXIF の向き）・GIF（動く）、fit・拡大・pan・pinch・慣性、前後の画像、全画面。Files からの起動は WS093、実機の確認は残り |
| [WS092](ws092/ws.md) | MG006 | text editor（simple）（2026-09-29 ユーザー） | completed | 2026-09-29 完了（QEMU）: `/bin/textedit`、共有の file chooser（libkeiland、KEILAND_VERSION 12）、touch・PRIMARY・clipboard・Files からの起動。実機は未実施。touch の drag での選択は無し |
| [WS093](ws093/ws.md) | MG006 | Files から app の起動（画像・text の double click、file の種類と app の対応）（2026-09-29 ユーザー） | completed | 2026-09-29 完了（QEMU）: Files の double click・Enter・double tap で png・jpeg・gif → Image Viewer、text 系 → Text Editor、html → Browser、pdf → PDF Viewer。Always Open With と Use System Default（`~/.config/keiland/open-with`）。実機は未実施 |
| [WS096](ws096/ws.md) | MG002 | Qt6（core・gui・widgets）の互換の書き下ろし（API の interface だけ、zlib）（2026-09-29 ユーザー、デモの後） | canceled（2026-10-05） | デモの後 |
| [WS097](ws097/ws.md) | MG002 | GTK4 の互換の書き下ろし（API の interface だけ、zlib）（2026-09-29 ユーザー、デモの後） | canceled（2026-10-05） | デモの後 |
| [WS100](ws100/ws.md) | MG006 | system bar の音量: 右上の通知領域の音量の icon、音量の slider と mute、変えたときの確かめの音（2026-09-30 ユーザー。動画の再生はデモの後） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | 2026-09-30: L1 がそろった（A1〜A6、Settings の Sound の頁）。L2 は 5330 の実機（A7、ユーザー）。L3 の p008 cleared: 確かめの音の遅れは QEMU の guest の中で中央値 31〜37 ms（≤ 50 ms、合否は実機で）。kernel の fragment を小さくする直しは実機で 50 ms を超えたとき |
| [WS103](ws103/ws.md) | MG006 | compositor を libvulkan だけにする（GPU の UAPI の直の ioctl を無くす）（2026-09-30 ユーザー「規則にして今移す」、規則は Guardrail） | completed | 2026-10-01 完了（p001〜p007、q508〜q514）: V1〜V4 を満たす（QEMU の Venus と 5330 の passthrough、単独の実機の起動は未実施）。Linux・FreeBSD の backend は F-065。試験は plan/tools/gpu-boundary |
| [WS104](ws104/ws.md) | MG006 | Keiland の OS の境界の整理: desktop の公開の header を `userland/desktop/include/` へ、libkeiland と compositor の OS の部分を `zedbsd/` の module へ、install の path を macro に。zedBSD の振る舞いは変えない（2026-10-01 ユーザー「Linux移植を進めます」、WS105 の準備） | completed | q515〜q522 / A1〜A6 verified。全文規約と全必須回帰 PASS、Linux は WS105 へ |
| [WS105](ws105/ws.md) | MG006 | Keiland を Linux で動かす（`/opt/keiland`）: `make keiland-linux`、libvulkan-compat（独自の WSI から system の libvulkan へ chain）、compositor の Linux の module（KMS・evdev・linux-dmabuf・logind）、主な app、gdm、wpa_supplicant・ALSA（2026-10-01 ユーザー、F-065 の Linux の分） | completed | L1〜L9/最終source conformance verified、q538 finished。Linux host/ownDebian13guest・zedBSD回帰、BUG-125/127は未修正trackingのユーザー許可。GitHub publication pending、次の実装なし |
| [WS107](ws107/ws.md) | MG006 | engine の source を libbrowser に所属させ、Wayland無し・標準Vulkan/抽象入力の component と browser shell を整備 | completed | B1〜B5 verified / q544、API v2/public Vulkan client/最終boot。GitHub deferred（2026-10-05 Q1 の担当。p172 で browser2 を取り込み済み） |
| [WS108](ws108/ws.md) | MG007 | CI で Debian13/Ubuntu26.04 の Linux Keiland .deb を別々に作成/検証/artifact保存 | completed | P1〜P5 / q549、2OS native deb＋QEMU runtime、CI/release定義。remote未実施 |
| [WS109](ws109/ws.md) | MG006 | Linux版の共通描画を利用した native FreeBSD15 Keiland、audio/network/WiFi backend | completed | q574 native実機build/install+全文規約、p008 user「完璧に動作しました」でF6受け入れ合格 |
| [WS110](ws110/ws.md) | MG006 | 通常compositor起動を既定にし--testingで試験用有限modeを明示 | completed | 2026-10-05 完了（引数なしで通常の session、試験は --testing、試験の script 233 file を置き換え、試験は plan/tools/compositor） |
| [WS111](ws111/ws.md) | MG006 | Linux/FreeBSD共通console keiland-desktop、GDMはdirect維持 | completed | q575/q576: 共通console launcher/両native install/全source確認、GDMdirect不変。--login検討のみ |
| [WS114](ws114/ws.md) | MG006 | Linux標準GTK4互換性を調査し機能表レビュー後にXDG-shell/portal等を選択改善 | completed（2026-10-06 夜 ユーザー「WS085、WS088、WS114はComplete、はすでにできており、Completeにしてください） | p007（Linux 本物の GTK4 の CSD）・p008（KDE の server decoration、宣言の無い窓は SSD）cleared |
| [WS118](ws118/ws.md) | MG003 | Latitude 5320 で Kei を動かす（LCD の制御の不具合、sshd の遠隔 log 用 image、ユーザーと実機） | closed（2026-10-13 ユーザー） | p001（遠隔 log の image A は QEMU で SSH・collect まで）。実機は RTL8156 の USB の LAN でユーザーと |
| [WS120](ws120/ws.md) | MG006 | 音楽アプリ（fg019） | completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」） | — |
| [WS122](ws122/ws.md) | MG006 | 動画プレーヤアプリ（fg019） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | p001 |
| [WS123](ws123/ws.md) | MG006 | VA-API のライブラリ | canceled（2026-10-02 user、アプリが Vulkan Video を直接使う） | — |
| [WS127](ws127/ws.md) | MG006 | Files のベータ1 のブラッシュアップ（最重点）（fg019） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001（棚卸し）cleared、p002（BUG-140・Move To・New Window・重ね表示の scroll bar・spring-loaded・PDF の thumbnail）は eject を除き済み。eject は WS132。標準 app の開発は WS131 の後 |
| [WS128](ws128/ws.md) | MG006 | 標準アプリ全般のベータ1 のブラッシュアップ（fg019） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001（棚卸し、候補 C1〜C15）・p002（Notes の Open・Save As）・p003（Text Editor の Replace・Open Recent）cleared。標準 app の開発は WS131 の後 |
| [WS133](ws133/ws.md) | MG003 | 安定版 S1 の実機試験（安定版の image を実機で起動し SSH で複数の試験を詰め込む。最初の項目は ws005-p020・p024 から移した WiFi） | completed（S1、2026-10-03。2026-10-04 の UAT は WS の外で [uat.md](uat.md)・[uat/](uat/) に記録） | 安定版 S1 の内容と試験の一覧はユーザーと決める |
| [WS135](ws135/ws.md) | MG006 | 設定の読み書きを libkeiland に一本化（libkeiland が直接か compositor の拡張で解決、監視と通知の API、desktop.conf は compositor の内部で session の開始・終了だけ読み書き。BUG-162、2026-10-03 ユーザー） | completed（2026-10-04） | — |
| [WS136](ws136/ws.md) | MG007 | 試験の image を「WS の tests/ の config.mk ＋個別の file の複写」に揃え、過去の build/ を入力にしない（2026-10-04 ユーザー） | completed（2026-10-04） | — |
| [WS137](ws137/ws.md) | MG006 | FreeBSD の試験の VM（QEMU+KVM）を T1・T2 で使えるようにし、libkeiland-backend の FreeBSD の build と試験を流す（2026-10-04 ユーザー） | completed（2026-10-04） | — |
| [WS138](ws138/ws.md) | MG006 | 背景の画像を PPM から PNG に（F-071。起動の decode は prefetch の thread へ） | completed | 2026-10-05 完了（PNG・JPEG の背景、PPM の読みを削除、試験は plan/tools/wallpaper） |
| [WS139](ws139/ws.md) | MG006 | desktop の性能の台帳と改善（F-072。開発の host の Venus は lavapipe の CPU、E2 の 5330 の測定は未実施） | closed（2026-10-13 ユーザー） | — |
| [WS140](ws140/ws.md) | MG002 | ld.so の依存・object・handle の上限を動的に（F-070。main は 32・16 のまま） | completed | 2026-10-05 完了（ld.so の上限を動的に、amd64、試験は plan/tools/rtld） |
| [WS142](ws142/ws.md) | MG006 | デスクトップのアプリの切り替え: Windows キーでアプリの一覧、タッチパッドの端からの 2 本指（Wiseview・仮想デスクトップ）、上部のバーのアプリの一覧とプレビュー、3 本指のタップ・Alt+Tab の切り替え（2026-10-04 ユーザーの要望） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001〜p006 cleared（QEMU）。5330 の UAT 待ち（実機は第 1 段の外） |
| [WS148](ws148/ws.md) | MG006 | Settings の Privacy の頁の検討（要らなければ頁を削除、要るなら設計と実装）（2026-10-04 ユーザー） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS149](ws149/ws.md) | MG006 | Settings の Security の頁の検討（要らなければ頁を削除、要るなら設計と実装。WS148 の Privacy と項目を分け合う）（2026-10-04 ユーザー） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS151](ws151/ws.md) | MG006 | Settings の Accessibility の頁の検討（要らなければ頁を削除、要るなら設計と実装。見る・聞く・操作・読み上げ）（2026-10-04 ユーザー） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS160](ws160/ws.md) | MG002 | su・sudo・passwd（2026-10-05 ユーザー「su, sudoを実装してください。」・passwd も実装） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | なし |
| [WS163](ws163/ws.md) | MG006 | 数字 6 桁の login（PIN、greeter・lock の画面）（2026-10-05 ユーザーの追加） | completed（WS172 に吸収、WS172 p002 の PASS） | — |
| [WS166](ws166/ws.md) | MG006 | IME の予測変換（2026-10-05 ユーザーの追加） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS167](ws167/ws.md) | MG006 | GPU の command の protocol を Venus の番号の流用から独自の名前と番号に（Venus と一致する内容から始め、Venus の番号を再利用したことを header に書き、Google の著作権の表示を外せるようにする）（2026-10-05 ユーザーの追加） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS168](ws168/ws.md) | MG006 | プレビュー（縮小表示）を作る隔離された専用の command（chroot・他の file の open・network・fork の禁止、fd 0・1 だけ）（2026-10-05 ユーザーの追加） | completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」） | — |
| [WS178](ws178/ws.md) | MG006 | OpenGL を Desktop へ、GLX を xserver へ | completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」） | — |
| [WS179](ws179/ws.md) | MG006 | UI のアクセントカラーを選べるように | completed（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.2）） | — |
| [WS182](ws182/ws.md) | MG006 | 電源ボタンで Log Out・Shut Down などのメニュー（ベータ2 の最後、2026-10-07 ユーザー） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001 設計 |
| [WS183](ws183/ws.md) | MG003 | I2C HID の Extended Interrupt と TGL の GPIO の group（5320 の touchpad を割り込みで、ベータ2・後回し） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001 |
| [WS187](ws187/ws.md) | MG006 | ロック画面（大きな時計、下部から上へのスワイプ・wheel での解除、自動の lock の猶予、Password・PIN・Hardware Key の選択、2026-10-08 ユーザー） | completed（2026-10-13 ユーザー「完了の記録だけが残る物: WS120・WS168・WS178・WS187 →closeしてください。」） | — |
| [WS188](ws188/ws.md) | MG006 | app の OS の操作を libkeiland → compositor → backend へ（Settings の About・Storage・Users・Languages の残り）と境界の検査の強化（2026-10-08 ユーザー） | completed（2026-10-08 Q1 の判定（sweep-beta2-rc §2）: p001〜p004 cleared） | p001 設計 |
| [WS192](ws192/ws.md) | MG006 | 右上の状態の島を tap で開く glass の操作パネル（WiFi・音量・IME などを大きく、タブレット向け）（2026-10-09 ユーザー、ベータ2） | completed（ベータ2 の範囲、2026-10-10 Q1） | p001 設計と実装 |
| [WS195](ws195/ws.md) | MG006 | zedBSD でも userland/desktop を /opt/keiland/ に、account-admin を base から Keiland へ（2026-10-09 ユーザー、ベータ3） | canceled（2026-10-13 ユーザー） | p001 設計 |
| [WS196](ws196/ws.md) | MG002 | useradd・usermod・userdel（2026-10-09 ユーザーの問い、ベータ3 以降の提案） | completed（2026-10-14） | p001 範囲 |
| [WS199](ws199/ws.md) | MG006 | Settings のセキュリティキーの独立の頁とウィザード（2026-10-10 ユーザーの UAT、ベータ2） | completed | p001 |
| [WS200](ws200/ws.md) | MG006 | Settings の Users のパスワード変更のウィザードと認証方式の選択（2026-10-10 ユーザーの UAT、ベータ2） | completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」） | p001 |
| [WS224](ws224/ws.md) | MG006 | Vulkan の header の著作権表示の形（zedBSD の file header の下に Khronos の表示を別の block で、派生物、解説は入れない）（2026-10-13 ユーザー） | completed（2026-10-13） | Q1 |
| [WS227](ws227/ws.md) | MG002 | menuconfig の Base・Desktop の表示名の整理、画像と PDF の library を Desktop へ、Linux・FreeBSD に zedBSD 独自の base の命令（2026-10-14 ユーザー） | completed（2026-10-14） | — |
| [WS228](ws228/ws.md) | MG002 | 独自実装の簡単な vi を base に足す（2026-10-14 ユーザー、ベータ2 の最後の機能） | completed（2026-10-14） | — |
| [WS230](ws230/ws.md) | MG006 | i915 の GuC 対応（2026-10-14 ユーザー、ベータ3） | canceled（2026-10-27 04:13 Q1: 2026-10-27 ユーザー「i915のGuCは実装せず、xeドライバを開発するときにGuC対応を実装することにします。」、WS295 へ） | 未割当 |
| [WS237](ws237/ws.md) | MG006 | i915 の Gen9・Gen9.5（第 6〜10 世代 Core）の display controller の制御 | completed（2026-10-15 ユーザー「Gen9は残りの安全策の作業をマージしたら終了です） | — |
| [WS244](ws244/ws.md) | MG006 | i915 の Linux 由来の code の整理（Gen9 の display を guardrail に、GT・compiler・header・license）（2026-10-15 ユーザー） | completed（2026-10-27 01:54、ws.md を完了の形に、5330 の自動の回帰で判定） | — |
| [WS245](ws245/ws.md) | MG006 | i915 の Gen12 系（display 12・13、Skylake〜Raptor Lake Refresh の方針で 5330 を特別扱いしない）の display の書き直し（2026-10-15 ユーザー） | completed（2026-10-27 01:54、ws.md を完了の形に、5330 の自動の回帰で判定） | — |
| [WS247](ws247/ws.md) | MG006 | nvrtx の comment・log から開発の管理情報と出典の名前を除き、出典を block ごとに（2026-10-15 ユーザー） | completed（2026-10-15） | — |
| [WS258](ws258/ws.md) | MG001 | 全ドライバの出典の表記を guardrail に（file の先頭から block へ）（2026-10-18 ユーザー） | completed（2026-10-18、残りは WS244・WS259） | — |
| [WS261](ws261/ws.md) | MG008 | include/kern/boot.h から PC-98・Raspberry Pi の固有の構造体を除く（2026-10-18 ユーザー） | completed（2026-10-18） | — |
| [WS262](ws262/ws.md) | MG001 | boot loader（bootloader/）をコーディング規約の全文に合わせる（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS263](ws263/ws.md) | MG008 | riscv32 の virt（QEMU）への移植、framebuffer の console・VFS の段まで（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS264](ws264/ws.md) | MG008 | ppc32 の QEMU の machine（「virt」相当）への移植、framebuffer の console・VFS の段まで（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS265](ws265/ws.md) | MG008 | ppc64 の QEMU の machine（「virt」相当）への移植、framebuffer の console・VFS の段まで（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS266](ws266/ws.md) | MG008 | m68k（68030）の virt（QEMU）への移植、framebuffer の console・VFS の段まで（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS267](ws267/ws.md) | MG008 | sun4u（sparcv9）を再び build できるようにし、QEMU で動かす（2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS268](ws268/ws.md) | MG008 | x68k（X68000）を再び build できるようにする（起動は任意、2026-10-18 ユーザー） | completed（2026-10-19） | — |
| [WS271](ws271/ws.md) | MG008 | MIPS32（mipsel）の QEMU malta への移植、framebuffer の console・VFS の段まで（2026-10-19 ユーザー） | completed（2026-10-19、QEMU malta の MIPS32 で VFS の段まで、T1-725・728・730・734） | — |
| [WS272](ws272/ws.md) | MG008 | MIPS64（mips64el）の QEMU malta への移植、framebuffer の console・VFS の段まで（2026-10-19 ユーザー） | completed（2026-10-19、QEMU malta の MIPS64R2-generic で VFS の段まで、512M の high の alias も、T1-731・733・734） | — |
| [WS274](ws274/ws.md) | MG001 | 仮想メモリの code（vm.c・vmspace.c など）を src/kern/vm/ に分けて整理する（2026-10-19 ユーザー） | completed（2026-10-19、src/kern/vm/ の 26 file、T1-741 まで PASS） | — |
| [WS276](ws276/ws.md) | MG008 | m68k（x68k・m68k-virt）の静的な program の TLS を kernel と libc で支える（2026-10-19、q1301 の所見） | completed（2026-10-19、m68k の静的な TLS、T1-741） | — |
| [WS286](ws286/ws.md) | MG008 | Digilent ZYBO（初代、Zynq Z-7010）への移植: ARMv7-A の HAL と JTAG の起動、PL（FPGA）の block を動かすまで（USB は外す） | completed | 2026-10-23 完了: 実機の ZYBO で JTAG の起動・UART の login・`fpgaload`/`fpgareg` で PL の block（ID・SCRATCH・COUNTER・LED・IRQ 61）。残り（PL310/L2、SMP の CPU1、armv7 の struct stat の詰めた timespec、ptrace の user VA の icache）は ws.md |
| [WS288](ws288/ws.md) | MG008 | FPGA の GPU の IP（display controller・command processor・blitter・vertex fetcher・rasterizer・shader unit・texture unit・ROP・memory controller）、ZYBO で workflow、本命は KC705 | closed（2026-10-26 22:57、ユーザーの指示。amaterrace-gpu は他の担当へ、zedBSD の driver は WS290） | 2026-10-24 ユーザーの決定で目標の板は KC705 だけ、RTL を揃えるのを優先（板は p024、timing は p025）。cleared p002〜p005・p007・p008・p012・p017・p023、in-progress p001・p006（branch）・p009。引き継ぎ [handover-20261024-2230.md](ws288/handover-20261024-2230.md) |
| [WS289](ws289/ws.md) | MG008 | ZYBO の単独起動: SD（SDIO0）・LAN（GEM0 + RTL8211E、SSH）・SD からの起動（BOOT.BIN）。root を SD に。SDHCI・Cadence GEM の共通の部分を汎用の driver に分ける | completed | 2026-10-23 completed。実機の ZYBO が SD の BOOT.BIN → SPL Falcon → FIT kernel で単独起動し、root は SD、LAN 1000 Mbps・DHCP・SSH、`deploy.sh` で kernel 更新と reboot ができる。汎用の SDHCI・Cadence GEM の core を Pi 4/Pi 5/ZYBO の glue に分けた。100/10 Mbps は未確認 |
| [WS293](ws293/ws.md) | MG006 | i915 の Vulkan の executor を render/ から vk/ に揃える（動作は不変） | completed（2026-10-27 01:39） | — |
| [WS294](ws294/ws.md) | MG006 | i915 の GuC による submission | canceled（2026-10-27 00:18、GuC は WS295 の xe で） | — |
