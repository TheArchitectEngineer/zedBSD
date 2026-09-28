<!-- awesome-plan project=zedbsd record=ws069 -->

# WS069: Wayland デスクトップ（zdesktop）で X11 の app を動かす（単体の xserver、GLX）

<!-- awesome-plan-current:start -->
Status: completed（2026-09-27）
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: none（最後の Queue は q492）
Resume point: なし（完了）
<!-- awesome-plan-current:end -->

## 目標

2026-09-26 ユーザー: 「ホーム画面、EGL/GLES、WaylandコンポジタのX11機能など、デスクトップ関連の作業を優先しつつ…」
「WaylandコンポジタのX11サーバ機能については、GLX拡張も実装しておいてください。」
2026-09-27 ユーザーの判断（[design.md](design.md) §0）: X server は単体のプログラム `userland/desktop/xserver`、rootless だけ、
標準の Wayland と Vulkan、非標準の拡張は libkeiland、zdesktop に組み込める module の形。Xzed はレトロ用に戻す。

## 結果

- **`/bin/xserver`**（`userland/desktop/xserver/`）: rootless の X11 server。X の top-level の窓それぞれが
  zdesktop の xdg_toplevel（Wiseman が装飾）。組み込める API（`x11server.h`: create・pollfds・dispatch・stopped・destroy、global な
  状態なし）と module（server・protocol・window・rootless・wayland・vulkan・keymap・glyphs・glx）。client ごとの出力の queue（POLLOUT で
  送る）、止まった client の報告。窓は top-level ごとの Vulkan の swapchain（MAILBOX、staging からの copy）で表示し、使えないときや
  `--shm` では wl_shm。core font は libtruetype の等幅 font。
- **GLX**: server の GLX 拡張（opcode 144）と client の `libGL`（GLX 1.4、EGL/GLES（WS068）の pbuffer に描いて PutImageRGB24 で窓へ）。
  固定機能の GL 1.x（glBegin/glEnd、行列、光源、display list）。試験の app は glxtest と zgears。
- **BUG-057**（GLX の間欠の止まり）: kernel の unix socket と socket の packet の待ちが `waitq_sleep` の EAGAIN（眠る前の wakeup）を
  失敗として返し、blocking の send が失敗していた。修正（p010）。
- libX11 の XPending を MSG_PEEK で 32 byte の event 単位に、`wr()` の失敗の報告。
- Xzed（`userland/retro/xzed`）は ws069 の前（`cc4433d4`）に戻した（`/dev/graphics` のレトロ用のデモ）。
- App Home の `zdesktop-x11` が xserver を起動する。

## 受け入れの確認（p006、2026-09-27）

1. ~~rootful~~: 2026-09-27 ユーザーの判断「rootlessのみでOKです」で外した。
2. rootless で X の app の窓が Wiseman の窓になる: Venus の x11-p003（zterm、入力、docked）PASS。
3. GLX: x11-p004（glxtest、docked の大きさの変化を含む）・x11-p005（zgears 300 frame、回る）PASS。zdesktop-p070（App Home から Gears と
   X terminal）PASS。
4. i915 実機（capture の `zdesktop-x11`）: p006 の run1・run2 で 6 検査 PASS（Gears が回る、X terminal、仮想デスクトップ 2 と戻り）。
   規約の全文との照合（p006: 3 つ以上の条件の分割 19＋9 か所、`X11SERVER_SHM` の環境変数を `--shm` の option に、lookup と GLX の
   要求の成功の return を最後に、直接の return の分割）。新しい file は style-check 0、変えた legacy file（xlib.c 188→186、
   unix-socket.c 156、socket.c 62）は増えない。boot test PASS（`build/ws069-p006-boot/login.png`）。
   QEMU の証拠と実機の証拠は上のとおり分けた。実機の LCD の目視と実機の fps の測定（ufs-cat が疎な file の log を読めない）は未実施。

## 制限・移管

- Venus では Vulkan の窓の道が wl_shm より約 3 倍遅い（zgears 2.4 fps と 7.8 fps。命令ごとの同期の往復）→ F-021。
- GLX の画像は CPU で読み戻して PutImage で渡す。GPU の buffer のまま渡す段（DRI3/Present に当たる、libkeiland の
  `keiland_gpu_buffer_v1`）→ F-030。
- X の screen の大きさは `--size`（既定 1280x800）で、wl_output に合わせない → F-024。
- zdesktop への内蔵はしていない（API はその形）。
- BUG-056（zdesktop が App Home の後や client の後片付けで終わる）・BUG-058（zgears が最初の frame の前に黙って終わる、1 回）は再発時に調べる。
- 試験は [plan/tools/x11/](../tools/x11/) へ移した（master の Tools 節）。

## Phase 一覧（記録は git の履歴）

| Phase | 内容 | Status |
| --- | --- | --- |
| ws069-p001 | 設計（[design.md](design.md)） | cleared（q472） |
| ws069-p002 | rootful の Wayland backend（Xzed の上） | cleared（q473）。p009 で Xzed を戻し、rootful は外した |
| ws069-p003 | rootless | cleared（q474） |
| ws069-p004 | GLX の核 | cleared（q477） |
| ws069-p005 | 固定機能の GL 1.x と gears | cleared（q478） |
| ws069-p007 | i915 実機での BUG-057 の段の特定 | uncleared・canceled（q485、X server を作り直すため p010 へ） |
| ws069-p008 | xserver（単体の rootless のプログラム、組み込める module） | uncleared（q488）→ cleared（p010 の後の追記） |
| ws069-p009 | Xzed をレトロ用に戻す | cleared（q490） |
| ws069-p010 | BUG-057 の原因と修正（kernel の socket の待ち） | cleared（q489） |
| ws069-p011 | 窓を Vulkan の swapchain で表示、Wayland の dispatch を非 blocking に | cleared（q491） |
| ws069-p006 | 規約の全文との照合と回帰（最後） | cleared（q492） |
