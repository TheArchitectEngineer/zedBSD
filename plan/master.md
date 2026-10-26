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
2026-10-27 04:41 時点（詳細は [queue.md](queue.md)）。

| 担当 | Queue | 仕事 | worktree / branch |
| --- | --- | --- | --- |
| P1 | q1607 | [BUG-346](bugs/BUG-346.md)（sshd の子の回収漏れ）を VF2（10.0.30.2）で再現・修正 | `p1-bug346` / `agent/p1-bug346` |
| P2 | q1609 | [ws291-p004](ws291/ws.md)（RDNA の GPU 側の最初の試し、実機 10.0.10.3）。SSH は 1 本の接続を使い回す（watchdog は使わない） | `p2-ws291` / `agent/p2-ws291` |
| P3 | q1608 | 未完の WS の精査（completed・canceled・保留に直す） | `p3-wsaudit` / `agent/p3-wsaudit` |
| P4 | q1610 | plan/ 一式・AGENTS.md・.claude/ の形の整理（この形） | `p4-plan` / `agent/p4-plan` |

- 予約: q1599（P4、[WS252](ws252/ws.md) p002〜p008 の RKL・ADL-S/RPL-S・ICL の GT、pending）。
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
- 公開は rev.5 まで（実質のベータ2）。次に何をやるか（以前の「ベータ3・ベータ4」の段は取り消し）は Q1 とユーザーが整理中。
- **fg023 zedBSD の GHCI 2.0 の driver**（[WS290](ws290/ws.md)）: amaterrace-gpu の `docs/ghci.md` の GHCI 2.0 だけに従う driver に書き直し、host 試験で確かめる。
- **GPU の描画の速さ**（[WS291](ws291/ws.md)、P2）: AMD RDNA → NVIDIA → PowerVR。Linux よりよくなるまで profile を見て直す。
- **Pi 5 の仕上げ**（[WS278](ws278/ws.md)・[WS279](ws279/ws.md)）: BT・BUG-345・GPU の速さ。ユーザーの指示の時。
- 保留: fg020 VisionFive 2。達成: fg021 ZYBO（2026-10-23）、fg019 ベータ2（2026-10-17 OSC）、fg010 OSC のデモ、fg012〜fg016。取り下げ: fg022 KC705 の GPU の board（WS288 の終了）。
<!-- master:focus:end -->

<!-- master:priority:start -->
優先順位（依存による順とは別。Queue の権限は変えない）:
1. WS290: p001 の設計 → ghci.md の疑問（G01〜G39・U1〜U7）をユーザーへ → 実装 → 規約の全文。WS291（P2）は並べて進める。
2. Pi 5 の残り（WS278・WS279）: ユーザーの指示の時。
3. 設計だけの物（WS282〜WS285、WS281 malloc、WS287 EHCI の LS/FS、WS280 H.264）、WS292（GHCI の PROT、WS290 の後）、WS295（xe、p001 の調査だけ）、WS296（drivers の comment の整理）: 後日、担当の起動はユーザーの指示の時。
<!-- master:priority:end -->

### ユーザーの未決

<!-- master:open-decisions:start -->
- WS290: design.md の ghci.md の疑問 G01〜G39・U1〜U7（Q1 がユーザーに示す）。
- WS282〜WS285: 実装を始める時期（ユーザー「設計だけ残してラップアップして、後日実装することにします。」）。再開の時に WS282 の試験の画面を出す PC（D11）と買う webcam（D12）。
- WS279: design.md の D1〜D4。WS278: D1〜D3 と実機の確認。
- WS286: openXC7 の host への導入。LLVM の cache の asset を GitHub の release に upload してよいか（toolchain の変更の時に確かめる）。
- KC705 の Vivado の license（BASIC の tier が 7 Series を含むか、導入の時に実物で確かめる）。
- BUG-345（Pi 5 の click が画面に出ない窓）: 再発したら再起動の前に Q1 に知らせてもらう。
- 以前からの物: WS037 の nvrtx の takeover を既定にするか、UEFI の loader の GOP の mode の選び方、WS204・WS205 の扱い、BUG-222、WS153 U2〜U15（ユーザーが検討中、聞かない）。
<!-- master:open-decisions:end -->

### 止まっている物

<!-- master:blocked:start -->
- WS290 の実機の試験: GHCI 2.0 のハードウェア（am-ws001-p007）ができるまで無い。試験は zedBSD の host 試験だけ（amaterrace-gpu の sim・firmware は使わない）。
- VF2 の zedbsd.cfg は login=graphical を外したまま（元は zedbsd.cfg.p044orig、戻さない）。
- GitHub への公開は push の指示の時だけ。
<!-- master:blocked:end -->

### 次の番号

<!-- master:next:start -->
BUG-347 · q1611 · WS297 · KL_VERSION 96。使う前に衝突を確かめる（`ls plan/bugs`・`ls -d plan/ws*`・`grep q16 plan/queue.md`）。
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

状態の本体は各 ws.md、ここは写し（`planning`・`planned`・`incomplete`）。完了・取り下げは [workstreams-closed.md](workstreams-closed.md) へ移す。
2026-10-27 に P3（q1608）が未完の WS を精査中。結果が出たらこの表を直す。

| WS | Primary | 内容 | 状態 | 再開点 |
| --- | --- | --- | --- | --- |
| [WS001](ws001/ws.md) | MG002 | POSIX.1-2024 準拠 | incomplete | p033〜p039 cleared（patch、df・du、who、stty、dirname、mktemp・install・base64、xargs）、main へ merge。p040 mesg は実装を merge、uncleared（console の case は BUG-067 待ち）。以後はユーザーの指示のときだけ |
| [WS004](ws004/ws.md) | MG003 | ハードウェア拡張 | incomplete（ベータ2、2026-10-13） | NVMe 実機・転送・driver 共通化 |
| [WS009](ws009/ws.md) | MG001 | 文書 | incomplete（ベータ2、2026-10-13） | DOC-54（GPU の文書） |
| [WS013](ws013/ws.md) | MG007 | CPAR（container 分割） | incomplete（Future Work F-002 に保留） | 昇格まで再開しない |
| [WS026](ws026/ws.md) | MG001 | 試験資産の整理 | planning | Phase 未定義 |
| [WS033](ws033/ws.md) | MG005 | networking サービスと有線インタフェースの管理 | incomplete | 抜き差しの実機確認 |
| [WS037](ws037/ws.md) | MG006 | **nvrtx**: NVIDIA RTX 2000 以降（Turing〜Blackwell、GTX 16xx を含む）の GPU driver。WS141 の vc4 と同じ進め方（文書が先、GPL の作業の文書は commit しない、定数の一括の改名、最後に類似の監査、GSP の firmware は userland/firmware）。Pascal 以前は範囲の外（2026-10-04 ユーザー） | incomplete（2026-10-14 開始） | 独立。試験は centris に挿す NVIDIA の GPU の VFIO |
| [WS039](ws039/ws.md) | MG006 | AMD RDNA GPU（amdrdna） | incomplete（p001〜p005・p007 の一部・p008・p011 cleared、vkquad が RX 5600 XT で描ける（amdrdna.vk=1）。p012 の code は P4 の branch） | p006（display の引き継ぎ）→ p009（desktop）、p012 の実機 |
| [WS046](ws046/ws.md) | MG002 | GNU 互換の make（autotools の出力を実行できる範囲。並列・jobserver は WS064） | incomplete（ベータ2、2026-10-13） | p002〜p004・p006 cleared。p007 uncleared（BUG-033 の主因を直した）。p009・p012 cleared（BUG-033: configure 204〜252 → 91 秒、link 0.36 秒、file の fault 15 µs/page）。p013 cleared（libc の mount の一覧の API。coreutils の cross build が通った）。次は p014（p011 の当て直し）・p005 |
| [WS050](ws050/ws.md) | MG003 | USB-C の UCSI driver | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | WS049 が前提 |
| [WS051](ws051/ws.md) | MG006 | USB-C の DisplayPort Alternate Mode | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc §2）: p001・p004a・p004b・p005a cleared） | WS050 と i915 の display が前提 |
| [WS052](ws052/ws.md) | MG003 | 電源管理（S0i3、modern standby、`/dev/system` で制御。S3・S4 は対応しない） | incomplete | p001 設計（§10 決定済み）、p002 調べ（HAL の差分 H1〜H4 は承認待ち）、p003 ACPI の側（実装・host 試験済み、T1 の boot 待ち）。次は p004（HAL に依らない） |
| [WS068](ws068/ws.md) | MG006 | EGL と OpenGL ES（と desktop GL 3.0〜4.6）を Vulkan と display 拡張の上に実装する（Wayland とディスプレイ直接の両方）（2026-09-26・27 ユーザー指示） | incomplete | 自前の GLSL compiler（p003 = p015〜p019、GLSL 1.40〜3.30・ES 3.00 と uniform block の p012 = p020・p021）cleared（2026-09-27、サブエージェント、main へ merge）。次は p013（desktop GL 3.0 の context）・p005（GLES 3.0 の API）・p014（GL 3.3〜4.6）。p002・p008・p010・p006 cleared |
| [WS074](ws074/ws.md) | MG006 | zedBSD の Web ブラウザ `userland/base/zdesktop-browser`（HTML5 の layout engine → 最適化にこだわらない JavaScript engine の接続 → CSS の準拠と Chrome との比較で目標値を段階的に上げる。JS と Wasm の実行 engine を共通化。画像は libpng-compat・新しい libjpeg-compat、TLS は当面 OpenSSL）（2026-09-27 ユーザー指示） | incomplete | p099 cleared（Acid2 100%）。p172 whole cleared（2026-10-03、Codex の browser3 の b-q598）。2026-10-05 Q1 に戻した。レンダリングの改善（p100 Acid3 の pixel 37.04%→100%、p101・p173〜p176）はユーザーの指示まで停止。B1 の試行 p178 を計画。次の候補 BUG-182 |
| [WS075](ws075/ws.md) | MG006 | i915 の高度化: 今日のデスクトップ（zdesktop の glass・backdrop のぼかし・タブ）とグラフィックス（GLES 2/3、GL 3.0〜3.2）を Latitude 5330 の i915 のネイティブ実行器で動かす（compiler の inlining・F-022・F-023 の不足、性能と安定）（2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」） | incomplete | 2026-09-30: L1（C6 91.3 ms）と L2（p029: blur は窓ごと、既定は無効・Settings だけ有効、C6 67.3 ms）を満たした。L3 は p030 で計測（文字の draw 約 400 で約 10 ms）。ユーザーの指示で描画の高速化を止め、p031（文字の draw をまとめる）は build まで済んだ patch（`phase031/exp/text-batch.patch`）で保留。再開はユーザーが描画の高速化の再開を言うとき |
| [WS079](ws079/ws.md) | MG006 | 手書きノート（Notes、筆圧 4096 段階の USB のペンタブレット、PDF に保存し編集の metadata を持つ）と PDF Viewer（scroll と page の swipe）、上の右端から左下へのスワイプで Notes を起動・最前面・全画面（2026-09-28 ユーザー） | incomplete | 2026-09-30: 段 L1（全 Phase）と L2 の QEMU の分（ws079-p016: 台本 S8・S9 を注入の touch と pen で自動で通す、PDF の頁送りの最長 142 ms ≤ 200 ms）を満たした。残り: 実機と Windows の QEMU での確かめ（ユーザー、`plan/ws079/demo-s8-s9-manual.md`）、L3 の実機のペン |
| [WS080](ws080/ws.md) | MG002 | `ld.coff`: Win64 PE32+ の動的ローダ（PE/COFF の mapping・relocation・DLL・import/export・Microsoft x64 ABI・最小の TEB/PEB・GS base）。NT の loader は再現せず `AddressOfEntryPoint` へ直接。互換の DLL は上に積む（2026-09-28 ユーザーの仕様 [spec.md](ws080/spec.md)） | incomplete（p001 の設計の文書まで、p004 in-progress・p005 planned、2026-10-04） | p001（設計）から。GS base は swapgs（案 A）に決定、差分は p001 で承認を得る。path は `/usr/libexec/ld.coff`・`/usr/lib/coff64/`（商標のため Win64 の名前を OS に出さない）。source は `userland/base/ld-coff/`・`userland/desktop/w64/`。橋の DLL は置かず互換の DLL が zedBSD の UAPI を直接呼び Wayland と直接通信。判断待ち: 優先度 |
| [WS081](ws081/ws.md) | MG006 | touch の操作の質: 慣性のある scroll と、低い fps の安い touch panel の数式による補間・予測。HID の driver・compositor・ブラウザ（と Keiland の app）にまたがる計画をこの 1 か所で（2026-09-28 ユーザー） | incomplete | 2026-09-30: L2 の準備 p016 cleared（touch の報告の記録の道具 `plan/ws081/tests/touchlog.c`、Linux の QEMU で 60 Hz・90 Hz の数と欠けを正しく数えた、ユーザーの 3 行の手順 `windows-touch.md`）。次は Windows の QEMU 用の image（p018、P4）→ ユーザーの計測 → L3 の p017 |
| [WS082](ws082/ws.md) | MG002 | Linux の `/dev/kvm` の移植の検討（eventfd 等の非 POSIX の fd の代わりに unix socket の message で MMIO・IRQ の通知。ioctl を直接の移植・別の仕組みでの代替・実装不能に分類）（2026-09-28 ユーザー） | planning（最初に「アイディアだけ描く」Phase を実行しユーザーと議論、2026-10-05 ユーザー） | p001 cleared（[study.md](ws082/study.md)）。§10 の 11 項目のユーザーの判断待ち |
| [WS083](ws083/ws.md) | MG006 | Vulkan Video の拡張（`VK_KHR_video_queue`・`video_decode_queue`・`video_decode_h264`）と i915 の VCS・MFX の対応、最初の目標は H.264 の decode（2026-09-28 ユーザー。OSC のデモには必須ではない） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | p001（設計）から。デモの後 |
| [WS084](ws084/ws.md) | MG006 | i915 の firmware の画面の引き継ぎ（素の実機の UEFI の起動で GOP が点けた pipe を N1 で止めて driver のものにし、デスクトップを出す）（2026-09-29 ユーザー、main が実装） | incomplete | p001・p002 cleared（2026-09-29、素の 5330 で takeover → LCD の Keiland、24.5 present/s）。残り: parity との乖離 1〜3 の整理 |
| [WS089](ws089/ws.md) | MG006 | 設定のアプリ（Settings: 左に項目の pane、右に設定、浮いたすりガラスの pane）（2026-09-29 ユーザー、デモの優先事項） | incomplete | p010（回帰と棚卸し、候補 C1〜C17）・p012（検索の key・touch の scroll・Wi-Fi の待ちの slot・文言）は済み。p019（titlebar の検索欄の Down・Up、compositor 側）。標準 app の開発は WS131 の後に組み直す |
| [WS090](ws090/ws.md) | MG006 | widget・control の共有 library（少なくとも慣性の smooth scroll、独自の部品）（2026-09-29 ユーザー） | incomplete | 2026-09-30: p001〜p006・p013・p008（PDF Viewer・Image Viewer）・p011（Terminal・Notes の窓）・p014（file chooser を親の title bar にぶら下がる sheet、不透明）cleared。KUI_VERSION 11・KEILAND_VERSION 20。統合の試験（demo-s8-s9.sh、QEMU）PASS。残り: p015（案、scroll を kui_scroll へ）・p009・p010（Files）・p007（Settings）、Terminal の p088 の切り分け。ラップアップ（2026-09-30 夕） |
| [WS094](ws094/ws.md) | MG006 | desktop の file の icon（`~/Desktop`）（2026-09-29 ユーザー） | incomplete | p014（規約の指摘）cleared。残り p012（5330 の実機）・p007（全回帰） |
| [WS095](ws095/ws.md) | MG006 | IME（Wayland の input-method-v2・text-input-v3、単一の IME・複数言語、まず日本語、REmacs の辞書）（2026-09-29 ユーザー） | incomplete（ベータ2・優先度低め、2026-10-13） | p012（辞書 1,478 見出し）・p013（変換中の文字を本文と同じ大きさで inline）・p005（候補の窓・右上の IME の status・key repeat）cleared（QEMU）。実機の目視はユーザー |
| [WS098](ws098/ws.md) | MG006 | IME の変換のニューラル化: 辞書で候補を作り、小型のモデル（15 MB 未満）で同音異義語の選択（語の番号の並び）とひらがな列の形態素解析（語の境界と品詞、BiLSTM か小型の Attention）を評価する（2026-09-29 夜 ユーザー、IME の最後の仕上げ） | planning | WS095 の基本の辞書の後。学習の corpus と license はユーザーの判断 |
| [WS099](ws099/ws.md) | MG006 | Keiland の compositor（zdesktop）のデモの基準: 窓の操作・App Home・Wiseview・全画面と最大化の解除・greeter から Log Out と Shut Down・すりガラスの上の文字の contrast・回帰の試験の全通過（2026-09-30 ユーザー、WS035 の後継。基準は ws.md） | incomplete | p023（BUG-136/137、直す前からの C 基準の失敗）cleared。p020（BUG-125）と p021 は BUG-147 の試験の頑健化（p024、P2 が作業中）の後に判断 |
| [WS101](ws101/ws.md) | MG006 | GPU の compute: i915 の Vulkan の compute（dispatch・shared memory・barrier・atomic）、libglesv2 の GLES 3.1 の compute、Noct の自動並列化（accel_opengles）が 5330 の GPU で動く（2026-09-30 ユーザー、10/17 のデモまで、最優先ではない） | incomplete | 2026-09-30: L1 で S13 が 5330 で通った。L2: p016（時間の分解）cleared、p017（buffer の使い回しと copy の削減、QEMU の GPU の call 835 → 212 ms、CPU の 10.6 倍遅い）uncleared。ユーザーの判断「今のまま」で S13 は今の見本、最適化はここで止める（5330 の p017 の値は P1 が追記） |
| [WS102](ws102/ws.md) | MG006 | スクリーンキーボード: 右下の角の swipe で右側に flick の panel（英字・記号・日本語）、左下の角の swipe で下側に QWERTY と手書き（認識は stub）。compositor に直接（2026-09-30 ユーザー） | incomplete | 2026-09-30: L1 を満たした。L2: p006・p007・p008・p009・p015・p016（右の列の道具の面）・p017・p018・p020・p021・p023・p024（履歴の tab。受け入れの手順だけ PASS、全手順の回帰・C9・boot test は未実施）、L3 の p019（色付きの絵文字）cleared（QEMU）。ユーザーの指示で優先を下げてラップアップ（2026-09-30 夕）。保留: p022（絵文字の tab）・BUG-125・p010・p011（速さ）・p012（IME、人間） |
| [WS106](ws106/ws.md) | MG001 | base/desktop の test/probe/demo 30件を userland/tests/ へ移し、package/config/install と既存の動作を維持 | incomplete | q540 partial cleared、p002 uncleared（ime-probe回答待ち）、p003未実行。 |
| [WS112](ws112/ws.md) | MG007 | Linux5種類のbinary packageを指定make/CIで作成しreleaseへ添付 | incomplete | p001/q585契約調査uncleared、D1 Fedora/Arch boot回答待ち。RPi arm64、CI runtime不要、FreeBSD source-only。q591は候補のみ |
| [WS113](ws113/ws.md) | MG006 | zedBSD i915 hotplug/Vulkan Displayから複数画面・Settings/libkeiland・窓の全体移動 | incomplete | p001/q586設計調査uncleared、D-ATOMIC未決、A3成果回収/終了。全拡張/全mirror、pointer越境で窓一括移動。実装未投入 |
| [WS115](ws115/ws.md) | MG002 | upstream GTK4をzedBSD `packages/desktop/gtk4`へ移植し知見を記録 | incomplete | p001・p004〜p009・p002（GTK 4.18.6 と依存一式の build）cleared。p010（zedBSD で起動）は ld.so の上限で止まり、ユーザーの判断で後回し（再開点は phase010） |
| [WS116](ws116/ws.md) | MG002 | upstream Qt6の範囲をGTK4移植後に検討し `packages/desktop/qt6`へ移植 | planning | WS115の知見後。旧WS034 p030移管、Queue none |
| [WS117](ws117/ws.md) | MG006 | Linux の本物の Qt6 を調査し、素の Qt6 アプリが動くよう compositor を改良（WS115/116 の前） | planned（2026-10-07 ユーザー（クリック）: ベータ3 以降） | WS115・WS097 の後 |
| [WS119](ws119/ws.md) | MG003 | インストーラの作り直し | planning | p001 planned（要件の案） |
| [WS121](ws121/ws.md) | MG006 | Web ブラウザでのアクセラレーションつきのビデオ再生（fg019） | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2.3）: **ベータ3**（browser と一緒、WS074）） | p001（2026-10-05 Q1 の担当、P2 が q775 で設計中、ベータ2） |
| [WS124](ws124/ws.md) | MG002 | GNU Emacs の package（fg019） | planning | p001・p002 planned |
| [WS125](ws125/ws.md) | MG002 | vim の package（fg019） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | p001・p002 planned（p002 は package の tree を image に入れる共通の仕組み） |
| [WS126](ws126/ws.md) | MG002 | Python 3 の package（fg019） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | p001 planned |
| [WS129](ws129/ws.md) | MG007 | ベータ1 のリリース作業（版・release notes・既知の問題・CI の release・最終回帰）（fg019） | incomplete | p009（デモの image を CI 土台に）・p010（全 desktop app と base の program を config へ）cleared。版 zedbsd-0.1.0-beta1、Prerelease を user が手で昇格 |
| [WS130](ws130/ws.md) | MG005 | IPv6 の network stack（ベータ1 は計画だけ、実装はベータ2 以降。DHCPv6 は `dhcpc -6`） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | p001 設計 |
| [WS131](ws131/ws.md) | MG006 | libkeiland を GUI toolkit 兼 desktop 機能の抽象化層にする（app の窓の作成を含む GUI の構築を共通化） | incomplete | p003〜p005 cleared、p006（Linux は T1-042 PASS、zedBSD の T2-007 は未実行）・p007（demo-s8-s9 未実行）・p008（試験未依頼）は uncleared。次は p008 の試験と p009。設定の部分は WS135 へ |
| [WS132](ws132/ws.md) | MG006 | /dev/system の電源管理と PnP の通知（subscriber が事象を指定）、自動 mount、Files の eject | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2）: p001 を cleared） | p001 設計（/dev/system に電源管理と PnP の通知、subscriber が事象を指定） |
| [WS134](ws134/ws.md) | MG006 | システムモニターのアプリ（Analytic Spatial UI、中央の状態コア、層構造、2026-10-03 ユーザー） | incomplete | p001・p002 cleared、p003 uncleared（sim の fps 4.7）、p004 uncleared（`interact.c` まで） |
| [WS141](ws141/ws.md) | MG006 | Raspberry Pi 4 のグラフィックス driver（VideoCore VI: HVS・pixelvalve・HDMI と V3D 4.2）。Linux の vc4・v3d の初期化の順と command の順を先に文書にし、i915 の書き換えを手本に我々の interface へ。framebuffer に段の印。Linux の vc4・v3d は GPL なので code は写さない（2026-10-04 ユーザー） | incomplete（display/V1〜V10/二device share/Normal NC/worker/SPIR-V compiler/typed Vulkan/native draw-transfer/Que…） | 独立（他の WS と並走） |
| [WS143](ws143/ws.md) | MG006 | Bluetooth（Settings の stub の頁の実体、HCI・daemon・desktop の経路）。ベータ2 の実装の項目、時期は未定（2026-10-04 ユーザー） | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc §2）: p004 cleared） | — |
| [WS144](ws144/ws.md) | MG005 | VPN（bridge・tunnel などの汎用の network の基盤と、選んだ VPN の protocol、Settings の stub の VPN の頁の実体）。ベータ2 の実装の項目、時期は未定（2026-10-04 ユーザー） | planning（Queue なし） | — |
| [WS145](ws145/ws.md) | MG006 | 印刷: printer の daemon（IPP・LPD で PDF）、libkeiland の印刷の口と printer の一覧（compositor 経由、backend が daemon を起動）、Settings の Printers の頁（IP・port・protocol）、後に PostScript・vendor の filter（2026-10-04 ユーザー） | incomplete（2026-10-08 q910 P2 の照合: p001〜p004・p007 cleared（2026-10-07・08）） | — |
| [WS146](ws146/ws.md) | MG006 | SSH を使う独自のオンラインストレージ（server に agent を転送、file の木と metadata の database、差分の同期、手元に本体の無い on-the-fly のアクセス、OneDrive のような機能）。「あとで」の要望（2026-10-04 ユーザー） | planning（Queue なし） | — |
| [WS147](ws147/ws.md) | MG006 | Microsoft OneDrive の client（Settings の Sharing の頁から設定、OAuth・Graph の API・同期、on-the-fly は WS146 と共有）（2026-10-04 ユーザー「独立WSにします」） | planning（Queue なし） | — |
| [WS150](ws150/ws.md) | MG004 | userland の file system の kernel の枠組み（FUSE に当たる物）。クラウドストレージ（WS146・WS147）の前提（2026-10-04 ユーザー） | planning（Queue なし） | — |
| [WS152](ws152/ws.md) | MG007 | system の更新（Settings の Updates の頁の実体）。ベータ3 の実装の項目、p001 は方式の検討（2026-10-04 ユーザー） | planning（p001 から、Queue なし） | WS129 |
| [WS153](ws153/ws.md) | MG007 | Settings の Apps の頁と third-party の app の repository（userland/packages とは別）。p001 は package の仕組みの検討（2026-10-04 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | — |
| [WS154](ws154/ws.md) | MG006 | Settings の Languages の頁で IME を選ぶ（日本語・SKK・なし=英語）と、SKK の IME の新しい実装（辞書は Emacs の物）（2026-10-04 ユーザー） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | WS095 |
| [WS155](ws155/ws.md) | MG006 | Keiland の app: カレンダー・スケジューラ・オーガナイザ（まず簡単な物）（2026-10-04 ユーザー） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | — |
| [WS156](ws156/ws.md) | MG006 | app の通知: 画面の下の中央を流れる headline の popup（右から中央、3 秒、左へ fade-out、× で消す）と hotkey の ring の log（すべて消去、個別に消した物は残さない）（2026-10-04 ユーザー） | incomplete | — |
| [WS157](ws157/ws.md) | MG006 | Keiland の app: 写真の管理（p001 は要件の検討）（2026-10-05 ユーザー） | incomplete（ベータ2、GUI の UAT OK 2026-10-13、T1 待ちの Phase あり） | — |
| [WS158](ws158/ws.md) | MG006 | Keiland 本体と Keiland の app の翻訳（英語が基準、日本語はベータ2、Settings の Languages の頁で選ぶ。F-068 を昇格）（2026-10-05 ユーザー） | incomplete | WS154 |
| [WS159](ws159/ws.md) | MG006 | native のタッチパッド（LPSS I2C・I2C-HID・HID の digitizer、evdev の MT）と compositor のタッチパッドの層（tap・tap-drag・押し込み・2 本指のスクロール）（2026-10-05 ユーザー「ACPI AMLを実装したあと、I2C-HIDを実装しましょう。compositorのtouchpad層も作りましょう。」） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | WS049 |
| [WS161](ws161/ws.md) | MG006 | YubiKey のサポート（最初は USB の FIDO2、目標は NFC の CTAP2）（2026-10-05 ユーザーの追加） | incomplete（2026-10-08 q902 P1 の照合: p002〜p005 cleared） | — |
| [WS162](ws162/ws.md) | MG006 | FIDO2 の login（greeter・lock の画面で security key で login）（2026-10-05 ユーザーの追加） | incomplete（WS172 に吸収、未着手の Phase は canceled で WS172 p003 へ） | — |
| [WS164](ws164/ws.md) | MG006 | OS の起動時の Welcome の画面（既存の Start で足りるかの検討から）（2026-10-05 ユーザーの追加） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | — |
| [WS165](ws165/ws.md) | MG006 | 手書きの入力（goal の設定が難しいので段階化する）（2026-10-05 ユーザーの追加） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | — |
| [WS169](ws169/ws.md) | MG006 | メーラの app と compositor のメールの API（許可された app が受信の通知を受ける、browser の認証 code の自動入力、IMAP4・SMTP から Gmail・Outlook へ）（2026-10-05 ユーザーの追加） | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2）: p000 を cleared） | — |
| [WS170](ws170/ws.md) | MG006 | Phone の app（連絡先・SMS/MMS/RCS・VoIP を統合したタイムライン、compositor のメッセージの API、モデム・スマホの bridge の backend。最初は連絡先からタイムラインの表示まで）（2026-10-05 ユーザーの追加） | incomplete（ベータ2 の範囲は完了 2026-10-13、残りは規約の全文の見直し＝ベータ3） | — |
| [WS171](ws171/ws.md) | MG008 | hal.h の全ての関数に HAL v2 と同じ水準の契約の comment を書く（comment の差分もユーザーの review の後に当てる）（2026-10-05 ユーザー「このコメント、すべての関数につけてほしいです。今でなくていいので」） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | — |
| [WS172](ws172/ws.md) | MG006 | passkey の認証の枠組み: sessiond は外部の `/sbin/passkey`（base）で password・PIN・FIDO2（将来はセキュリティチップ）を確かめる、root だけの `/etc/passkey`、PIN の失敗の回数は sessiond の memory（2026-10-05 ユーザー） | incomplete（ベータ2、2026-10-13） | WS161 |
| [WS173](ws173/ws.md) | MG006 | AAT（Agent Acceptance Test）: /dev/input-inject のマウスとキーボード、試験の image だけの画面の撮影、SSH の host の道具で、エージェントが素の 5330 を操作して受け入れを確かめる。UAT はデバイス系と使用感に絞り AAT の後に遅らせる（2026-10-05 ユーザー） | incomplete（p004・p006 は T1-202 待ち） | — |
| [WS174](ws174/ws.md) | MG003 | 起動時に Ctrl で kernel の message を console に、Shift で console の login に（UEFI の bootloader だけ、BIOS は後日）（2026-10-05 ユーザー） | incomplete（p001・p002・p005 cleared、p003 は T1-213 待ち） | — |
| [WS175](ws175/ws.md) | MG006 | Notes で PDF の画像と文字を編集（移動・大きさ・差し替え・挿入、文字の編集・削除・挿入・font、複数頁）（2026-10-06 ユーザー） | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2）: p002・p010 は PNG の目視まで保留） | — |
| [WS176](ws176/ws.md) | MG006 | Canvas: pen と touch のイラスト制作・画像編集（layer・brush・素材集・CLIP STUDIO と PSD の互換）（2026-10-06 ユーザー、ベータ3） | planning（ベータ3） | — |
| [WS177](ws177/ws.md) | MG006 | ベータ2 積み残し（準正常系・異常系） | incomplete（2026-10-08 q902 P1 の照合: 19 Phase のうち 12 cleared、p016〜p018 は Q1 の判定待ち、p002・p003・p006・p011 は実機・UAT、p0…） | — |
| [WS180](ws180/ws.md) | MG006 | Emacs の拡張 — Emacs をベースにしたグラフィカルなエディタ（エージェント開発のための次世代のエディタ） | planning（2026-10-07 ユーザーの依頼で WS だけ作成） | — |
| [WS181](ws181/ws.md) | MG006 | 窓の docked・floating・最小化の状態、App Home の独立のモード、gesture、整列のメニューと整列モード（UAT 2026-10-07） | incomplete（2026-10-08 Q1 の判定（sweep-beta2-rc2 §2）: p011 はユーザーの返事待ちで保留） | — |
| [WS184](ws184/ws.md) | MG006 | 左手デバイスの OSK（クリエイターモード: ダイヤル・ホイール・ボタン 2×5、左上の swipe で出す、ベータ2） | planning | p001 設計 |
| [WS185](ws185/ws.md) | MG006 | ゲームパッドの OSK とゲームコンソールモード（両上隅の同時 swipe、Xbox の pad を模す、段 1 は mview、ベータ2） | planning | p001 設計 |
| [WS186](ws186/ws.md) | MG003 | Realtek RTL8822CE（5320 の PCIe の WiFi、ベータ3、ベータ2 が早く終われば前倒し） | planning | p001 調査と設計 |
| [WS189](ws189/ws.md) | MG006 | app の間の drag and drop（画像、受け入れの見た目、dock の spring-loaded、desktop に file、画面をまたぐ）（2026-10-08 ユーザー） | incomplete（2026-10-08 q902 P1 の照合: p001・p004 cleared、p002・p003 は T1 の手操作で大半を確認、残りの項目は T1 の束） | p001 設計（q892） |
| [WS190](ws190/ws.md) | MG006 | 文字の欄と Text Editor の指での選択（ダブルタップ・端の drag・コピー・切り取り・貼り付け・すべて選択の popup）（2026-10-08 ユーザー、WS189 の後） | incomplete | p001 設計 |
| [WS191](ws191/ws.md) | MG006 | 再生の音を libkeiland の audio stream の口へ（compositor・backend、zedBSD audiod・Linux PipeWire・FreeBSD OSS）（2026-10-08 ユーザー） | incomplete（2026-10-13 P2 q964 の照合: p001〜p004 は各 phase.md で cleared（2026-10-08 Q1、T1-444・T1-448・T1-454）、残りは p00…） | p001 設計 |
| [WS193](ws193/ws.md) | MG006 | make menuconfig のメニュー階層の作り直しと Build boot image（進捗の bar、-j$(nproc)）（2026-10-09 ユーザー、ベータ2） | incomplete（ベータ2 の範囲は完了 2026-10-13 Q1 判定: 完了の条件 1〜3 は満たした、RPi4 の実機の確かめはユーザーの UAT で済みとする（2026-10-13 ユーザーの UAT の一…）） | p001 実装 |
| [WS194](ws194/ws.md) | MG006 | make keiland-linux（apt・yum・pacman）・keiland-freebsd（pkg）の必要な package の確認と導入、build 後の install の確認（2026-10-09 ユーザー、ベータ2） | incomplete（2026-10-18 Q1: Phase を実行済みなので planned から直した） | p001 実装 |
| [WS197](ws197/ws.md) | MG006 | Bluetooth のスマホ連携: MAP（SMS・MMS）・Integration（WS170）・PBAP・HFP（通話、SCO）。ベータ2 の必須（2026-10-10 ユーザー） | incomplete | 2026-10-11: p001〜p005・p011〜p013 cleared、p010 は受信だけ。残りは HFP p006・p007、MMS の送信、実機 p008、規約 p009（ベータ3） |
| [WS198](ws198/ws.md) | MG002 | zedBSD の上で zedBSD を self-build（host の clang、2026-10-09 ユーザー、優先度低、ベータ2 の見込み） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | p001 前提の調べ |
| [WS201](ws201/ws.md) | MG002 | /home の暗号化（UFS の先頭の key slot、FIDO2 の hmac-secret／PRF と回復のパスワード、master key は初回に /dev/random）（2026-10-10 ユーザー、ベータ3、今は検討しない） | planning | p001 設計 |
| [WS202](ws202/ws.md) | MG006 | libavcodec なしで libmedia だけで mp4（H.264 は Vulkan Video・AAC-LC は自前）と .m4a を再生（Video Player・Music）（2026-10-11 ユーザー、設計だけ、実装は別のセッション） | incomplete（native再生softwareをmainへ統合、実機と全受入matrixは未確認） | 設計の第 4 版（review 3 回）、87 LW、ユーザーの判断 H1〜H6・J1〜J10 待ち |
| [WS203](ws203/ws.md) | MG005 | Raspberry Pi 4 内蔵 Ethernet (BCM2711 GENET v5) | incomplete | — |
| [WS204](ws204/ws.md) | MG006 | HD Audio の録音（内蔵マイク）と HFP 通話の音（2026-10-11 ユーザー） | incomplete（2026-10-11 P1 q929: p001 の設計） | P1 q929 |
| [WS205](ws205/ws.md) | MG006 | Bluetooth audio（A2DP の SBC・AAC、headset で HFP 通話、libmedia の encoder）（2026-10-11 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P1 q931 |
| [WS206](ws206/ws.md) | MG005 | Raspberry Pi 4 の WiFi（2026-10-11 ユーザー） | incomplete（p001 は第 3 版の review-3 と E2・E4・E6 の回答の後、clearance は Q1 の判断待ち） | P1 q932 |
| [WS207](ws207/ws.md) | MG006 | Raspberry Pi 4 のオーディオ（再生・録音）（2026-10-11 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P1 q933 |
| [WS208](ws208/ws.md) | MG008 | arm64 の HAL の SMP（Pi 4 の 4 core）（2026-10-11 ユーザー） | incomplete（2026-10-13 P6: p007 で Pi の 4 core の固まりの検出（stall watch）と SMP の 2 つの直し） | P2 q939（q938 BUG-288 の後） |
| [WS209](ws209/ws.md) | MG006 | audiod の音の endpoint と route（通話・Bluetooth・modem）（2026-10-11 ユーザー Q12） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P1 |
| [WS210](ws210/ws.md) | MG006 | Keiland の app の描画を GPU へ（libkeiland の canvas・文字・画像を Vulkan で）（2026-10-12 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P1 |
| [WS211](ws211/ws.md) | MG006 | Pi 4 の compositor の描画の削減（App Home の遷移のクロスフェード、draw の束ね）（2026-10-12 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P3 |
| [WS212](ws212/ws.md) | MG006 | Wiseview の作り直し: 仮想デスクトップ 3 枚の矩形と窓の一覧、drag で仮想デスクトップ・display をまたいで移動、multi display の縦の積み重ね、display のローテーション（タグ・Shift+Super+←/→、比率の保持）（2026-10-12 ユーザー） | planning | 未割当 |
| [WS213](ws213/ws.md) | MG006 | compositor の thread 化（入力・cursor と描画の分離、重いコマの間も入力と cursor が止まらない）（2026-10-12 ユーザー） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P4（p001 設計） |
| [WS214](ws214/ws.md) | MG006 | 窓をディスプレイの重ね合わせ（HVS の plane）に載せる（Pi 4、drag・dock の間 GPU が窓を描き直さない）（2026-10-12 ユーザー「ぜんぶやりましょう。」） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | P2（p001 設計） |
| [WS215](ws215/ws.md) | MG005 | Raspberry Pi 4 の Bluetooth（内蔵の BCM43455、UART の HCI）（2026-10-12 ユーザー） | incomplete（2026-10-12 Q1 作成） | 未割当（SMP・最適化の終了の後） |
| [WS216](ws216/ws.md) | MG006 | Notes の描く道具（筆・鉛筆・領域の塗りつぶし・水彩）（2026-10-13 ユーザーの UAT） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | 未割当（BUG-296 の後） |
| [WS217](ws217/ws.md) | MG006 | 画面の回転（Settings の Display、縦長で Notes を使う）（2026-10-13 ユーザーの UAT） | incomplete（2026-10-18 Q1: Phase を実行済みなので planning から直した） | 未割当（まず設計） |
| [WS218](ws218/ws.md) | MG005 | Intel WiFi（AX211）driver の license の表示を実態に合わせる（OpenBSD・Intel の license の削除の監査）（2026-10-13 ユーザー） | incomplete（p002 は実装・host 試験済み、AX211 の QEMU・実機の試験待ち） | p001 の記録の締め、p002 の試験 |
| [WS219](ws219/ws.md) | MG006 | Venus の protocol の op の流用の整理（定数名の付け直し、Venus の著作権表示を外し引用の明記に留める）（2026-10-13 ユーザー） | incomplete（2026-10-13 P2 が p001 を実施、判断待ち） | 未割当（WS218 p001 の後の P1） |
| [WS220](ws220/ws.md) | MG005 | TCP の転送の速さの安定（継続の転送ですぐ 1 MB/s 未満に落ちる、window の制御の直し）（2026-10-13 ユーザー） | incomplete（2026-10-13 Q1 作成） | 次に空いた P（WS219 より先） |
| [WS221](ws221/ws.md) | MG005 | Jumbo frame（Ethernet の L2 の大きな MTU、device ごとの上限、IP・TCP の MSS）（2026-10-13 ユーザー） | incomplete（2026-10-13 Q1 作成） | 次に空いた P |
| [WS222](ws222/ws.md) | MG006 | 画像・PDF・字形の library を menuconfig の Desktop へ戻す（PNG・PDF・JPEG・TrueType）（2026-10-13 ユーザー、今の作業が全部終わってから） | planned | 最後（今の作業の後） |
| [WS223](ws223/ws.md) | MG006 | 全画面は compositor の合成、game mode は Wayland の拡張で求められた時だけ（Video Player は game mode をやめる）（2026-10-13 ユーザー） | planning | 次に空いた P |
| [WS225](ws225/ws.md) | MG006 | Vulkan の header を仕様から導ける内容だけで書き直し、Khronos の著作権表示を外す（2026-10-13 ユーザー） | planning（blocked、ベータ2 で実装） | 着手しない |
| [WS226](ws226/ws.md) | MG006 | Khronos の Vulkan 1.3 の header をそのまま使い、libvulkan で Vulkan 1.3 の API を実装しきる（不整合の整理）（2026-10-13 ユーザー） | planning | 次に空いた P |
| [WS229](ws229/ws.md) | MG006 | i915 を Gen9・Gen9.5（Core の 6〜10 世代の iGPU）に対応（GOP のままで GT と EU → 最後に display controller）（2026-10-14 ユーザー、ベータ2 に入る、既定で有効） | incomplete（p001〜p005・p007 cleared、display controller の p006 はベータ3） | P1（q976 p001） |
| [WS231](ws231/ws.md) | MG002 | AHCI（SATA）の driver（2026-10-14 ユーザー、ベータ2 に入る） | incomplete（p001・p002 cleared、NCQ・TRIM・hotplug・ATAPI は後） | — |
| [WS232](ws232/ws.md) | MG002 | menuconfig に i386（PC-9800・PC/AT）と Retro の menu（Xzed・zwm・zshell・zterm）（2026-10-14 ユーザー） | incomplete（2026-10-14 P1: p004 を追加・実行中） | — |
| [WS233](ws233/ws.md) | MG002 | firmware を awemorris/zedBSD-firmware から取る | incomplete（2026-10-14 Q1 起動、P1） | — |
| [WS234](ws234/ws.md) | MG006 | libkeiland の DLL 版（zedBSD 用と Windows 用）で、同じ .exe が zedBSD と Windows の両方で動く | planning・**保留**（2026-10-14 ユーザーが起票） | — |
| [WS235](ws235/ws.md) | MG006 | Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE） | planning（2026-10-14 起票） | — |
| [WS236](ws236/ws.md) | MG006 | Raspberry Pi 4（VideoCore VI）の動画の再生の hardware 支援 | incomplete（2026-10-14 着手、P3） | 残りは優先度が低い: Pi を main の libvulkan・wayland に戻して 720p30 を確かめる、Phase の状態の整理（p001〜p005・p007・p011 が in-progress、p006…） |
| [WS238](ws238/ws.md) | MG004 | amd64 の native の image を ESP（loader だけ）・FAT32（kernel と boot の設定）・UFS の root・swap の構成にする | incomplete（2026-10-14 夜 ユーザー指示、ベータ2 に入れる） | — |
| [WS239](ws239/ws.md) | MG002 | Wayland（compositor）の起動の失敗時に text console の login へ戻す | incomplete（2026-10-15: p001 の実装・host 試験済み、QEMU の確かめ p002 待ち） | — |
| [WS240](ws240/ws.md) | MG006 | nvrtx の RTX 50（Blackwell）（2026-10-15 ユーザー） | planning（開始しない、ユーザー「50はまだ開始しないでください。」） | — |
| [WS241](ws241/ws.md) | MG006 | nvrtx（RTX）の描画の速さ（2026-10-15 ユーザー） | incomplete（p001 in-progress、T1-697 で boost が効く） | p001 の 3 節（boost を既定に、rec submit、streams） |
| [WS242](ws242/ws.md) | MG006 | nvrtx の RTX 20/30/40 の全モデル（2026-10-15 ユーザー） | incomplete（p001・p002 in-progress、GTX 16・RTX 30・40 を既定で有効はユーザーの決定、未実装） | p001 の再開点 1〜4 |
| [WS243](ws243/ws.md) | MG006 | nvrtx の NVDEC で H.264（Vulkan Video）（2026-10-15 ユーザー） | incomplete（p001 を途中で一時停止、実機は WS241 の後） | p001 の再開の点（design.md） |
| [WS246](ws246/ws.md) | MG006 | nvrtx の compiler を sm75.h・sm75.c にまとめて refactor（2026-10-15 ユーザー）、2026-10-22 ユーザーの指示で compiler/sm75/ の 14 file に分け直した（d73ca1f65） | incomplete（p001 cleared、出典の comment は ws247-p003 で済み） | p002（latency の表、構造） |
| [WS248](ws248/ws.md) | MG005 | Intel AX211 の driver の出典の表記（block ごとの referenced:）と規約の整理（2026-10-15 ユーザー） | incomplete（p001・p002 cleared、main 285c55e39。T1-698: 5330 の VFIO で attach・firmware・scan が整理の前と同じ、T1-700: VFIO の接続は整理の前も後も失敗（差なし）） | 5330 の素の起動で接続を確かめて completed（beta2.md の U9、2026-10-15 ユーザー「実機で接続を確かめてから完了」） |
| [WS249](ws249/ws.md) | MG005 | BCM43455（Pi 4 の WiFi）の driver の出典の表記（block ごとの referenced:）と規約の整理（2026-10-15 ユーザー） | incomplete（p001・p002 cleared、main 331d314be。Pi 4 の実機の WiFi の試験待ち） | T の Pi 4 の試験 → completed |
| [WS250](ws250/ws.md) | MG006 | nvrtx の終わらない GPU の仕事の取り消しと復帰、system は動き続ける（2026-10-15 ユーザー） | incomplete（p001 を P4 が q1206 で設計中） | p001 |
| [WS251](ws251/ws.md) | MG001 | source の tree 全体から開発の管理情報を削除する（2026-10-15 ユーザー） | incomplete（p002 GPU ドライバ cleared 2026-10-19、p001・p003〜 は planned） | p001 の tree 全体の棚卸し（未割当） |
| [WS252](ws252/ws.md) | MG006 | i915 の RKL・ADL-S/RPL-S・ICL の GT の対応（2026-10-15 ユーザー、WS245 §7 #20） | incomplete（p001 cleared 2026-10-19 q1265、design.md 版 3.1。p002〜p008 は実機まで planned。LW: RKL・ADL-S 12、全部 約 27〜29） | 判断は回答済み（D2 全部・D1 C・D3 Meteor Lake の時・D5 無いまま）。実機ができたら p002 から |
| [WS253](ws253/ws.md) | MG001 | 起動の時間の短縮: 通常の起動は sessiond（Wayland）を先に、network・audio・sshd などは後に。debug の起動は今のまま（2026-10-16 ユーザー） | incomplete（p001: 2 段の起動・desktop の表示を待つ late・kernel の disk の待ちの短縮を main に d454d4bd3、QEMU で約 9 s → 約 4 s。5330 の確認待ち） | 5330 の起動の記録（P1 の次の起動で） |
| [WS254](ws254/ws.md) | MG001 | networkd: DHCP の失敗の後に 1 分おきに取り直す（2026-10-16 ユーザー） | incomplete（p001 cleared: QEMU で確認、main 5cb63ff2a。実機は未実施） | p002 規約の見直し |
| [WS255](ws255/ws.md) | MG001 | 試験機（5330・RDNA 機）の zedBSD の試験の image を静的な IP に（2026-10-16 ユーザー） | incomplete（p001 uncleared: 手引き・QEMU で address は確認、main d2a709ba6。route と DNS は BUG-328 待ち） | BUG-328 の修正 |
| [WS256](ws256/ws.md) | MG008 | StarFive VisionFive 2（JH7110、RISC-V）への移植（2026-10-18 ユーザー） | incomplete（p002 toolchain の RISCV は cleared、他は詳細を Q1 とユーザーで決めてから P2） | p004 → p005（UART の login）→ p011 framebuffer → p013〜p019（SD・rootfs・USB keyboard・NVMe・LAN・sound）。p012 の周辺の設計は今から可 |
| [WS257](ws257/ws.md) | MG006 | i915 を Haswell・Broadwell（Gen7.5・Gen8）に対応（2026-10-18 ユーザー） | planning（今は取り組まない） | — |
| [WS259](ws259/ws.md) | MG005 | RTL8822B の表（rtl8822b-tables.inc）を値の引用に書き換える（2026-10-18 ユーザー） | incomplete（p002 cleared、p001 は license の判断と実機の WiFi 待ち） | p001 |
| [WS260](ws260/ws.md) | MG006 | i915 の Gen9 の display（gen9-*.c）を xd と同じく自前で書き直す（2026-10-18 ユーザー） | incomplete（p001 cleared、p002〜p015 は実装済み、T1-710 の SV7・5330 の実機の回帰の後に Q1 が判定、2026-10-19） | T1-710（機械の準備待ち、beta2.md U24・U25） |
| [WS269](ws269/ws.md) | MG008 | VisionFive 2 の GPU（PowerVR BXE-4-32）の driver（2026-10-19 ユーザー） | incomplete（p001〜p003 cleared、p004 を P2、2026-10-19 q1330） | p004 PMU・CRG・別名・BVNC |
| [WS270](ws270/ws.md) | MG008 | VisionFive 2 で Keiland の desktop を動かし、OS を完全に動かす（2026-10-19 ユーザー） | incomplete（2026-10-19 から p001〜p006a を実行。実機の結果待ち） | — |
| [WS273](ws273/ws.md) | MG008 | Anbernic RG350（JZ4770、MIPS32）の実機への移植（2026-10-19 ユーザー） | incomplete（2026-10-19 22:22 ユーザーの指示で中断: p001・p002 cleared、p003 途中（main 7adce0d6d）。実機 1 は beta2.md U26） | 再開は ws273/phase003/phase.md |
| [WS275](ws275/ws.md) | MG008 | sun4u に /dev/graphics を実装し Xzed を動かす（2026-10-19 ユーザー） | incomplete（2026-10-19 停止、BUG-337。p001〜p005 cleared・main に merge 50bd10c5f） | BUG-337 |
| [WS277](ws277/ws.md) | MG008 | VisionFive 2 の DC8200 の display driver（zero copy の scanout）（2026-10-20 ユーザー） | incomplete（p001 の設計を P1、q1341） | p001 設計 → design-reviewer |
| [WS278](ws278/ws.md) | MG008 | Raspberry Pi 5 対応（config/ci/config-rpi5.mk、必要なら GPU・WiFi）（2026-10-21 ユーザー） | incomplete | p001 設計・p002 起動 image は main（6757ac6f2）、実機の確認待ち（plan/ws278/tests/rpi5-hw-test.md）、判断 D1〜D3 待ち |
| [WS279](ws279/ws.md) | MG008 | Raspberry Pi 5 の GPU（V3D 7.1）と display（HVS6）（2026-10-21 ユーザー） | incomplete | p001 design.md（約 47〜64 LW の Phase 表）、ユーザーの判断 D1〜D4 待ち |
| [WS280](ws280/ws.md) | MG006 | H.264 の software decoder（libh264dec、libmedia から dlopen、任意）（2026-10-21 ユーザー） | planning | 実装はユーザーの指示まで |
| [WS281](ws281/ws.md) | MG002 | libc の malloc を速くする（first fit・1 本の list・1 つの lock の作り直し）（2026-10-22 ユーザー） | planning | 着手は後日（ユーザー） |
| [WS282](ws282/ws.md) | MG006 | camera の driver の枠組み（/dev/camN）と USB Video Class の driver（CSI の rpicam は今回やらない） | incomplete | p001 の設計 版 3（/dev/camN ＋ UVC、review-v2 を反映、p002〜p011 で 32〜43 LW、p005 は WS197 p007a2 の実機の loopback の後、実装は後日）。Q1: D9 GPU の差分・D10 xHCI の拡張は WS282 の担当が入れてよい（T1 の回帰付き） |
| [WS283](ws283/ws.md) | MG006 | bcm2711 の Vulkan の compute（Pi 4・Pi 5） | incomplete | p001 の設計 版 2（review の H1〜H6・M1〜M10・L1〜L7 を反映、p002〜p014 で 77〜121 LW、7.1 を先に 4.2 は p013 で移植）。Q1: 7.1 先・NonWritable の印（p011）・CONFIG_DRIVER_BCM2711_COMPUTE（p005）を可 |
| [WS284](ws284/ws.md) | MG006 | YOLOX-Nano の推論（C と Vulkan compute、Apache-2.0） | incomplete | p001 の設計 版 2（review の F1〜F22 を反映、p002〜p008 で 35〜49 LW、重みは build の時に取得して変換）、D10（license の確認を出さない）・D11（試験の画像を tree に入れる）は 2026-10-23 に決定、実装は後日 |
| [WS285](ws285/ws.md) | MG006 | camera の object detection の demo の app（WS282〜WS284 の上） | planning | p001 の設計 |
| [WS287](ws287/ws.md) | MG008 | EHCI の full-speed・low-speed の device（split transaction、ZYBO の ChipIdea を含む） | planning | 着手は後日 |
| [WS290](ws290/ws.md) | MG006 | zedBSD の GHCI の driver を GHCI 2.0（amaterrace-gpu の docs/ghci.md）だけに書き直す。試験は host だけ | incomplete（p001 の設計は済み、ghci-questions.md の回答待ち） | p001 の設計（q1594、P1） |
| [WS291](ws291/ws.md) | MG006 | GPU の driver の描画の速さ（desktop 60 fps・重い 3D の model 50 fps）: AMD RDNA（P2）→ NVIDIA（P2、GPU を入れ替え）→ PowerVR（VF2、P3） | incomplete（p001・p002・p002a cleared） | p003 投入の経路（q1603、P2） |
| [WS292](ws292/ws.md) | MG006 | GHCI の session の保護（PROT）を zedBSD の driver に（WS290 の後） | planning | p001 設計（WS290 と G29 の回答の後） |
| [WS295](ws295/ws.md) | MG006 | Intel の xe の driver（Meteor Lake 以降の iGPU と discrete の Arc、GuC の submission、i915 の使える所は写して重複） | planning | p001 調査（担当の起動はユーザーの指示の時） |
| [WS296](ws296/ws.md) | MG001 | src/drivers（gpu/ を除く）の comment から開発の管理情報を消し、XXX の規約の当て直し（usb など 11 file） | planned | 18 LW、担当の起動はユーザーの指示の時 |
