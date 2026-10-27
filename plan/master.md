<!-- awesome-plan project=zedbsd record=master -->

# zedBSD Master

[AGENTS.md](../AGENTS.md)（規則）· [Queue](queue.md) · [担当の台帳](agents/registry.md) · [運用](agents/protocol.md) · [完了した WS](workstreams-closed.md) · [Guardrail](guardrail.md) · [設計方針と全体の決定](master-design-policy.md) · [道具と実機](tools.md) · [Bug Board](known-bugs.md) · [Future Work](future-work.md) · [Past Log](history/index.md) · [設定](config.md) · [plan の地図](README.md)

<!--
  「今の状態」の各 block は「master:<名前>:start」〜「master:<名前>:end」で丸ごと置き換えてよい（今の事だけを書く。終わった事・経緯は書かない）。
  置き換え: sed -i '/^<!-- master:agents:start/,/^<!-- master:agents:end/{//!d}' plan/master.md の後に sed -i '/^<!-- master:agents:start/r new.md' plan/master.md。
  ユーザーの決定の日付の log は置かない（AGENTS.md §8）。
-->

## 今の状態

### 動いている担当と Queue

<!-- master:agents:start -->
2026-10-27 04:45 時点（詳細は [queue.md](queue.md)）。

| 担当 | Queue | 仕事 | worktree / branch |
| --- | --- | --- | --- |
| P1 | q1607 | [BUG-346](bugs/BUG-346.md)（unix の datagram の受信が送り手を起こさず sshd が止まる）を VF2（10.0.30.2）で修正 | `p1-bug346` / `agent/p1-bug346` |
| P2 | q1611 | [ws291](ws291/ws.md) p005〜p007: RDNA の NGG（A passthrough → B culling → C 詰め直し）、実機 10.0.10.3。SSH は 1 本の接続を使い回す | `p2-ws291` / `agent/p2-ws291` |
| P4 | q1610 | plan/ 一式・AGENTS.md・.claude/ の形の整理 | `p4-plan` / `agent/p4-plan` |

- P3 の q1608（未完の WS の精査）は終了、main に merge 済み。結果は [ws-audit.md](agents/P3/ws-audit.md)。ユーザーに 9 件を質問中。
<!-- master:agents:end -->

### 統合の注意

<!-- master:merge:start -->
- 全担当の成果は main に merge 済み（上の Queue の作業中の物を除く）。`agent/p2-ws288-p006` は WS288 の終了で使わない。
- 既定の config: Pi の BT は config-rpi4.mk・config-rpi5.mk で n（ユーザー）、Pi 5 の GPU（CONFIG_DRIVER_BCM2712_GPU）は CI で n、
  fan（CONFIG_DRIVER_RPI5_FAN）は y、CI の VF2 は CONFIG_POWERVR_DEBUG n。
- firmware は全部 zedBSD-firmware（rev fcdfccf、i915 の DMC 6 file を含む、push 済み）から取る。
<!-- master:merge:end -->

### Focus と優先順位

<!-- master:focus:start -->
- 公開は rev.5 まで（実質のベータ2）。次に何をやるか（以前の「ベータ3・ベータ4」の段は取り消し）は Q1 とユーザーが整理中。下の「保留の WS」はその整理の対象。
- **fg023 zedBSD の GHCI 2.0 の driver**（[WS290](ws290/ws.md)）: amaterrace-gpu の `docs/ghci.md` の GHCI 2.0 だけに従う driver に書き直し、host 試験で確かめる。
- **GPU の描画の速さ**（[WS291](ws291/ws.md)、P2）: AMD RDNA → NVIDIA → PowerVR。Linux よりよくなるまで profile を見て直す。
- **5330 の電源と性能**（P1、2026-10-27 ユーザー「P1は、しばらくはさっきお願いした内容で進めていきますので、master.mdに書いておいてください。」）: [WS299](ws299/ws.md) NVMe の速さ → [WS300](ws300/ws.md) NVMe・VMD の低電力 → [WS052](ws052/ws.md) S0ix → [WS301](ws301/ws.md) CPU の DVFS・P/E core → [WS302](ws302/ws.md) DTB → 熱と fan。順と依存は [pm-plan.md](agents/P1/pm-plan.md)。
- 達成: fg021 ZYBO（2026-10-23）、fg020 VisionFive 2（WS256・WS269・WS270・WS277、2026-10-22 ユーザー「すばらしい。実用スピードです。」）、
  fg019 ベータ2（2026-10-17 OSC）、fg018 Linux 標準 GTK4（WS114）、fg010 OSC のデモ、fg012〜fg016。Pi 5（WS278・WS279）も completed。
  取り下げ: fg022 KC705 の GPU の board（WS288 の終了）。
<!-- master:focus:end -->

<!-- master:priority:start -->
優先順位（依存による順とは別。Queue の権限は変えない）:
1. WS290: p001 の設計 → ghci.md の疑問（G01〜G39・U1〜U7）をユーザーへ → 実装 → 規約の全文。WS291（P2）は並べて進める。
2. P1 は 5330 の電源と性能の線（WS299 → WS300 → WS052 → WS301 → WS302 → 熱と fan、[pm-plan.md](agents/P1/pm-plan.md) の順）。BUG-346 は resolved。
3. すぐ取り組む物（2026-10-27 ユーザー）: WS221（Jumbo frame）、WS242（RTX 30/40）・WS243（NVDEC）・WS250（nvrtx の取り消しと復帰）、WS223（全画面は合成、推しで実装）、WS009 の DOC-54（/dev/gpu の UAPI の文書）。
4. WS226（Vulkan 1.3 相当を全 GPU driver で）は WS290 の後。WS292（GHCI の PROT）も WS290 の後。
5. WS295（xe、p001 の調査だけ）・WS296（drivers の comment の整理）: 担当の起動はユーザーの指示の時。WS068 は空き時間だけ。
6. 保留の WS（WS297 を含む）: Q1 とユーザーの整理の後。
<!-- master:priority:end -->

### ユーザーの未決

<!-- master:open-decisions:start -->
- 保留の WS の整理（以前のベータ3・4 の中身を含む）。Q1 とユーザー。
- WS290: design.md の ghci.md の疑問 G01〜G39・U1〜U7（Q1 がユーザーに示す）。
- WS282〜WS285: 実装を始める時期（ユーザー「設計だけ残してラップアップして、後日実装することにします。」）。再開の時に WS282 の試験の画面を出す PC（D11）と買う webcam（D12）。
- WS286 の後の物: openXC7 の host への導入。LLVM の cache の asset を GitHub の release に upload してよいか（toolchain の変更の時に確かめる）。
- BUG-345（Pi 5 の click が画面に出ない窓）: 再発したら再起動の前に Q1 に知らせてもらう。
- 以前からの物: UEFI の loader の GOP の mode の選び方、BUG-222（ue0 の SCP が約 10 Mbps、tracking）、WS153 U2〜U15（ユーザーが検討中、聞かない）。（WS037 の nvrtx は 2026-10-15 ユーザーの決定で既定 on、未決ではない）
<!-- master:open-decisions:end -->

### 止まっている物

<!-- master:blocked:start -->
- WS290 の実機の試験: GHCI 2.0 のハードウェア（am-ws001-p007）ができるまで無い。試験は zedBSD の host 試験だけ（amaterrace-gpu の sim・firmware は使わない）。
- VF2 の zedbsd.cfg は login=graphical を外したまま（元は zedbsd.cfg.p044orig、戻さない）。
- GitHub への公開は push の指示の時だけ。
<!-- master:blocked:end -->

### 次の番号

<!-- master:next:start -->
BUG-347 · q1617 · WS304 · KL_VERSION 96。使う前に衝突を確かめる（`ls plan/bugs`・`ls -d plan/ws*`・`grep q16 plan/queue.md`）。
<!-- master:next:end -->

## 目的

- **目的**: 寛容なライセンスで企業が自由に使える UNIX 互換 OS を、GPL の Linux kernel に依存せずに作る。
- **利用者**: OS を組み込んで独自のディストリビューションを作る開発者・企業と、デスクトップ・ラップトップ・SBC で使う個人。
- **最終成果**: Linux/Android を置き換えられる水準の kernel と userland、最小の HAL による移植契約、用途別に構成・配布できる仕組み。
- **範囲**: kernel、HAL、driver、libc、base の userland、Keiland（desktop）、外部 package のクロスビルド、インストーラ、文書。
- **範囲外**: Linux の kernel ABI・DRM の互換、Mesa 流の user mode driver、正式な UNIX 認証・Vulkan CTS 認証の取得（主張しない）。
- **制約**: HAL の API の変更は差分ごとの事前承認（[Guardrail](guardrail.md)）。独立実装とライセンスの境界（[設計方針](master-design-policy.md)）。

### Objectives

- **O1**: 寛容なライセンスで企業が自由に使いやすい UNIX 互換システムを、GPL の Linux kernel に依存せず、Linux/Android を置き換え可能な水準で提供する。
- **O2**: デスクトップ、ラップトップ、SBC、タブレット、モバイルなど様々な規模で動く kernel と userland を提供し、開発者が独自ディストリビューションを自由にカスタマイズ・リブランディング・配布できるようにする。
- **O3**: UNIX/BSD/Linux の遺産から現代のシステムに必要なエッセンスを抽出し、networkd、netconf、service などをシンプルで一貫した仕組みとして再実装する。
- **O4**: ページベース MMU を備える 32bit/64bit コンピュータへ UNIX 互換 OS を確実に移植できる、明確で最小限の HAL を定義し、人類の共有知とする。
- **O5**: AI 時代の OSS のあり方を、大規模な AI 活用開発を通じて探索し、成果・失敗・人間の判断を再利用可能な知見として共有する。

### Milestone

達成は所属の WS の完了の数ではなく、到達点の証拠で判定する。completed の Milestone はまだ無い。WS の所属は下の表と [完了した WS](workstreams-closed.md) の Primary の列。

| Milestone | Objective | 受け入れの核 |
| --- | --- | --- |
| **MG001** 継続開発できる基盤 | O4, O5 | 文書化した環境で build でき、設計境界・規約・試験・制限を追跡できる |
| **MG002** UNIX アプリケーションの実行基盤 | O1 | process・memory・libc・loader/TLS の対応範囲を互換性台帳と代表アプリで確認できる |
| **MG003** 対象機へ導入して単独起動 | O2, O4 | 合意した機種・媒体でインストール後の単独起動と login を確認できる。実機と QEMU の証拠を分ける |
| **MG004** データの保持とメモリ/ストレージの実用 | O1, O2 | 永続化、低メモリ時の進行、媒体世代、既定構成の性能を確認できる |
| **MG005** 一貫したネットワーク/サービス管理 | O1, O2, O3 | networkd・netconf・service の責務・設定・操作が一貫し、永続化と失敗後の復旧を確認できる |
| **MG006** グラフィカルな操作環境 | O2 | 入力・描画・ウィンドウ・端末・GUI ツールの一連の操作を確認できる |
| **MG007** 用途別の独自ディストリビューション | O1, O2 | 第三者が用途別に構成し、独自ブランドで build・配布できる |
| **MG008** 最小 HAL の移植契約と異種機での実証 | O4 | HAL 契約・移植手順と異種/レトロ機での実証を公開する |
| **MG009** AI 活用 OSS 開発の知見の公開 | O5 | 設計権限・レビュー・変更追跡・失敗からの回復の事例と根拠を公開する |

## 未完の WS

状態の本体は各 ws.md、ここは写し。完了・取り下げは [workstreams-closed.md](workstreams-closed.md) へ移す。2026-10-27 に P3（q1608）が精査した（[ws-audit.md](agents/P3/ws-audit.md)）。

### 進めている・次の WS

| WS | Primary | 内容 | 状態 | 再開点 |
| --- | --- | --- | --- | --- |
| [WS299](ws299/ws.md) | MG004 | 5330 の NVMe の読み書きを速くする（512 MiB の崖、転送・queue・readahead）（2026-10-27 ユーザー） | planning | p001 測定と設計、約 32 LW |
| [WS300](ws300/ws.md) | MG003 | NVMe と VMD の低電力（S0 idle の VMD の suspend・NVMe の状態、APST・ASPM・LTR）（2026-10-27 ユーザー） | planning | p001・p002（VMD の suspend、WS052 の前提）、約 27 LW |
| [WS301](ws301/ws.md) | MG004 | CPU の DVFS と P/E core の相対性能の枠組み（UAPI は /dev/system 推し、fan は別 WS 案）（2026-10-27 ユーザー） | planning | p001 調査・設計・HAL と UAPI の案、約 40 LW |
| [WS302](ws302/ws.md) | MG006 | デスクトップスレッドバランサ DTB（CONFIG_DTB、sched への hint）（2026-10-27 ユーザー） | planning | p001 検討（WS301 p001 の後）、約 32 LW |
| [WS303](ws303/ws.md) | MG004 | 熱と fan（温度・fan の回転数・thermal mode、/dev/system）（2026-10-27 ユーザー） | planning | p001（WS301 p002 の後） |
| [WS052](ws052/ws.md) | MG003 | 電源管理（S0ix を 5330 で初めて動かす、modern standby、/dev/system） | incomplete | 再開点 p014（WS300 p002 の後）、並べて p015・p016、約 31 LW |
| [WS009](ws009/ws.md) | MG001 | 文書 | incomplete（DOC-54 を今書く、2026-10-27 ユーザーの回答「今書く」） | DOC-54（/dev/gpu の UAPI の文書） |
| [WS068](ws068/ws.md) | MG006 | EGL と OpenGL ES（と desktop GL 3.0〜4.6）を Vulkan と display 拡張の上に実装する（Wayland とディスプレイ直接の両方）（2026-09-26・27 ユーザー指示） | incomplete・継続（空き時間だけ、2026-10-27 ユーザーの回答「継続」） | p037・p034〜p036（GL 3.3〜4.6）、p039 ETC2、p007 規約 |
| [WS074](ws074/ws.md) | MG006 | zedBSD の Web ブラウザ `userland/base/zdesktop-browser`（HTML5 の layout engine → 最適化にこだわらない JavaScript engine の接続 → CSS の準拠と Chrome との比較で目標値を段階的に上げる。JS と Wasm の実行 engine を共通化。画像は libpng-compat・新しい libjpeg-compat、TLS は当面 OpenSSL）（2026-09-27 ユーザー指示） | incomplete・継続（Codex のセッション） | WPT Interop 2025 の 100%（ws074-q20261019-55 active） |
| [WS221](ws221/ws.md) | MG005 | Jumbo frame（Ethernet の L2 の大きな MTU、device ごとの上限、IP・TCP の MSS）（2026-10-13 ユーザー） | incomplete（すぐ取り組む物、2026-10-27 ユーザー「Jumbo Frameはすぐやるものの一つにします。」） | p001 の判断から |
| [WS223](ws223/ws.md) | MG006 | 全画面は compositor の合成、game mode は Wayland の拡張で求められた時だけ（Video Player は game mode をやめる）（2026-10-13 ユーザー） | planning（すぐ進める、2026-10-27 ユーザーの回答「推しで決めて実装」、D1〜D6 は推し） | p001 の D1〜D6 を推しで確定 → p002 |
| [WS226](ws226/ws.md) | MG006 | Khronos の Vulkan 1.3 の header をそのまま使い、libvulkan で Vulkan 1.3 の API を実装しきる（不整合の整理）（2026-10-13 ユーザー） | planning（WS290 の後、全 GPU driver で Vulkan 1.3 相当を capability の flag とともに、2026-10-27 ユーザー） | WS290 の後に p001 |
| [WS242](ws242/ws.md) | MG006 | nvrtx の RTX 20/30/40 の全モデル（2026-10-15 ユーザー） | incomplete（すぐ取り組む物、2026-10-27 ユーザー「すぐ取り組むものの一つです。」） | p001 の再開点 1〜4、GTX 16・RTX 30・40 の既定の有効（ユーザーの決定、未実装） |
| [WS243](ws243/ws.md) | MG006 | nvrtx の NVDEC で H.264（Vulkan Video）（2026-10-15 ユーザー） | incomplete（すぐ取り組む物、2026-10-27 ユーザー「すぐ取り組むものの一つです。」） | p001 の design.md から |
| [WS250](ws250/ws.md) | MG006 | nvrtx の終わらない GPU の仕事の取り消しと復帰、system は動き続ける（2026-10-15 ユーザー） | incomplete（すぐ取り組む物、2026-10-27 ユーザー「すぐ取り組むものの一つです。」） | p001 の設計（草稿は main 64f05b2c7） |
| [WS290](ws290/ws.md) | MG006 | zedBSD の GHCI の driver を GHCI 2.0（amaterrace-gpu の docs/ghci.md）だけに書き直す。試験は host だけ | incomplete（p001 の設計は済み、ghci-questions.md の回答待ち） | p001 の設計（q1594、P1） |
| [WS291](ws291/ws.md) | MG006 | GPU の driver の描画の速さ（desktop 60 fps・重い 3D の model 50 fps）: AMD RDNA（P2）→ NVIDIA（P2、GPU を入れ替え）→ PowerVR（VF2、P3） | incomplete（p001〜p004 cleared） | p005〜p007 NGG（q1611、P2） |
| [WS292](ws292/ws.md) | MG006 | GHCI の session の保護（PROT）を zedBSD の driver に（WS290 の後） | planning | p001 設計（WS290 と G29 の回答の後） |
| [WS295](ws295/ws.md) | MG006 | Intel の xe の driver（Meteor Lake 以降の iGPU と discrete の Arc、GuC の submission、i915 の使える所は写して重複） | planning | p001 調査（担当の起動はユーザーの指示の時） |
| [WS296](ws296/ws.md) | MG001 | src/drivers（gpu/ を除く）の comment から開発の管理情報を消し、XXX の規約の当て直し（usb など 11 file） | planned | 18 LW、担当の起動はユーザーの指示の時 |

### 保留の WS（何をやるかは Q1 とユーザーが整理中）

「元:」は以前の段（ベータ3・4 などは取り消し）。

| WS | Primary | 内容 | 状態 | 再開点 |
| --- | --- | --- | --- | --- |
| [WS001](ws001/ws.md) | MG002 | POSIX.1-2024 準拠 | incomplete・保留（元: ベータ3、段は取り消し・整理中） | p041〜p044 の guest 試験、p045 POSIX の header、p046〜p051、p040 mesg（BUG-067） |
| [WS013](ws013/ws.md) | MG007 | CPAR（container 分割） | incomplete・保留（Future Work F-002） | F-002（runtime の隔離・CLI・service container） |
| [WS026](ws026/ws.md) | MG001 | 試験資産の整理 | planning・保留（元: ベータ3、段は取り消し・整理中） | Phase 未定義 |
| [WS080](ws080/ws.md) | MG002 | `ld.coff`: Win64 PE32+ の動的ローダ（PE/COFF の mapping・relocation・DLL・import/export・Microsoft x64 ABI・最小の TEB/PEB・GS base）。NT の loader は再現せず `AddressOfEntryPoint` へ直接。互換の DLL は上に積む（2026-09-28 ユーザーの仕様 [spec.md](ws080/spec.md)） | incomplete・保留 | p002 は agent/p1 の a06d5d75d・6c7c991b1（未 merge）、p003〜p012 |
| [WS082](ws082/ws.md) | MG002 | Linux の `/dev/kvm` の移植の検討（eventfd 等の非 POSIX の fd の代わりに unix socket の message で MMIO・IRQ の通知。ioctl を直接の移植・別の仕組みでの代替・実装不能に分類）（2026-09-28 ユーザー） | planning・保留（要検討） | ユーザーの判断（study.md §10） |
| [WS098](ws098/ws.md) | MG006 | IME の変換のニューラル化: 辞書で候補を作り、小型のモデル（15 MB 未満）で同音異義語の選択（語の番号の並び）とひらがな列の形態素解析（語の境界と品詞、BiLSTM か小型の Attention）を評価する（2026-09-29 夜 ユーザー、IME の最後の仕上げ） | planning・保留（要検討・ブロック） | p001 設計（WS095 の後、corpus と license はユーザー） |
| [WS115](ws115/ws.md) | MG002 | upstream GTK4をzedBSD `packages/desktop/gtk4`へ移植し知見を記録 | incomplete・保留（元: ベータ3、段は取り消し・整理中） | p010（GTK の起動、ld.so の上限は WS140 で解決済み）、p003 |
| [WS116](ws116/ws.md) | MG002 | upstream Qt6の範囲をGTK4移植後に検討し `packages/desktop/qt6`へ移植 | planning・保留（元: ベータ3、段は取り消し・整理中） | p001〜p005（WS117 の後） |
| [WS117](ws117/ws.md) | MG006 | Linux の本物の Qt6 を調査し、素の Qt6 アプリが動くよう compositor を改良（WS115/116 の前） | planned・保留（元: ベータ3 以降、段は取り消し・整理中） | p001〜p006 |
| [WS119](ws119/ws.md) | MG003 | インストーラの作り直し | planning・保留（元: ベータ4 以降、段は取り消し・整理中） | p001 要件の案から |
| [WS126](ws126/ws.md) | MG002 | Python 3 の package（fg019） | incomplete・保留（元: ベータ3、段は取り消し・整理中） | p005 の guest の受け入れ（test-wait）、p006 規約 |
| [WS144](ws144/ws.md) | MG005 | VPN（bridge・tunnel などの汎用の network の基盤と、選んだ VPN の protocol、Settings の stub の VPN の頁の実体）。ベータ2 の実装の項目、時期は未定（2026-10-04 ユーザー） | planning・保留（元: ベータ4 以降、段は取り消し・整理中） | p001 から |
| [WS146](ws146/ws.md) | MG006 | SSH を使う独自のオンラインストレージ（server に agent を転送、file の木と metadata の database、差分の同期、手元に本体の無い on-the-fly のアクセス、OneDrive のような機能）。「あとで」の要望（2026-10-04 ユーザー） | planning・保留（元: ベータ4 以降、段は取り消し・整理中） | p001 から（WS150 の後） |
| [WS147](ws147/ws.md) | MG006 | Microsoft OneDrive の client（Settings の Sharing の頁から設定、OAuth・Graph の API・同期、on-the-fly は WS146 と共有）（2026-10-04 ユーザー「独立WSにします」） | planning・保留（元: ベータ4 以降、段は取り消し・整理中） | p001 から（WS150 の後） |
| [WS150](ws150/ws.md) | MG004 | userland の file system の kernel の枠組み（FUSE に当たる物）。クラウドストレージ（WS146・WS147）の前提（2026-10-04 ユーザー） | planning・保留（要検討・ブロック） | p001 から |
| [WS152](ws152/ws.md) | MG007 | system の更新（Settings の Updates の頁の実体）。ベータ3 の実装の項目、p001 は方式の検討（2026-10-04 ユーザー） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 の U1〜U12 の判断 |
| [WS153](ws153/ws.md) | MG007 | Settings の Apps の頁と third-party の app の repository（userland/packages とは別）。p001 は package の仕組みの検討（2026-10-04 ユーザー） | incomplete・保留（ユーザーが検討中） | U2〜U15 の決定の後に p002 以降 |
| [WS158](ws158/ws.md) | MG006 | Keiland 本体と Keiland の app の翻訳（英語が基準、日本語はベータ2、Settings の Languages の頁で選ぶ。F-068 を昇格）（2026-10-05 ユーザー） | incomplete・保留（元: ベータ3、段は取り消し・整理中） | p002〜p005 の T1・ユーザーの日本語の review、p006 |
| [WS171](ws171/ws.md) | MG008 | hal.h の全ての関数に HAL v2 と同じ水準の契約の comment を書く（comment の差分もユーザーの review の後に当てる）（2026-10-05 ユーザー「このコメント、すべての関数につけてほしいです。今でなくていいので」） | incomplete・保留（優先度低め、ユーザーの review 待ち） | p002 のユーザーの review、p003 の適用 |
| [WS172](ws172/ws.md) | MG006 | passkey の認証の枠組み: sessiond は外部の `/sbin/passkey`（base）で password・PIN・FIDO2（将来はセキュリティチップ）を確かめる、root だけの `/etc/passkey`、PIN の失敗の回数は sessiond の memory（2026-10-05 ユーザー） | incomplete・保留（元: 実装はベータ3、段は取り消し・整理中） | p004b の TPM の設計、p005 OpenSSL の置き換え、p007・p011 の試験、p006 規約 |
| [WS176](ws176/ws.md) | MG006 | Canvas: pen と touch のイラスト制作・画像編集（layer・brush・素材集・CLIP STUDIO と PSD の互換）（2026-10-06 ユーザー、ベータ3） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 から |
| [WS180](ws180/ws.md) | MG006 | Emacs の拡張 — Emacs をベースにしたグラフィカルなエディタ（エージェント開発のための次世代のエディタ） | planning・保留（元: ベータ3 以降、段は取り消し・整理中） | Phase 未定義 |
| [WS184](ws184/ws.md) | MG006 | 左手デバイスの OSK（クリエイターモード: ダイヤル・ホイール・ボタン 2×5、左上の swipe で出す、ベータ2） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 設計から |
| [WS185](ws185/ws.md) | MG006 | ゲームパッドの OSK とゲームコンソールモード（両上隅の同時 swipe、Xbox の pad を模す、段 1 は mview、ベータ2） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 設計から |
| [WS186](ws186/ws.md) | MG003 | Realtek RTL8822CE（5320 の PCIe の WiFi、ベータ3、ベータ2 が早く終われば前倒し） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 調査と設計から |
| [WS197](ws197/ws.md) | MG006 | Bluetooth のスマホ連携: MAP（SMS・MMS）・Integration（WS170）・PBAP・HFP（通話、SCO）。ベータ2 の必須（2026-10-10 ユーザー） | incomplete・保留（元: HFP はベータ3、段は取り消し・整理中） | p006・p007 HFP、p010 の MMS の送信、p014、p008 実機、p009 規約 |
| [WS198](ws198/ws.md) | MG002 | zedBSD の上で zedBSD を self-build（host の clang、2026-10-09 ユーザー、優先度低、ベータ2 の見込み） | incomplete・保留（元: ベータ3、段は取り消し・整理中） | p001 の判定から |
| [WS201](ws201/ws.md) | MG002 | /home の暗号化（UFS の先頭の key slot、FIDO2 の hmac-secret／PRF と回復のパスワード、master key は初回に /dev/random）（2026-10-10 ユーザー、ベータ3、今は検討しない） | planning・保留（元: ベータ3、段は取り消し・整理中） | p001 設計 |
| [WS204](ws204/ws.md) | MG006 | HD Audio の録音（内蔵マイク）と HFP 通話の音（2026-10-11 ユーザー） | incomplete・保留（元: HFP はベータ3、段は取り消し・整理中） | p001 の U1・U2、p002〜p005 |
| [WS205](ws205/ws.md) | MG006 | Bluetooth audio（A2DP の SBC・AAC、headset で HFP 通話、libmedia の encoder）（2026-10-11 ユーザー） | incomplete・保留（元: HFP はベータ3、段は取り消し・整理中） | p001 の D1〜D6、p004〜p011 |
| [WS209](ws209/ws.md) | MG006 | audiod の音の endpoint と route（通話・Bluetooth・modem）（2026-10-11 ユーザー Q12） | incomplete・保留（元: HFP はベータ3、段は取り消し・整理中） | p001 の再 review、p004〜p006 |
| [WS212](ws212/ws.md) | MG006 | Wiseview の作り直し: 仮想デスクトップ 3 枚の矩形と窓の一覧、drag で仮想デスクトップ・display をまたいで移動、multi display の縦の積み重ね、display のローテーション（タグ・Shift+Super+←/→、比率の保持）（2026-10-12 ユーザー） | planning・保留（着手はユーザーが決める） | p001〜p005 |
| [WS222](ws222/ws.md) | MG006 | 画像・PDF・字形の library を menuconfig の Desktop へ戻す（PNG・PDF・JPEG・TrueType）（2026-10-13 ユーザー、今の作業が全部終わってから） | planned・保留（後日） | p001 |
| [WS225](ws225/ws.md) | MG006 | Vulkan の header を仕様から導ける内容だけで書き直し、Khronos の著作権表示を外す（2026-10-13 ユーザー） | planning・保留（ブロック） | p001 |
| [WS234](ws234/ws.md) | MG006 | libkeiland の DLL 版（zedBSD 用と Windows 用）で、同じ .exe が zedBSD と Windows の両方で動く | planning・保留 | p001 |
| [WS235](ws235/ws.md) | MG006 | Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE） | planning・保留（見積もりだけ） | p001〜p007 |
| [WS240](ws240/ws.md) | MG006 | nvrtx の RTX 50（Blackwell）（2026-10-15 ユーザー） | planning・保留（開始しない） | — |
| [WS251](ws251/ws.md) | MG001 | source の tree 全体から開発の管理情報を削除する（2026-10-15 ユーザー） | incomplete・保留（作業対象になった時） | p001 の棚卸し、drivers の外の群 |
| [WS257](ws257/ws.md) | MG006 | i915 を Haswell・Broadwell（Gen7.5・Gen8）に対応（2026-10-18 ユーザー） | planning・保留（今は取り組まない） | — |
| [WS273](ws273/ws.md) | MG008 | Anbernic RG350（JZ4770、MIPS32）の実機への移植（2026-10-19 ユーザー） | incomplete・保留 | 再開は ws273/phase003/phase.md、実機 U26 |
| [WS275](ws275/ws.md) | MG008 | sun4u に /dev/graphics を実装し Xzed を動かす（2026-10-19 ユーザー） | incomplete・保留 | p006（BUG-337）、p007 |
| [WS280](ws280/ws.md) | MG006 | H.264 の software decoder（libh264dec、libmedia から dlopen、任意）（2026-10-21 ユーザー） | planning・保留（後日） | — |
| [WS281](ws281/ws.md) | MG002 | libc の malloc を速くする（first fit・1 本の list・1 つの lock の作り直し）（2026-10-22 ユーザー） | planning・保留（後日） | — |
| [WS282](ws282/ws.md) | MG006 | camera の driver の枠組み（/dev/camN）と USB Video Class の driver（CSI の rpicam は今回やらない） | incomplete・保留（設計だけ、実装は後日） | p002〜p011（設計 版 3 は済み） |
| [WS283](ws283/ws.md) | MG006 | bcm2711 の Vulkan の compute（Pi 4・Pi 5） | incomplete・保留（設計だけ、実装は後日） | p002〜p014 |
| [WS284](ws284/ws.md) | MG006 | YOLOX-Nano の推論（C と Vulkan compute、Apache-2.0） | incomplete・保留（設計だけ、実装は後日） | p002〜p008 |
| [WS285](ws285/ws.md) | MG006 | camera の object detection の demo の app（WS282〜WS284 の上） | planning・保留（後日） | p001 設計 |
| [WS287](ws287/ws.md) | MG008 | EHCI の full-speed・low-speed の device（split transaction、ZYBO の ChipIdea を含む） | planning・保留（後日） | — |
| [WS297](ws297/ws.md) | MG001 | completed の WS に残った「規約の全文の見直し」を一括で（WS296 と同じ形、2026-10-27 ユーザーの回答「1 つの WS にまとめる」） | planned（時期は整理で） | p001 対象の確定（候補 101 Phase・89 WS） |
