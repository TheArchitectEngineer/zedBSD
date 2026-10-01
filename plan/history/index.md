<!-- awesome-plan project=zedbsd record=past-log -->

<!-- awesome-plan-current:start -->
Active Queue: なし
Last finished Queue: [q517](queue-q517.md)（ws104-p003 cleared）
<!-- awesome-plan-current:end -->

# Past Log

## 最新: 2026-10-01 q517

[q517](queue-q517.md): ws104-p003 **cleared**。libkeiland の network・network-link・audio を `zedbsd/*-zedbsd.c` に byte 同一で移動（移動前 hash と照合）。Makefile と host script 2 本の path を更新。共通 C の OS 専用 include は 0。公開 export は前後一致。amd64 build exit 0、自前 warning 0、host audio 14/14、Settings host build、Settings guest 回帰 8 本、boot 全て PASS。移動した 3 file の style-check 0。証拠: `plan/history/ws104/q517/`（hash、export 一覧、回帰 summary、login PNG）。旧 object は共有 build に残し、消していない。実機・Linux は未実施。

実装 `78da13872bbef7d62572fadab616ab78043d13f6`。自律実行承認による次の WS104 Phase に進む。GitHub へは未公開。

## 最新: 2026-10-01 q516

[q516](queue-q516.md): ws104-p002 **cleared**。公開版 21 の `keiland_audio_available()` を追加し、Settings の audiod socket 直接参照を除去。旧関数・Settings の `/run/` literal は 0。amd64 disk-image exit 0、自前 warning 0（raw filter の 1 行は OpenSSH の並列 stderr が分断された EC_KEY の非推奨 warning と前後から確認）。host audio 14/14、Settings host build、Settings guest 8 本の回帰、boot 全て PASS。新 API の socket 不在・通常 file・Unix socket を 0/0/1、版 21 と host で確認。style-check 0。clang-format 19.1.7 を新関数の範囲に使用し、全文規約が指定する定義の引数改行は手動で復元。任意の audiod 有り Sound 頁は未実施（socket 判定は旧関数と同値、positive host probe 済み。WS 全体の p008 で volume-p005 を実行）。証拠: `plan/history/ws104/q516/`。実機・Linux は未実施。

実装 `7e3ac1bc26ede65bd94e364eee2d23c84a6668d0`。自律実行承認による次の WS104 Phase に進む。GitHub へは未公開。

## 最新: 2026-10-01 q515（WS104 の公開ヘッダー分離）

[q515](queue-q515.md): ws104-p001 **cleared**。desktop 公開ヘッダー 24 file を `userland/desktop/keiland/` へ内容を保って移動し、承認済みの sysroot・参照 path の差分を適用した。amd64 build（自前 warning 0）、sysroot の 241 file の同一性と再生成、旧 path 0、host 4 本（textedit 34/34・audio 14/14・Files/Settings build）、boot test PASS。実装 `12d7efeea05917a0d12c50a93824a6b6dc990c59`（WIP）。外部 package の warning は 254 行。実機・他 platform・formatter は未実施。次の候補は ws104-p002 または p004、独立の ws105-p001。WS104 は incomplete。技術判断の許可と Queue 実行のユーザー指示を記録。GitHub へは未公開。

## 最新: 2026-10-01 q514（WS103 完了）

[q514](queue-q514.md): WS103 p007 cleared、**WS103 完了**。Keiland の compositor は GPU を Vulkan（libvulkan）だけで扱う: GPU の直の ioctl 0、GPU の fd なし、GPU の UAPI は
OS の backend の `gpu-zedbsd.c` だけ（V1）。client の buffer は dedicated の import で libvulkan が kernel の記述と照らし（V3）、fence は WSI が present ごとに新しく送り compositor は poll だけ。
規約の全文の見直し、回帰（C1・C2・C9・Notes・forge・fence・p054・boot test・5330）PASS、V4（QEMU の起動は遅くならず、5330 の C6 はばらつきの内）。q508〜q514 の 7 Queue、
p005〜p007 はユーザーの自走の指示。Linux・FreeBSD の backend は F-065。GitHub へは未公開。

## 2026-10-01 q513

[q513](queue-q513.md): WS103 p006 cleared。compositor は GPU の fd を持たず、fence は poll だけ、GPU の UAPI は `gpu-zedbsd.c` だけ（V1 の `v1-check.sh` PASS）。
fence・p054・forge・Notes・C1・C2・boot test・5330 PASS。GitHub へは未公開。

## 2026-09-30 q512

[q512](queue-q512.md): WS103 p005 cleared。WSI が Wayland の present ごとに新しい fence を送る（前は slot の fence を再利用して世代が進んだ）。QEMU の Venus で 600 個が全て世代 1、
時間は変わらず。p054・C1・C2・boot test・5330 PASS。GitHub へは未公開。

## 2026-09-30 q511

[q511](queue-q511.md): WS103 p004 cleared。compositor の client の buffer の取り込みを dedicated の import にし、`GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY` を削除。
OS の backend（`gpu-zedbsd.c`・`zwl-gpu.h`）で wire の値を確かめる。libvulkan は dedicated の無い import と allocation の capability の dedicated の import を拒む（実行の前に見つけた穴）。
偽の buffer の probe（`/bin/gpu-forge-test`）は断られ、compositor は動き続けた。C1・C2・boot test・5330 の smoke PASS。GitHub へは未公開。

## 2026-09-30 q510

[q510](queue-q510.md): WS103 p003 cleared。libvulkan に VK_KHR_get_memory_requirements2・VK_KHR_dedicated_allocation を足し、image の capability の dedicated の
import で kernel の記述と image を照らす（host の試験 18 件）。QEMU の C1・C2・boot test と 5330 の passthrough の smoke PASS。準備として別の checkout を指す
CMake の cache を作り直し（ユーザー許可）、BUG-126（toolchain の lock と libcxx の複写）を記録。GitHub へは未公開。

## 2026-09-30 q509

[q509](queue-q509.md): WS103 p002 uncleared。compositor の起動の問い合わせを VK_KHR_display へ移し、`--direct` と表示の claim・present・release の ioctl を削除。QEMU の Venus で
C1・C2・boot test PASS。5330 の passthrough の smoke は最初 FAIL（試験の image で `login=graphical` が重複し kernel が boot の parameter を拒んでいた。COM1 の mirror で特定、ユーザー指示の例外）。既定の boot の行で作り直して PASS（34 回、最大 48 ms）、Phase は cleared（追い）。GitHub へは未公開。

## 2026-09-30 q508

[q508](queue-q508.md): WS103（compositor を libvulkan だけに）の p001 cleared。GPU の直の ioctl の置き換えを設計した（起動の問い合わせ → VK_KHR_display、`--direct` の削除、
buffer の記述の照合 → libvulkan の dedicated の import、fence → present ごとに新しい fence と poll）。design-reviewer 2 回。同日、優先順位の書き直し（WS103 が最優先）と
Keiland の Linux・FreeBSD の構成（F-065）を記録。code の変更なし。GitHub へは未公開。

## 2026-09-30 q507

[q507](queue-q507.md): WS074 p099 cleared。固定 WPT の Acid2 を 90.56% から byte-identical な 100.00% へ改善。一般化した CSS・layout・object image/Adam7・paint order・border・compositing の修正を plain/ASan と focused regression で確認。Acid3 は p100 に残し、GitHub へは未公開。

## 最新: 2026-09-29〜30 の周期（subagent N=6〜9、worktree の branch を main が merge、Queue の外）

- **実機（Dell Latitude 5330、内蔵 LCD）**: firmware の画面の takeover の不具合（DPLL の読み出し）を直し、Keiland が起動（WS084）。1 fps → RPS（busy の時間の評価）で
  約 55 flip/s（ws075-p020）、FIFO の先行で 58/s（p019）、compiler の guard（分岐の中の texture の send を IF で飛ぶ）で 10 app の latency 132 → 32 ms（p021）。
  すりガラスは遅さの主因でないと計測（p023）、ユーザーの決定で分岐の中の ALU を飛ぶ実装へ。実機の image `build/demo-lcd8`（ロゴ無効）。
- **新しい app と部品**: Image Viewer（WS091、完了）、Text Editor と共有の file chooser（WS092、完了）、Files からの起動と Always Open With（WS093、完了）、
  Settings 23 頁・検索・生成の壁紙 5 枚（WS089、ブラッシュアップは後回し）、部品の library `libkeiui`（WS090 p001〜p003、文字の編集の touch は 1 本指で選択・2 本指で scroll）、
  デスクトップの icon（WS094 p001〜p003、Files `--desktop`）、IME（WS095 p001〜p004、Alt+Space で日本語、kanji → 漢字）。
- **Keiland**: 起動の短縮（p129〜p133）、BUG-112（xdg_wm_base）、Terminal の本体だけ直角（p134・p136）、Notes の全画面を解く配置（BUG-114）。
- **ブラウザ**（WS074）: JS に class・async・Promise・generator・Symbol・for-of・Map・Set（test262 21806 → 28381）、DOM に innerHTML・getComputedStyle・scroll・DOMException。
  Amazon の Uncaught は search 0・top 3（DOM の fetch・IntersectionObserver）。画素の差の原因（動的な script・百分率の高さ）を特定し p088〜p093 に計画。
- **bug**: BUG-030（usb-storage の flush の待ちで 5 秒の時間切れ → command ごとの timeout、TCG 75 回で 0）、BUG-051・102・104〜111、BUG-105（Logi Bolt の HID、host で確認）。
  新規 BUG-113（試験の誤り、resolved）・BUG-115・BUG-116。
- **基盤**: venus-win32 の取り込み、Kei-nightly.zip の CI、ls（GNU 形式）・sh（履歴・補完・`~`）の WS086・WS087（完了）。
- **ユーザーの判断**: master の「有効なユーザーの判断」に要約。IME とブラウザは一時的に人間が作業中。
- **未実施**: 実機での確認の大半（demo-lcd8 でユーザーが確認）。QEMU の証拠と実機の証拠は各 phase.md で分けている。

## 最新: 2026-09-30 q506

[q506](queue-q506.md): 9公開siteを固定Chrome User-Agentと現実的な2 viewportで画像・box・DOM比較し、Mediumのchallengeを記録。intrinsic auto margin、HTML presentational hint、table rowspan、非同期`postMessage`を修正した。WPT CSS2 reftestは44/100、Acid2は90.56%、Acid3は9/100。GitHubのES module実行はp098候補。GitHubへは未公開。

## 2026-09-30 q505

[q505](queue-q505.md): Mozilla日本語topと直接linkされた3 pageを固定Chrome User-Agentでcaptureし、Chromiumと比較。custom-propertyの`@supports`とbutton内の空白を修正し、topは70.71%/ink 62.52%から78.23%/ink 69.54%へ改善。GitHubへは未公開。

## 2026-09-30 q504

[q504](queue-q504.md): flex itemのcross axisで`box-sizing:border-box`を保ち、Amazon検索欄の高さを55pxから38pxへ修正。form controlの`text-indent`も実装し、検索文字の範囲はChromiumと同じ`x=435..574, y=22..36`。GitHubへは未公開。

## 2026-09-30 q503

[q503](queue-q503.md): Amazonの後続script向けWeb API、非同期fetch、MutationObserver、有界なsettleを実装。sign-in tooltipの幅とstacking orderも修正し、dynamic topは71.81%/ink 64.58%、約32秒・Uncaught 4。GitHubへは未公開。

## 2026-09-30 q502

[q502](queue-q502.md): DOMへ挿入された外部scriptを非同期取得して一度だけ実行し、load/errorを送る。Chromium fixture、DOM 20/20、ASan。dynamic topはAUI後続scriptへ進み、次のWeb API不足をp092へ分けた。GitHubへは未公開。

## 2026-09-30 q501

[q501](queue-q501.md): percentage heightを確定したcontaining blockで解決し、flex/gridへ高さを伝播、gridの`1fr`行を配分。topは84.06%/ink 80.56%、searchは76.32%/ink 34.48%へ改善。GitHubへは未公開。

## 2026-09-30 q500

[q500](queue-q500.md): Amazonの固定captureをbrowserとChromiumで同条件に描き、入力・実行環境・画像のhashと画素・ink指標をJSONへ残す手順を確立。top 82.96%/ink 79.01%、search 75.85%/ink 32.97%。GitHubへは未公開。

## 2026-09-29〜30 q499

[q499](queue-q499.md): Windows版QEMU/virglrendererのVenus表示とFiles起動停止、Noctの旧path混入を修正。表示と通常makeを確認。vendorのcommit/pushはユーザーreview待ち。GitHubへは未公開。

## 最新: 2026-09-29 q498

[q498](queue-q498.md): `toolchain` の Noct smoke 依存を削除。`make toolchain-cache` は既存のホスト用 LLVM を受け入れた。ゲスト用 clang のソースビルドを終え、`make -j16` と通常の `make` が image の検査まで PASS。QEMU・実機は未実施。GitHub へは未公開。

## 最新: 2026-09-29 q497

[q497](queue-q497.md): q496 の修正後、すでに `.nbc` に直した Remacs source への patch 再適用が失敗。patch rule を適用済みなら通すよう修正し、未適用の source と既適用の source の両方で確認。全体の `make` は未実施。GitHub へは未公開。

## 最新: 2026-09-29 q496

[q496](queue-q496.md): Remacs 用の host Noct の build を canonical host Noct に統合し、取得する Remacs source の `.nb` を現行 Noct の `.nbc` に合わせる patch を適用。Noct smoke と Remacs package target は PASS。全体の `make` は clang package の build の途中で中断し、image と boot は未確認。GitHub へは未公開。

## 最新: 2026-09-28 の 3 つの 5 時間の周期（subagent の運用、Queue の外）

- **Kei Operating System への改名**（WS078）: `userland/desktop/`（`/bin/wayland`・`/bin/terminal`・`/bin/files`・`/bin/browser`・`/bin/xserver`・`/sbin/sessiond`・`libkeiland`）、
  `keiland_*` の protocol、`KERN_` の識別子、画面の Kei、Desktop の menu の分類（BUG-080）、X11 と zedinst を `userland/retro/` へ。
- **Keiland**（WS035 p101〜p113）: 表示の引き継ぎ・lock・PRIMARY・network の menu と group・configure_bounds・衝突の dialog と Trash・Kei の起動画面（GOP 1920x1080）と spinner・greeter/lock/壁紙/files の Kei の見た目・terminal の選択・sessiond の GPU の待ち。
- **手書きの Notes と PDF Viewer**（WS079）: USB の pen（筆圧 4096・傾き・消しゴム）と multitouch の kernel、注入の device、`zwp_tablet_manager_v2`・`wl_touch`、
  右上のスワイプ・三回 click・二本指の flick、libpdf（writer・reader・描画・増分の更新・文字と shading の途中）、Notes（保存・自動保存・journal・他の PDF への書き込み）、PDF Viewer。
- **ブラウザ**（WS074 p016〜p058）: HTTP・HTTPS・画像・背景画像・非同期の読み込み・keep-alive・cache・DOM の入力・部品 `libbrowser.so`。
- **i915 と HDMI**（WS075）: stencil・multisample・後退の修正、`display=hdmi`（10 インチの LCD 1920x1280）、demo の image、BUG-091（spin lock）・BUG-092 の修正。
- **bug**（WS073 ほか）: BUG-075・079〜084・086〜092 を解決、BUG-093・094 を起票。
- 実機の USB の demo の image: `build/demo-hdmi/hdd-image.img`（main ca74780e、QEMU の boot test PASS、実機はユーザーが試験）。
- 最後の検証: main 909fb907 の desktop の image の build と boot test PASS（batch111）。

## q443〜q495（2026-09-26〜27）

サブエージェントの運用の再開（2026-09-27 11:52、rate limit の解除の後）: 作業用 N=3（WS071・WS073・WS035）と今回限りの salvage の片付け（WS001・WS056・WS049・WS048・WS068）。[q495](queue-q495.md): ws068-p024 cleared（VAO・buffer の map と copy・instancing・uniform buffer ほか、Venus で egl-p024 PASS）。

[サブエージェントの作業の保全（2026-09-27）](salvage-2026-09-27.md): 8 つのサブエージェントが rate limit で止まった。未 commit の作業を `salvage/<ws>` の 7 branch に保全（未検証、そのまま merge しない）。

2 回目の一括の merge（2026-09-27、サブエージェント）: WS036 p028（Noct の aarch64）・p029（package は LLVM の source を書き換えない）、WS065 completed、WS071 p002（`/bin/zdesktop-files` の骨格）、WS060・WS063 completed、WS064 completed、WS062 completed、WS070 p005（i915 実機の menu）。main の検証で libcxx の source の race（一時の対処の後に p029 で正式に）と stale な Noct の CMake の dir を片付けた。BUG-059〜064 を登録。

WS071 p001 と WS036（p026）・WS044（p002）の merge（2026-09-27、Queue の外、サブエージェント）: File Manager の設計（`zdesktop-files`）。LLVM の AArch64 zedbsd target（patch zedbsd7）、sysroot-arm64、rpi4 の FAT32 の boot partition、rtld の PF_X の重複の修正。main の toolchain を zedbsd7 に切り替えた。

WS049 の merge（2026-09-27、Queue の外、サブエージェント）: kernel の ACPI AML interpreter（host の試験で acpiexec と一致、fuzzing、SCI・GPE・EC の host の模擬、`/dev/acpi`、Global Lock、ECDT）。kernel への統合は HAL の差分の承認待ち。

[q494](queue-q494.md): ws068-p023 cleared。cube map（6 layer の image、cube の束縛と sampler、FBO の面への描画、面ごとの読み戻し、mipmap）。Venus の egl-p023 と回帰 PASS。

[q493](queue-q493.md): ws068-p022 cleared。libGLESv2 の framebuffer object と renderbuffer（texture の level 0 への描画、depth renderbuffer、FBO の readback、GPU の描いた texture の読み戻し）。FBO は GL の行の向きで描く（y を裏返さない vertex module、front face を逆）。Venus の egl-p022 と回帰 PASS。

WS045 の merge（2026-09-27、Queue の外、サブエージェント）: sed・awk・grep ほかの GNU 拡張（p001〜p009）。判断待ち 3 点は ws045/ws.md。

WS068 の GLSL の merge（2026-09-27、Queue の外、サブエージェント）: 自前の GLSL compiler（`userland/base/libglesv2/glsl/`、GLSL ES 1.00・1.10〜1.50・3.30・ES 3.00、SPIR-V は i915 が受ける形）と libGLESv2 の接続、uniform block。main で glsl-host PASS、Venus の x11-p004・p005・egl-p008・p019・p020・zdesktop-p070 PASS。実機は未実施。

[q492](queue-q492.md): ws069-p006 cleared、**WS069 completed**（zdesktop で X11 の app: 単体の rootless の zdesktop-x11server、窓は Vulkan、GLX と固定機能の GL 1.x、BUG-057 の修正）。規約の全文との照合で条件の分割・`--shm`・成功の return を直した。Venus と実機（2 run とも 6 検査）で確認。試験は plan/tools/x11/ へ。

WS048 の merge（2026-09-27、Queue の外、サブエージェント）: Pi 4 の FDT、brcmstb の PCIe、firmware の mailbox と VL805 の firmware、非 coherent の DMA（dma.c）。p004 は hal.h の差分の承認待ち。main で amd64 の image と rpi4 の vmunix の build（warning 0）、boot test（amd64 と QEMU raspi4b）PASS。実機は未実施。

WS070 の merge（2026-09-27、Queue の外、ユーザーの例外の許可でサブエージェントが実行）: System Menu の p001〜p004（protocol、zdesktop の描画と操作、libzdesktop、zdesktop-terminal の menu）。main の Venus で x11-p003〜p005・zdesktop-p068（題名の double click を x+80 に直した）・p070・menu-p003 PASS。p005（実機・規約）は残り。

[q491](queue-q491.md): ws069-p011 cleared。zdesktop-x11server の窓を top-level ごとの Vulkan の swapchain（MAILBOX）で表示し、wl_shm は fallback（`X11SERVER_SHM=1`）。libvulkan の present の worker と同じ接続を読むため、server の Wayland の dispatch を非 blocking に直した。Venus の x11-p003〜p005・zdesktop-p070、実機の run1 が PASS。Venus では Vulkan の道が約 3 倍遅い（F-021）。

[q490](queue-q490.md): ws069-p009 cleared。Xzed を ws069 の前（cc4433d4）へ戻し、amd64 の Wayland 版の build 規則と rootful の試験を消した。std VGA の QEMU で Xzed と zterm。

[q489](queue-q489.md): ws069-p010 cleared。BUG-057 の原因は kernel: `waitq_sleep` の「眠る前の wakeup」の EAGAIN を unix socket の送りの待ちが失敗として返し、blocking の send が失敗して libX11 が要求を打ち切っていた。2 つの待ちを直し、Venus の x11-p005 と実機の zgears 5000 frame で確認。ws069-p008 も追記で clear。

[q488](queue-q488.md): ws069-p008 uncleared。単体の rootless の X server `zdesktop-x11server`（組み込める形、出力の queue、絶対座標の一貫）が Venus と i915 実機で動く。x11-p005 の frame 数だけ BUG-057 で未達。BUG-057 は Venus でも再現し、kernel の unix socket で大きな send の送り手が起きない所まで特定。

[q487](queue-q487.md): ws035-p074 cleared。`Xzed.h` と `zed-gpu-buffer-v1-client-protocol.h` を非公開に（libX11・libwayland の下へ）、libzdesktop の役割を 2 つに。p073 の build の変数の衝突を修正。

[q486](queue-q486.md): ws035-p073 cleared。`userland/base/zwl` を `userland/base/zdesktop`（`/bin/zdesktop`）へ改名（C の識別子と log の `ZWL` は据え置き）。

[q485](queue-q485.md): ws069-p007 uncleared・canceled。BUG-057 は zgears が libGL の中の XGetGeometry の返事を待って止まる所まで特定。libX11 の `XPending` の断片の読み捨てを修正。2026-09-27 ユーザーの判断で X server を単体の zdesktop-x11server に作り直し、Xzed は元に戻す（master の 2026-09-27 の判断）。

[q484](queue-q484.md): ws068-p006 cleared。i915 実機で GLX の zgears（固定機能の GL）が正しく描き約 27 fps、App Home の X terminal、仮想デスクトップ（6 検査 PASS の run あり）。実行器に `vkResetDescriptorPool` と線幅・depth bias・stencil の命令、固定機能の shader を i915 の compiler の制約に、libEGL は pbuffer を最初の pass で clear。zgears の間欠の止まりは BUG-057、zwl の終わり方は BUG-056 に追記。

[q483](queue-q483.md): ws035-p072 cleared。窓の最小化（Wiseview の薄いタイルから戻す）、Wiseview のタイルをバーの絵へ drag・Ctrl+Alt+Shift+←/→ でデスクトップ間の移動。

[q482](queue-q482.md): ws035-p064 cleared。docked の題名を引くと窓が指に追従して縮み、140 px の手前で離すと戻り、越えると元の大きさで外れる。

[q481](queue-q481.md): ws035-p065 cleared。仮想デスクトップ 4 つ（窓はデスクトップごと、バーの絵・Ctrl+Alt+←/→・左右の端の swipe で横に移動）。

[q480](queue-q480.md): ws035-p071 cleared。App Home のページング（drag・ホイール・キー・ドット）、起動した icon から窓が育つ、左上への drag で閉じる、Tab。

[q479](queue-q479.md): ws035-p070 cleared。App Home に X terminal と Gears、`/usr/libexec/zdesktop-x11` が Xzed --rootless を必要なときに 1 つ起動。demo image にも。

[q478](queue-q478.md): ws069-p005 cleared。固定機能の GL 1.x（行列・光源・material・glBegin/glEnd・client 配列・display list、flat の provoking vertex）を libGL の中に。zgears の歯車が Venus の rootless の X の窓で回る（約 4.7 fps）。

[q477](queue-q477.md): ws069-p004 cleared。GLX の核（Xzed の QueryExtension と GLX の問い合わせ、libGL.so = GLES の変換層＋GLX＋libX11、pbuffer に描いて XzedPutImageRGB24）。Venus の zwl＋Xzed rootless で glxtest の窓が GL で描かれ、docked でも追従。

[q476](queue-q476.md): ws068-p010 cleared。EGL の pbuffer（offscreen の color と depth、frame をまたいで中身を保つ、swap で submit と解放）。Venus で 600 frame と 2048x1536 の readback が一致。pbuffer でも 1 frame 約 95 ms（F-021 に追記）。

[q475](queue-q475.md): ws068-p008 cleared。GLES 2.0 の描画の核（SPIR-V の shader binary、gl_Position の書き換え、buffer・texture・blend・depth・stencil・cull・glReadPixels、strip と fan の展開、dynamic uniform）。Venus の display 直接・zwl の窓・docked で画面と glReadPixels が一致。frame を重ねるのは p009。

[q474](queue-q474.md): ws069-p003 cleared。Xzed の rootless（X の top-level ごとに zwl の窓、top-level ごとの合成、enter で raise と focus、configure で resize、close で client を切る）。libX11 は閉じた接続で exit。Venus で X の zterm が Wiseman の窓になり、ドッキングで resize、× で終了。

[q473](queue-q473.md): ws069-p002 cleared。Xzed の Wayland backend（rootful: X の screen を zwl の 1 つの窓に、wl_shm、pointer・keyboard、libtruetype の glyph）。Venus の zwl で X の zterm に打った command の出力が見える。

[q472](queue-q472.md): ws069-p001 cleared。zwl で X11 の app を動かす設計（新 WS069）: 既存の Xzed に Wayland backend（rootful → rootless）、GLX は EGL/GLES の上で DRI3/Present に当たる buffer の受け渡し。

[q471](queue-q471.md): ws068-p002 cleared。EGL 1.5 の核（Wayland・display 直接・surfaceless、config、window surface と swapchain、context、swap）、libwayland-egl、clear だけの libGLESv2、Khronos の header と出典、egltest。Venus で zwl の窓と全画面の clear。

[q470](queue-q470.md): ws068-p001 cleared。EGL/GLES の設計（plan/ws068/design.md）: library の構成、EGL（Wayland・display 直接）、GLES の方式の比較と推奨（B: 自前の変換層＋glslang）。方式の選択はユーザーの判断待ち（p003 の前提）。

[q469](queue-q469.md): ws035-p069 cleared。App Home の PoC（desktop の層が右下へずれて明るい Home が現れる、launcher と左上の角からの drag、6 列の icon、打つと検索、起動、Esc と角で閉じる）。Venus と i915 実機で terminal と mview を起動。実機用の demo image の build script。

[q468](queue-q468.md): ws035-p068 cleared。zdesktop-terminal（zterm の VT100 を移し拡張、libtruetype の等幅 font の atlas、Vulkan の cell 描画、US 配列、key repeat、forkpty、resize）。Venus と i915 実機で shell が動く。Venus の image に git 外の font と壁紙を入れる build script。i915 の 1 回の画面停止は BUG-056（tracking）。

[q467](queue-q467.md): ws031-p050 cleared。i915 の実行器の session の close で、application が破棄しなかった Vulkan の object（command pool と buffer、descriptor pool と set、pipeline と kernel、fence、allocation、その他）を解放。descriptor pool の破棄でその set も解放。host の fixture に残したまま close する試験。実機の zdesktop の scenario に描画中に殺す Vulkan の窓と、mview を × で閉じる段を足し、mview は `reason=closed` で終わり zwl は合成を続けた。

[q466](queue-q466.md): ws035-p067 cleared。5330 の i915（VFIO）で zdesktop に mview（Vulkan の client）の窓を合成し、ドッキング（1920x1042 で描き直し）と Wiseview を capture で確認。i915 の実行器の object 表と blob の対応を session ごとの key に（wire の id が process の間で衝突していた）、allocation の import、libvulkan は allocation の共有の無い node で画像の import へ。F-022 を promoted。

[q465](queue-q465.md): ws035-p066 cleared。5330（10.0.30.3）の i915 を VFIO で渡した実機の GPU で Wiseman Mode（壁紙・すりガラス・文字・浮いたタイトルバー・ドッキング・Wiseview）を capture で確認（wl_shm の窓）。zwl の shader を native compiler に合わせ、fence の fd 無し・descriptor set の再利用・triangle list、libvulkan は画像共有だけの node で外部 memory、i915 の GGTT の窓を 64 MiB・1 GiB に。GPU の client の窓は F-022。

[q464](queue-q464.md): ws035-p063 cleared。Wiseview（ユーザーの設計 [wiseman-design.md](../ws035/wiseman-design.md)、Wiseman = WM、全画面 = ゲームモード、窓 = Wiseman Mode）: 下端からの drag で開き、窓がタイルのグリッドへ、選択・閉じる・背景で閉じる。

[q463](queue-q463.md): ws035-p062 cleared。最大化 = タイトルバーのシステムバーへのドッキング（ダブルクリック・上への drag・button）、解除（バーの題名のダブルクリック・⧉・下への pull で引きずり出す）、220 ms の遷移、仮想デスクトップのハリボテ。shell.c を glass.c から分けた。

[q462](queue-q462.md): ws035-p061 cleared。ユーザーの絵を抽象化した壁紙（`--wallpaper`、PPM、git 外）と、すりガラスで透ける窓（`--window-opacity`）。10 %・60 % の画面。

[q461](queue-q461.md): ws035-p060 cleared。mview に `--windowed`・`--size`。`zwl --glass` の窓で 3D model を Vulkan で描き、drag で回転、最大化で描き直し。spin は窓 8.80 fps・全画面 8.59 fps（Lavapipe 律速）。

[q460](queue-q460.md): ws035-p059 cleared。`zwl --glass`: 本体から離れて浮いたすりガラスのタイトルバー（題名・3 つの button、drag・最大化・閉じる・hover）、角丸と影、上部のバー（ハリボテ）、CPU で描く壁紙と縮小ぼかし（壁紙だけを透かす）、Inter（Google Fonts、git 外）を libtruetype で atlas に。p052〜p054 の回帰 PASS。

[q459](queue-q459.md): ws035-p054 uncleared。`zed_gpu_buffer_v1` version 2 の `set_acquire_fence`（1 commit に 4 つまで）、zwl は fence を poll して終わった commit だけを採る、WSI は fence を付けて先に commit。QEMU で保留の間も他の窓と合成が進み、hold 6000 ms に対し 5991〜6034 ms 待った。`vkQueuePresentKHR` は変更前と同じ 31〜35 ms（元から完了を待たない）で、受け入れ 3 はユーザーの判断で読み替えて cleared（2026-09-26）。libc の `setvbuf(…, NULL, _IOLBF, 0)` を直した（BUG-055）。

[q443](queue-q443.md): ws063-p001 cleared。journal の大きさを mkfs で記録し、mount で `.ufs-journal`（extent）を再利用・再確保・作成する。
[q444](queue-q444.md): ws061-p008 cleared。libc の同期の system call を減らし、make（直列）12.8〜13.1 秒（host `-j1` 15.2 秒）。
[q445](queue-q445.md): ws061-p009 uncleared（着手の前に中断: ユーザーの優先の変更）。
[q446](queue-q446.md): ws061-p010 cleared。system call の入口を `syscall`/`sysret` に（入口の費用 4.65 → 1.54%）、libc の lock の adaptive spin。
[q447](queue-q447.md): ws061-p009 cleared。loader の symbol の探索（hash を 1 度に、予約の名前、再配置の segment の検査）、`cc`・`ld` を link に、`spin_trylock` の TTAS。`cc t.c -o t` 75〜85 ms（host 83〜85 ms）、configure 9.1〜9.4 秒（host 10.7 秒）。
[q448](queue-q448.md): ws064-p001 cleared。base の make の `-j`（歩きの更新、job の表、GNU 互換の jobserver、`.WAIT`・`.NOTPARALLEL`）。差分試験 100/100、guest の expat `make -j4` 7.3〜7.8 秒（host 5.06 秒）。
[q449](queue-q449.md): ws064-p002 cleared。kernel の mutex の速い道、`vfork`（system call と libc）と vfork の posix_spawn、make・sh の posix_spawn、fork・destroy の VM の大域の lock の保持の短縮、逆写像の O(1)。`make -j4` 4.94〜5.00 秒（host 5.14〜5.18 秒）、configure 8.2〜8.9 秒、直列 11.3 秒。
[q450](queue-q450.md): ws064-p004 cleared。sh の pipeline・command substitution・`unset` の subshell を fork せずに（posix_spawn、shell の中の `echo`・`printf`）。fork: make 1417 → 214、configure 966 → 423。configure 7.1 秒、直列 10.7 秒、`make -j4` 4.2〜4.4 秒。
[q451](queue-q451.md): ws065-p001 cleared。sh に POSIX が未規定とする bash の構文（`$'...'`、`[[ ]]`、`function`、`(( ))`、`for (( ))`、`\|&`、`<<<`、`>& file`、`<( )`）。BUG-054 を記録。
[q452](queue-q452.md): ws065-p002 cleared。sh に bash の展開（`${v:o:l}`、`${v/p/r}` の類、`${v^^}`・`${v,,}` の類、`${!v}`）。bash の参照 21/21、dash との差は dash に無い `${x//}` の 1 件だけ増えた。
[q453](queue-q453.md): ws065-p003 cleared。sh に bash の builtin（`source`、`let`、`test ==`、`declare`・`typeset`、`printf -v`・`%q`、`builtin`、`pushd`・`popd`・`dirs`）。静的 link の効果を測り F-020 に記録（expat で 0〜3%）。
[q454](queue-q454.md): ws067-p001 cleared。`/dev/fd` を呼んだ process の descriptor に（一覧・lookup・stat）、diff が pipe を中身で比べる。BUG-054 resolved（QEMU）。
[q455](queue-q455.md): ws067-p002 cleared（規約）。WS067 completed。
[q456](queue-q456.md): ws062-p003 cleared。amd64 の既定の image を native（ESP・UFS root・swap partition、各 1 GiB の 2 GiB）に、CI は gzip で公開。CI の失敗（toolchain の smoke の消失）と clang の libc.so の依存の退行を直した。
[q457](queue-q457.md): ws035-p052 cleared。zdesktop（zwl）の 2 つのモード: Vulkan の合成（ウィンドウモード）と全画面の直接 scanout、切替。libvulkan の OPAQUE_FD の import を画像の fd に対応。
[q458](queue-q458.md): ws035-p053 cleared。`wl_shm`（libwayland の client にも追加）、cursor（矢印・client・非表示）、frame の予定。

## 前: q439〜q442（2026-09-26）

[q439](queue-q439.md): ws061-p006 cleared。UFS を write cached の既定に、`mount -o writethru` で write-through（2026-09-26 ユーザー指示）。configure（`/root`）17〜20 → 12.3 秒。journal の無い volume は電源断で漏れが残ると確かめた。
[q440](queue-q440.md): ws060-p002 cleared。batch の redo journal（v3）の設計（2026-09-26 ユーザー指示「journal を既定に、無い image は mount の時に作る、`nojournal`」の前提）。
[q441](queue-q441.md): ws060-p003 cleared。v3 の実装（pin した metadata、1 秒ごとの commit、2 slot の交互、`/.zedjournal`、superblock の locator、replay）、journal の既定化と `nojournal`。200 の作成 20.6 → 0.36 秒、強制終了の試験 4 時点と root で UFS OK、configure 13.0 秒。規約の指摘約 100 件は WS063-p002。
[q442](queue-q442.md): ws061-p007 cleared。readdir の読み（1 項目ごとに 8 KiB を 2 回 → sector だけ 1 回）と fault の待ちの全 page の走査を直し、configure（`/root`、journal）11.5 秒、host 10.9〜11.2 秒。

## 前: q438（2026-09-25）

[q438](queue-q438.md): ws061-p005 cleared。process の生成と終了の固定費用。新しい thread を作った CPU に置き idle の CPU が盗む、解放した page の LIFO の stack、HAL の `rep stos`/`movs`、`mutex_owned`・`space_op_enter` の lock の除去、destroy の unmap の省略に加え、thread の移動で表に出た race（current task の 2 段の読み、shootdown の送り手、preempt count・exit・switch の CPU の読み、移動の完了と wakeup）と、全 poll を起こす descriptor の通知、reaper と親の VM の mutex の奪い合い、readahead の全 worker の起床、生まれたばかりの子の盗みを直した。page の stack が逼迫時に DMA の確保を失敗させる退行も直した。`true` 3.4 → 1.1 ms、configure（`/root`）35 → 17〜20 秒（host 11.2 秒）、tmpfs 13 秒、make（直列）20〜25 秒（host `-j1` 15.2 秒）。並列の make（F-016）と UFS の delayed write（F-015）はユーザーの判断。amd64 の `defs.h` の未使用の `CLOCK_HZ 100` を削除（tick は 1000 Hz）。QEMU だけ。

## 前: q437（2026-09-25）

[q437](queue-q437.md): ws062-p002 cleared。NVMe の起動の harness、4 GiB の root・swap の image、BUG-053（RAM を超える anonymous memory）の修正（450・900 MiB）、`kern_free` の O(1)。configure（`/root`）の tmpfs との差は UFS の write-through と flush（F-015）。

## 前: q436（2026-09-25）

[q436](queue-q436.md): ws062-p001 cleared。amd64 の native の layout（GPT: ESP に `BOOTX64.EFI`・`vmunix`・`zedbsd.cfg`、UFS の root partition、swap partition、`PARTLABEL=` で指す）を `zedimage-host` と `ZEDBSD_VARIANT=native` で作れるようにし、検査器を足した。QEMU で起動し、root は `/dev/sda2` の UFS（rw）、swap は partition、SSH の harness が通る。kernel と loader は変えていない。

## 前: q435（2026-09-25）

[q435](queue-q435.md): ws061-p003 uncleared。fault-around（object が既に持つ隣の page を 16 page の窓で map）と、amd64 の TLB の invalidation の縮小（map の後の shootdown を削除、小さい範囲は `invlpg`）。file fault 3.2 → 1.4 µs、`clang --version` の fault 5862 → 1891（目標 1/8 は未達）、`cc t.c -o t` 0.21〜0.24 秒、configure 31 秒（tmpfs）・55 秒（overlay）。configure の時間は子の system 23 秒が主（fault は 1〜2 秒）。BUG-051（sshd の子の SIGSEGV 1 回、未再現）、BUG-052（tmpfs 32 MiB）を記録。ユーザー指示で disk の layout を ESP の vmunix・UFS の root partition・swap partition にする WS062 を立てた。回帰 make 91/91・sh 1388/1425・COW・SMP。

## 前: q434（2026-09-25）

[q434](queue-q434.md): ws046-p014 uncleared（kernel の変更は完了）。private の file の mapping（ld.so が map する libLLVM・libclang-cpp の text など）で page cache の page を read-only で map し、書き込みで COW（p011 の差分の当て直し。p011 の失敗の原因の object の寿命は p012 で直っていた）。`mprotect` で書き込み可能にしたとき commit を取らない穴を直した。file fault 8.4 → 3.2 µs、`ffault` 10 → 3 µs/page、`cc t.c -o t` 0.35 → 0.25 秒、configure（tmpfs）35 → 30〜33 秒。expat の make status 0（8 GiB・512 MiB）、runtests 4932/4932。`make check` は base に bash が無く status 2（判断待ち）。回帰 boot・make 91/91（8 GiB・512 MiB）・sh 1388/1425・COW・SMP、3 platform の build。

## 前: q433（2026-09-25）

[q433](queue-q433.md): ws061-p002 uncleared（anon fault の目標は達成、file の fault 8.4 µs と `true` 4〜5 ms は目標に届かず、残りは p003・ws046-p014）。VM object の registry の inode hash（全 object の線形探索が configure の kernel 標本の首位だった）、VM metadata の slab の O(1) の解放、kcrt の word 単位の複写、amd64 HAL の page table の entry 数と direct map の変換の短縮、itimer の tick の 1 回走査。configure 87 → 59〜64 秒（overlay）、59 → 35〜38 秒（tmpfs）、`clang --version` 160 → 80〜100 ms。ユーザーが HAL の規則を「実装は承認不要、API だけ承認」に変えた。BUG-049 は計測の image の誤りで不具合ではない、BUG-050（strerror の穴）を記録。p004（overlay の同期書き）を立てた。回帰 boot・make 91/91・sh 1388/1425・SMP・COW・itimer。

## 前: q432（2026-09-25）

[q432](queue-q432.md): ws061-p001 cleared（fg011）。host の参照: expat の configure 11 秒、`cc t.c -o t` 83〜92 ms、link 48〜57 ms（guest は 91 秒・636 ms・361 ms、7〜8 倍）。guest の時間は page fault に律速（約 30 µs/fault: `clang --version` 5797 fault = 188 ms、`ld.lld` 4547、`true` 306 = 11 ms）。loader の仕事は libLLVM/libclang-cpp の RELATIVE 23 万・relocation の表 5.6 MB。configure 中の kernel 標本: fault 22%、exit の page table 解体 13%、USB の同期 flush 20%。次: p002（fault の固定費用と exit の解体）、p003（fault-around）、ws046-p014（直接 map）。

## 前: q431（2026-09-25）

[q431](queue-q431.md): ws060-p001（journal の commit の費用の実測と設計、BUG-040）を始めた直後にユーザーが再優先付け（「expat の configure と compile を Linux と同等の水準に」を直近の目標に。commit の裏打ちは物理 + swap で決定）したので撤回、uncleared。WS057 は完了。

## 前: q430（2026-09-25）

[q430](queue-q430.md): ws046-p012 cleared（BUG-033 の残り）。VM object cache の追い出しを LRU に、最後の unmap で mapping の object を cache に残す（次の process が同じ file を map すると page が残っている）、dirty も busy も無い object の最後の unmap で全 page の同期 walk を省く。`cc t.o -o t` 0.51〜0.68 → 0.36〜0.38 秒、`libLLVM.so` の file の fault 25 → 15.3 µs/page、configure 91 秒。`KERN_SYSTEM_DROP_CACHES` を `/dev/system` に追加。BUG-045（stress の baseline の間欠）の原因は非同期の後始末との競走と特定（probe: `waitpid` の直後 file +2・vmspace +1、1 秒で戻る）、試験を直して resolved。回帰 boot・sh 1389/1425（1 件改善）・make 91/91（8 GiB・512 MiB）・COW・SMP 6/6。p011 の当て直しは p014 へ。

## 前: q429（2026-09-25）

[q429](queue-q429.md): ws059-p001 cleared、WS059 完了。BUG-047: disk の無い mount（overlay の root・tmpfs・devfs）の `st_dev` が全て 0 だったのを、`mount_device_number()`（disk は disk の番号、bind は source、それ以外は `0x80000000 | (1 + slot)`）で直し、generic・tmpfs・devfs・overlay の getattr が使う。全 mount で `st_dev` が異なり、coreutils の `df` が全 mount を大きさ付きで出す。回帰 boot PASS・sh 1388/1425（同じ集合）・make 91/91・SMP 0。

## 前: q428（2026-09-25）

[q428](queue-q428.md): ws058-p002 cleared（design policy 10）。buffer cache 物理/16 → /8（hash 2^16）、page cache の target 物理/4 → /2、VM object cache 32 → 256、snapshot 192 → 1024、I/O pool 4 → 64 MiB、file 表 192 → 2048、inode cache 512/256 → 2048/512、overlay の inode の表 256 → 4096。最初の大きな値は 2 つの退行を起こし bisect で原因を特定: overlay の固定の表は VFS の inode cache 以上でなければならず、cache の VM object は自分の file handle を開くので file の表を消費する（F-013: 動的確保と hash）。回帰: boot PASS、8 GiB と 512 MiB で make 91/91、sh 1388/1425（同じ集合）、SMP 0。expat の configure 96 → 89 秒、`cc t.c -o t` 1.0 → 0.63 秒。WS058 完了。

## 前: q427（2026-09-25）

[q427](queue-q427.md): ws058-p001 cleared（design policy 10）。8 GiB の guest で実測: buffer cache は物理/16（512 MiB）、page cache の target は物理/4（2 GiB）で比例。固定で小さいのは VM object cache 32、system 全体の open file 表 192、inode cache 512、I/O pool 4 MiB、snapshot 192。p002 で buffer 物理/8、page cache 物理/2、object 1024、file 8192、inode 8192、I/O pool 64 MiB に。

## 前: q426（2026-09-25）

[q426](queue-q426.md): ws057-p002 cleared。BUG-048（`SYSCALL_PAGE_MASK` が 32 bit で mmap などの長さが 4 GiB で切り捨て）を kernel で直し（mask を `uintptr_t` に、vmspace の丸め 5 箇所も）、`MAP_NORESERVE` を定義して受け付けて無視（reserve は無制限、commit は厳密）。probe: 128 GiB の `PROT_NONE` の reserve が commit を増やさず成功、5 GiB の RW の mapping の offset 4 GiB + 4 KiB を触れる、9 GiB は ENOMEM、`mprotect` の長さ超過は失敗。回帰 boot PASS・sh 1388/1425（同じ集合）・make 91/91・SMP 0。残りは p003（commit の裏打ちを swap だけにするかの判断）。

## 前: q425（2026-09-25）

[q425](queue-q425.md): ws057-p001 cleared（design policy 10 の調査）。commit の会計は `vm_commit_reserve/release` にあり、`PROT_NONE` の mapping は課金せず（reserve）、accessible にした時・anonymous private・書ける file private・stack・brk・shared object・fork で課金し、上限超は ENOMEM（over commit しない）。上限は起動時の空き物理 + swap（「swap だけ」にするかはユーザーの判断待ち）。probe で **BUG-048** を発見: `SYSCALL_PAGE_MASK` が 32 bit で `mmap`・`munmap`・`mprotect` の長さが 4 GiB で切り捨てられ、9 GiB の mmap が 1 GiB になる。`MAP_NORESERVE` は未定義で EOPNOTSUPP。修正は p002。

## 前: q424（2026-09-25）

[q424](queue-q424.md): ws046-p013 cleared。libc に glibc 形式の `<mntent.h>`（`setmntent`/`getmntent`/`endmntent`/`hasmntopt`、kernel の `KERN_SYSTEM_GET_MOUNTS` を mtab の形の stream に）を足し、coreutils 9.12 の cross build が gnulib の要求で次々に止まった点を libc で埋めた: `<elf.h>`（System V ABI）と `<link.h>` の `ElfW`、`<stdio_ext.h>`（`__fpending` など）、`<utime.h>`、`fseeko`/`ftello` の関数化、`posix_spawn_file_actions_add{chdir,fchdir}_np`、`<stdio.h>` の `_STDIO_H`、errno の `EPFNOSUPPORT` の独立と POSIX の残り 13 個、`statvfs.f_basetype`（kernel が type 名を入れる）、gnulib の `getlocalename_l` の zedBSD 分岐（patch）。configure・make・install が通り 102 program、guest で `df`・`stat -f`・`ls`・`sort` などが動く。回帰 boot PASS・make 91/91・sh 1388/1425（同じ集合）、`POSIX-R2-REMAINING` 01-12 PASS。新しい bug: BUG-047（disk の無い mount の `st_dev` が 0 で GNU df が root と tmpfs を出さない）。ユーザーの指示（design policy 10: メモリ 4 GB・swap 16 GB の前提、VM の reserve/commit）を記録し WS057・WS058 を planning で作った。

## 前: q423（2026-09-25）

[q423](queue-q423.md): ws056-p001 uncleared（残りは 1 点）。BUG-034（`RTSIG_MAX` 33）、BUG-035（atomic の試験を 8 byte に）、BUG-037（pax の `x`・`g`・`L`・`K`。GNU tar の pax・gnu の archive を guest で展開して host と一致）を直した。`POSIX-R2` の試験が進んで見つけた BUG-042 は真因が kernel の `thread_create(2)` の signal mask の非継承で、kernel と libc（worker・reaper を全 block、wake signal 63 を `__libc_init` で block）で直した。試験の前提の誤り BUG-043・044 も直し `POSIX-R2-REMAINING.ELF` は 01-12 PASS。`POSIX-R2.ELF` は console で timer の試験が EINTR（BUG-046、未特定）。BUG-045（stress の baseline、間欠）を記録。

## 前: q422（2026-09-25）

[q422](queue-q422.md): ws046-p009 cleared。承認された HAL の差分（`amd64_percpu_current()` を `rdmsr` から `%gs:0` の load に）を適用: syscall 1340 → 450 ns、fork+exec 9.1 → 6.5 ms、expat の configure 129 → 96 秒。回帰は amd64 の boot、sh 1388/1425（同じ集合）、make 91/91、対話 41/41、SMP stress 0。BUG-033 の残りは p012 へ。

## 前: q421（2026-09-25）

[q421](queue-q421.md): ws046-p008 uncleared。coreutils を host で zedBSD 向けに cross build: libc の header の誤り 2 つ（`<locale.h>` の include の循環、`WINT_MIN`・`WINT_MAX` が `wint_t` と合わない）を直して進んだが、libc に mount の一覧の API が無く `mountlist.c` で止まった（p013）。

## 前: q420（2026-09-25）

[q420](queue-q420.md): ws046-p011 uncleared。private の file の mapping で page cache の page を map する実装は、compile と link を速くした（1.0 → 0.61 秒）が、expat の configure を 128 → 270 秒と遅くし make を失敗させたので戻した。object の寿命の費用と見て、p012 で設計を直す。

## 前: q419（2026-09-25）

[q419](queue-q419.md): ws046-p010 cleared。`MAP_PRIVATE` の file の region（共有 library）も `MAP_SHARED`・exec の snapshot と同じく object の page を read-only で map し、書き込みで COW にする設計。実装は p011。

## 前: q418（2026-09-25）

[q418](queue-q418.md): ws046-p009 uncleared。guest の configure の 1 check 約 1.3 秒は compile と link（1.2 秒）で、kernel の file の page の fault が主。
`find_page()` が region の全 page の list をたどっていた（大きな library の fault が 2 乗）のを index に、reclaim の queue の探索を双方向の list にした: file の fault 145 → 25 µs/page、expat の configure 204〜252 → 129 秒。
残りの最大は HAL の `rdmsr`（kernel の標本の約 24%、差分を plan に置いた、**承認待ち**）と file の fault の複写（p010、設計から）。回帰は前と同じ。

## 直近の 30 Queue

| Queue | 内容 | Status |
| --- | --- | --- |
| [q421](queue-q421.md) | coreutils の cross build（ws046-p008 の残り） | finished（2026-09-25。uncleared、p013 へ） |
| [q420](queue-q420.md) | private の file の mapping で page cache（ws046-p011） | finished（2026-09-25。uncleared、戻した） |
| [q419](queue-q419.md) | file の fault の複写の設計（ws046-p010） | finished（2026-09-25。cleared） |
| [q418](queue-q418.md) | guest の configure の遅さ（ws046-p009） | finished（2026-09-25。uncleared、原因 2 つを直した） |
| [q417](queue-q417.md) | guest で coreutils（ws046-p008 の再開） | finished（2026-09-25。uncleared、範囲の変更） |
| [q416](queue-q416.md) | WS054 の回帰と規約（ws054-p004） | finished（2026-09-25。cleared。WS054 completed） |
| [q415](queue-q415.md) | UFS の directory を複数 block に、journal（ws054-p003） | finished（2026-09-25。cleared） |
| [q414](queue-q414.md) | UFS の directory を複数 block に、journal 無し（ws054-p002） | finished（2026-09-25。cleared） |
| [q413](queue-q413.md) | UFS の複数 block の directory の設計（ws054-p001） | finished（2026-09-24。cleared） |
| [q412](queue-q412.md) | 実 package を guest で最後まで（ws046-p008） | finished（2026-09-24。uncleared、coreutils は WS054 を待つ） |
| [q411](queue-q411.md) | guest の clang の遅さ（ws046-p007 の再開） | finished（2026-09-24。uncleared、主因を直した） |
| [q410](queue-q410.md) | WS053 の規約と最後の回帰（ws053-p005） | finished（2026-09-24。cleared。WS053 completed） |
| [q409](queue-q409.md) | i386 の vmunix に LTO（ws053-p004） | finished（2026-09-24。cleared） |
| [q408](queue-q408.md) | arm64 の vmunix に LTO（ws053-p003） | finished（2026-09-24。cleared） |
| [q407](queue-q407.md) | amd64 に LTO を既定で適用（ws053-p002） | finished（2026-09-24。cleared） |
| [q406](queue-q406.md) | vmunix の LTO の調査と設計（ws053-p001） | finished（2026-09-24。cleared） |
| [q405](queue-q405.md) | guest の clang の遅さ（ws046-p007） | finished（2026-09-24。uncleared、WS053 を優先して中断） |
| [q404](queue-q404.md) | 実 package を guest で build（ws046-p004） | finished（2026-09-24。cleared、BUG-033 を p007 に引き継ぎ） |
| [q403](queue-q403.md) | make の GNU の機能（ws046-p003） | finished（2026-09-24。cleared） |
| [q402](queue-q402.md) | kernel の mkdir の `.`（ws046-p006、BUG-032） | finished（2026-09-24。cleared） |
| [q401](queue-q401.md) | POSIX make の核（ws046-p002） | finished（2026-09-24。cleared） |
| [q400](queue-q400.md) | make の調査と設計（ws046-p001） | finished（2026-09-24。cleared） |
| [q399](queue-q399.md) | WS042 の最後の回帰（ws042-p012） | finished（2026-09-24。cleared。WS042 completed） |
| [q398](queue-q398.md) | ls の規約（ws042-p011） | finished（2026-09-24。cleared） |
| [q397](queue-q397.md) | sh の規約（ws042-p010） | finished（2026-09-24。cleared） |
| [q396](queue-q396.md) | libedit の規約（ws042-p009） | finished（2026-09-24。cleared） |
| [q395](queue-q395.md) | vi mode の行編集（ws042-p008） | finished（2026-09-24。cleared） |
| [q394](queue-q394.md) | 対話の回帰（ws042-p007） | finished（2026-09-24。cleared） |
| [q393](queue-q393.md) | guest の sh の差分試験（ws042-p006） | finished（2026-09-24。cleared） |
| [q392](queue-q392.md) | configure を guest で（ws042-p005） | finished（2026-09-24。cleared） |

それより前: [全 Queue の一覧](index-all.md)。
