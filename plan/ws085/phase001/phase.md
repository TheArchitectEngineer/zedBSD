<!-- awesome-plan project=zedbsd record=ws085-p001 -->

# ws085-p001: Windows Venusのrenderer契約と表示経路

Status: in-progress
Disposition: normal
Parent: [WS085](../ws.md)
Queue: [q499](../../queue.md) / q499-i01
Approval: ユーザー「Windowsで動くように修正してみてください。」および`vendor/`へのfork導入・直接変更許可（2026-09-29）。vendorのcommit/pushはユーザーがレビュー後に行う。

## 範囲と受け入れ

WINQ-EMU Alpha10の隔離コピーに、固定のzedBSD strict queue/quiescence契約を移植したrenderer DLLを使う。Windows hostは元のインストールを上書きしない。zedBSDのlibvulkanが既存の1.3.269 codecで確認した1.4.343 rendererのコマンド集合を受け、Windowsのblob scanoutが表示できない場合だけ既存のcopy表示を使う。ユーザー指定の`10.0.10.2:zedBSD-rpi4/`からデスクトップのフォントを取り込み、ゲストの`vkdemo`のGPU readbackとWindowsのSDL実画面のデスクトップ・メニューを確認する。

追加範囲（同日のユーザー指示）: `vendor/winq-emu-qemu`と`vendor/winq-emu-virglrenderer`のforkソースを直接修正し、Windows hostのblob scanoutに共有画像のmapped memoryを接続する。Linux上の`~/linux-pc98/scripts/build-qemu-win64.sh`を参考に、WINQのWindowsビルドを再現可能なスクリプトにする。copy fallbackとの速度差と表示を実機で確認する。

## 現在の証拠

- 元のWindows renderer: capset 160B。guestの物理Vulkanデバイスは0。
- host fork source `cmspam/winq-emu-virglrenderer` alpha10 `e80354b8`。1.1.0のstrict/quiesce patchを1.3.0 Windows thread serverへ移植し、UCRT64でDLL build PASS。隔離場所は `C:\Work\winq-zedbsd`。
- 改造版 capsetは168B、magic `0x5a424453`、flags7。hostがXML 1.3.269を提示する実験ではゲストの145 opcode番号が一致、`vkdemo --offscreen` のreadback hashを取得し、Waylandの`ZWL OUTPUT open`と合成frameが進んだ。
- blob scanoutはSDL実画面が黒い。hostのXML偽装を外し、guestが1.4.343を明示認識して既存copy fallbackを選ぶ実装を加えた。`make`とimage検査はPASS。WindowsのSDLでは起動ロゴから1280×800のデスクトップ背景へ切り替わった。
- 実XML 1.4.343のhostで`/bin/vkdemo --offscreen --time-ms=1000`はIntel Arc 130Vを列挙し、320×240のRGB SHA256 `6a446b50d2a1b40c743a77347615576bc0cb65e9eb55ef2209e7ed8a0f03e9ac`を返してDONE。Waylandの`ZWL OUTPUT open`と合成frameも確認。
- ただしWayland所有者PID 18を終了させた後の直接表示デモは`No such device`、`VKDEMO FAILED ... result=-4 frames=0`を返し、QEMUのSDLとQMPが応答しなくなった。この状態ではSSHも停止した。表示所有者の終了・切替が未解決であり、受け入れはまだ満たさない。
- Windows fork alpha10 `e80354b8f5a0dedc62374b905a181874682659cc`向けrenderer差分は[winq-virglrenderer-alpha10.patch](winq-virglrenderer-alpha10.patch)。Windows UCRT64の`ninja -C builddir -j4`でDLL build PASS、baseline indexへの`git apply --check --cached`で適用性PASS。元の`C:\WINQ-EMU\bin`は変更せず、隔離コピー`C:\Work\winq-zedbsd`で試験した。
- ユーザー指定のRaspberry Pi 4ツリーにあるInter、JetBrains Mono、Droid Sans Fallbackを、同じ場所のライセンス文書とともに`userland/desktop/fonts/`へコピー。3つのTTFは元の`build/ws035-fonts`とSHA256一致。wayland packageのdataとして`/usr/share/fonts/keiland*.ttf`へ導入し、`make`とimage検査PASS。guestの`ZWL GLASS ready text=1`、Windows SDLのランチャーで「Terminal」「Model viewer」「Gears」「Files」「Browser」「Lock Screen」「Log Out」を目視確認。証拠画像は`C:\Users\TabataKeiichi\Documents\Codex\2026-09-29\ws\outputs\zedbsd-windows-venus-menu.png`。
- Windows QEMUプロセスにIntel Vulkan ICD `igvk64.dll`、Intel OpenGL ICD `igxe2lpgicd64.dll`がロードされ、guest `vkdemo`は`Intel(R) Arc(TM) 130V GPU (16GB)`と報告。llvmpipeではない。現行のcopy表示はGPUからCPUへのreadbackと2D transferを行うため、表示の遅さが残る。Wayland PERFの起動時合成は約338ms/frame、後続の静的再描画では約10ms/frameも観測された。アニメーション性能は未評価。
- copy表示の実測ではGPU fenceが1〜6ms、guestからblobへの4,096,000バイト書き込みが450〜564ms、scanout ioctlが13〜27ms。速度の主因はCPU経由の転送。
- `vendor/winq-emu-qemu` alpha10 `4dd4da5f`、`vendor/winq-emu-virglrenderer` alpha10 `e80354b8`を導入。QEMU forkはupstream 11.0.0比で58ファイル、追加5,809行・削除112行。remote originはユーザーfork。
- QEMUのWindows版`virtio-gpu-udmabuf-stubs.c`はblob scanoutの成功を返すだけで画面を更新しない。`virtio-gpu-virgl.c`にmapped blobをpixman/SDL表示面にする変更を直接実装。修正済みQEMUだけが既存rendererのvendor capsetにhost scanout bit 8を付加し、guestはこのexact profileでnative共有表示を選ぶ。Windows UCRT64でrendererとQEMUのビルドはPASS。
- flags15を広告した初回実機起動は`vkQueueSubmit=-4`と`venus: context quarantined`。カーネルの`venus_strict_queue_find`がflags3/7だけを認識していたため、exact flags15にもstrict queue/quiescenceを付与した。修正後の`vkdemo --offscreen --time-ms=1000`はIntel Arcで既知SHA256 `6a446b50d2a1b40c743a77347615576bc0cb65e9eb55ef2209e7ed8a0f03e9ac`とDONE。
- Windows native共有画像はDMA-BUF形式ではhost-visible memoryの`vkAllocateMemory=-2`となる。exact flags15のみOPAQUEとして画像とメモリを確保し、カーネルは同一GPU内OPAQUE画像のexportを許可する。QEMUはblobのDMA-BUF fdを要求せずrenderer resourceをmapし、SDLのGLが扱うpixman色形式を追加した。
- QEMUのSDL表示面更新後にGLコンテキストをrenderer側へ戻さないとctx0 fenceが失敗し、guestは60秒後にtransport timeoutとなった。fence生成前に`virgl_renderer_force_ctx_0()`を呼ぶ修正後、Windows WHPXのSDLで1280×800のデスクトップをQMP screendumpとユーザー目視で確認。現行セッションの`/run/user/1000/session.log`に`ZWL VULKAN_ERROR`、`ZWL FAILED`はなく、更新時の合成は16〜34ms/frame（起動・静止時以外の観測）。`make CONFIG_PCAT_SERIAL_MIRROR=y`とimage検査はPASS。通常の`make`もimage検査までPASS。
- 元の`C:\WINQ-EMU\bin`は未変更。試験バイナリは`C:\Work\winq-zedbsd`、vendor sourceはユーザーのforkをoriginにした未commit状態。旧copy表示と所有者切替で報告したハングの再検証は別途必要。

## 手順・制限

exact flags15でWindows OPAQUE画像を共有表示し、flags7ではcopy fallbackを維持する。Linuxの1.3.269 profileは変更しない。次はログアウト・ログインなど表示所有者切替と継続的なアニメーションの速度を確認する。集合`make check`は実施しない。

## 2026-09-29 追加範囲と検証: Windows SDL multitouch

ユーザー指示: WindowsのQEMU画面で人がVenus表示とタッチUIをデバッグできるようにする。実USB機器のアタッチは避ける。SDLの指イベントを既存のzedBSD USB HIDマルチタッチドライバへ渡す。

QEMU forkのSDLにSDL_FINGERDOWN/MOTION/UPを追加してQEMUの既存マルチタッチ入力APIへ渡した。仮想usb-multitouch HIDデバイスは10指、Contact Count、Scan Timeを63バイトのreportとして送る。SDL合成マウス入力はタッチハンドラがあるときだけ抑制する。Windows UCRT64でQEMUのninjaビルドはPASS。

隔離QEMUでは追加オプション -device usb-multitouch,bus=xhci.0,port=1 で起動した。zedBSD dmesgに0627:0005、QEMU Multitouch Screen Touchscreen、report-bytes=63、/dev/input/event2が出た。zdesktopはkind=touch abs=1として認識。QMPから2指のbegin/data/endを送り、画面左上のタップでホームメニューが開いた。物理WindowsタッチパネルからのSDLイベント入力は未試験。元のC:\WINQ-EMU\binは変更せず、試験バイナリはC:\Work\winq-zedbsd。

## 2026-09-29 Windows配布フォルダ

ユーザー指示: C:\Work\winq-zedbsd内だけで起動できるboot.batを置き、HDDはdata/hdd-image.img、必要なfdもdata内へ。バッチは自身のディレクトリへ移動してからQEMUを相対パスで起動し、-L share、WHPX、Venus、SDL、usb-multitouchを指定する。COM1はstdioに接続する。

data/hdd-image.imgはビルド済みamd64 imageのコピー。data/edk2-x86_64-code.fdは同梱shareから、data/ovmf-vars.fdは未使用のUEFI変数テンプレートからコピー。QEMUのexe・renderer・DLLは同じ配布フォルダに置く。C:\Work以外を作業ディレクトリにした起動と、空白を含むジャンクション名からの起動の双方でguestのZWL OUTPUT open 1280x800、入力event2 kind=touchを確認。QEMUのロード済みSDL2、virglrenderer、pixman等の非Windows DLLは配布フォルダ内にあった。WindowsのVulkan loaderとGPU ICDはOS側。確認後にimageとUEFI変数の初期状態を復元し、ユーザーの画面を戻すため配布フォルダのboot.batから再起動した。

## 2026-09-29 Files起動時の停止の修正

ユーザーの「Filesをクリックして起動するとフリーズする」に対応。共有画像の取り込み時に guest の vkAllocateMemory が -4、compositor が終了することを再現した。Windows renderer の IMPORT_RESOURCE 受信が、header を除いた独立の C struct で残りを読んでおり、送信側の uint64_t size の前の padding 4 bytes と位置が一致しなかった。元の request struct の header 後から末尾までを受け取るように修正した。また、Windows の inline fd 転送は SCM_RIGHTS と異なり複製しないため、送信側で独立した fd を用意し、成功時は受信側に所有権を渡す。受信側の import 失敗時はその fd を解放する。

変更は vendor/winq-emu-virglrenderer/src/proxy/proxy_server.c と proxy_context.c。Windows UCRT64 ninja build PASS。調査用の guest import ログと仮の画像設定変更は撤去し、通常 make と image 検査 PASS。配布先 C:\Work\winq-zedbsd\libvirglrenderer-1.dll に反映した。

配布フォルダの既存 data/hdd-image.img で Files の初回表示、Documents への移動、終了 status=0、Files の再起動、Terminal の同時起動・surface map を確認。ZFILES READY、共有画像3枚の import 成功、QEMU stderr に fence error なし。証拠は C:\Work\files-fixed.png と C:\Work\zedbsd-winq-files-fixed-stderr.log。commit/push は行っていない。

調査中、壊れた通信からの guest GPU reset で vrend_destroy_context から NULL 呼び出しとなる別の host crash も捕捉した。正常なアプリ起動では再現しなくなったが、意図的なGPU障害からの回復と以前からの表示所有者切替は未検証。
