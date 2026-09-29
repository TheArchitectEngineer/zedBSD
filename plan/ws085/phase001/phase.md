<!-- awesome-plan project=zedbsd record=ws085-p001 -->

# ws085-p001: Windows Venusのrenderer契約と表示経路

Status: in-progress
Disposition: normal
Parent: [WS085](../ws.md)
Queue: [q499](../../queue.md) / q499-i01
Approval: ユーザー「Windowsで動くように修正してみてください。」（2026-09-29）

## 範囲と受け入れ

WINQ-EMU Alpha10の隔離コピーに、固定のzedBSD strict queue/quiescence契約を移植したrenderer DLLを使う。Windows hostは元のインストールを上書きしない。zedBSDのlibvulkanが既存の1.3.269 codecで確認した1.4.343 rendererのコマンド集合を受け、Windowsのblob scanoutが表示できない場合だけ既存のcopy表示を使う。ユーザー指定の`10.0.10.2:zedBSD-rpi4/`からデスクトップのフォントを取り込み、ゲストの`vkdemo`のGPU readbackとWindowsのSDL実画面のデスクトップ・メニューを確認する。

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

## 手順・制限

`userland/desktop/libvulkan/context.c`でexact host XML 1.4.343を別profileとして認識し、`wsi-display.c`でそのprofileの共有画像importを断って既存copy fallbackを選ぶ。Linuxの1.3.269は現行のGPU共有表示を維持する。次はWindows hostで表示所有者を終了させた時のrenderer/QEMUの待機箇所を特定し、lease解放と次の直接表示が通ることを確認する。集合`make check`は実施しない。
