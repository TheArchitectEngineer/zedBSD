<!-- awesome-plan project=zedbsd record=ws035 -->

# WS035: デスクトップ環境の構築とGPUドライバの安定化

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG006
Related Milestones: MG001, MG005
Objectives: O1, O2
Parent: [Master](https://github.com/awemorris/zedBSD/issues/1)
Queue: なし（計画のみ。実行Queue未作成）
GitHub: 未公開（ローカル計画。同期キャッシュ未構築のチェックアウトで作成）
<!-- awesome-plan-current:end -->

## 単一目標

zedBSD上で日常的に使えるデスクトップ環境（zdesktop）を構築する。zdesktopは
Wayland/X11コンポジタで、タスクバー・音声・フォント描画を持ち、最後にChromiumを動かす。
その過程でGPUドライバの不具合と不足を見つけて直し、安定させる。

到達点: zdesktop上でWaylandクライアントとX11クライアントを表示・操作できる。
ウィンドウ管理・装飾・タスクバー（WiFi・音量）・タイル一覧が動く。audiod経由で音が出る。
Chromiumが起動してページを表示する。途中で見つかったGPUドライバの問題は、修正済み
またはBugに登録済みである。

2026-09-23 ユーザー指示「もう1つwsを追加します。これはデスクトップ環境の構築と、
それを通じたGPUドライバの安定化を目標とします。」

## 範囲（ユーザー指定）

1. **refactor（最初に行う）**
   - `include/drivers/` を `src/drivers/` と同じ階層に揃える。
   - `libc/` を `src/libc/` へ移し、`libc/include/*` を `include/` へ移す。
   - `include/boot/*` を `include/kern/boot/` へ移す。rpi4とsun4uのboot定義
     （現在の `include/kern/rpi4/boot.h`、`include/kern/sun4u/boot.h`）も同じ場所へ移す。
2. **`/dev/graphics` の移行**
   - `/dev/graphics` はレガシーな2Dフレームバッファと、レトロPCのVRAMを扱う。
   - GPUドライバがロードされたら、`/dev/graphics` に通知する。
   - 通知を受けた `/dev/graphics` は、GPUのscanoutバッファを使うように切り替える。
   - これにより、ファームウェアのフレームバッファからGPU初期化後の画面へ継ぎ目なく切り替える。
3. **audioドライバ**
   - FreeBSDのOSS（`/dev/dsp0`）とおおむね同じAPIにする（完全互換は求めない）。
   - まずaudioドライバのフレームワークを作る。
   - 次にQEMUのIntel HDAエミュレーションを使ってhdaドライバを開発する。
   - 最後にPCIパススルーで実機のHDAを使い、再生できるようにする。
4. **audiod**: `/sbin/audiod` を作り、PulseAudioのごく基本的な再生機能を提供する。
   Chromiumが動くように互換libpulseを作る。最初は再生と録音だけで、転送等は不要（2026-09-23追加指示）。
5. **libtruetype**
   - Unicodeのttfフォントだけに対応する、ごく単純なフォントレンダリングライブラリ。
   - FreeTypeのライセンスが特殊なため、Zlibライセンスで独自に実装する。
   - zdesktopと関連アプリから使う。
6. **zdesktop**
   - `zwl` を `/bin/zdesktop` へ改名する。
   - 簡単なウィンドウ管理を実装する。
   - KWinのように、コンポジタ側でウィンドウタイトルとフレームを描く。
   - タイトルのフォントは、後でFreeType等を使う予定。当面は起動時に `/dev/graphics` から
     ASCII文字のglyphを全部取得して使う（既存の `KERN_GRAPHICS_GET_GLYPH` を使う）。
   - X11プロトコルの基本部分だけを実装し、X11サーバとしても振る舞う。既存Xzedのコードを
     コピーして使ってよい（Xzedは自作のZlibコードである）。
   - コンポジタに簡単なタスクバーを実装する。WiFiと音量の状態表示と操作を持つ。
   - コンポジタに簡単なウィンドウ一覧のタイル表示を実装する（Windows+Tab）。
7. **Chromium（最後）**: `userland/packages/network/chromium` に追加する。
   全ての基盤が揃わないと着手できないため、最後に行う。

## 調査で分かった現状（2026-09-23）

- `include/drivers/` はフラット（`pci-xhci.h`、`usb-storage.h`、`gpu.h` 等）で、
  `graphics/` と `hid/` だけがサブディレクトリになっている。`src/drivers/` は
  `usb/ pci/ gpu/ wifi/ ethernet/ fs/ disklabel/ isa/ generic/ platform/<機種>/` に分かれている。
- libcは2箇所ある。`libc/`（`libc.mk`、`include/`、`ctype.c` 等）と
  `userland/base/libc/`（`posix.c` 等）である。今回の移動対象は前者。
  `libc/include/` には標準ヘッダのほか、`X11/`、`vulkan/`、`wayland*`、`linux/`、`machine/` がある。
- `include/` には `boot/ drivers/ hal/ kern/ uapi/` がある。`include/kern/boot.h` という
  ファイルが既にあり、新しいディレクトリ `include/kern/boot/` と名前が並ぶ。
- boot関係のヘッダは `bootloader/include/`、`bootloader/sparcv9/handoff.h`、
  `src/hal/x86/boot-parameters.h` などにも散っている。今回の移動対象はユーザー指定の範囲に限る。
- `/dev/graphics` は機種ごとの実装（`src/drivers/platform/pcat/graphics/pcat-graphics.c`、
  `.../pc98/graphics/pc98-graphics.c`）だけで、共通層もGPU連携も無い。UAPIは
  `include/uapi/graphics.h`（ioctl 1–11。glyph取得の10を含む）。
- GPUは、共通層 `src/drivers/gpu/gpu.c` と、`venus/`・`i915/`（WS031がdisplay/KMSを実装中）。
- audio関連のコードは無い（ヒットしたのはi915 displayのaudio power制御だけ）。
- `zwl` は `userland/base/zwl/`。Xzedは `userland/X11/xzed/`（全ファイル `SPDX: Zlib`）。
  X11側には既存の `zwm`・`zshell`・`zterm` がある。
- WiFiは `/sbin/wifi` と `include/uapi/wlan.h`。networkdの状態をデスクトップへpushする経路は無い（後述）。

## 実行開始条件（2026-09-23ユーザー指示）

このWSの実行は、別エージェントが作業中のWS031とWS032が完了するまで始めない。
計画はそれ以前に進めてよい。両WSの完了後は、このWSのrefactor（p002–p004）を最優先で行い、
その後にこのWSの残りと WS034 を進める。

## GPU安定化の試験環境: QEMU＋VFIOのi915（2026-09-23ユーザー決定）

GPU安定化はi915で行う。エージェントが自律ループを回せるように、実機のIGDをQEMUへ
VFIOでパススルーして試験する。動作したコマンドラインは過去のWSから次のとおり特定した。

**参照条件**（WS031 E-49で統一。出典 `plan/ws031/handover/README.md` §4.2、
2026-09-15の確立手順 `plan/ws031/tests/vfio-passthrough-procedure.md`）:

```bash
cp -f /usr/share/OVMF/OVMF_VARS_4M.fd vg-parity.fd
sudo timeout 240 qemu-system-x86_64 \
  -machine q35,accel=kvm,memory-backend=mem \
  -cpu host,host-phys-bits-limit=39 \
  -m 4096 -smp 4 \
  -object memory-backend-memfd,id=mem,size=4G,share=on \
  -device vfio-pci,host=0000:00:02.0,x-igd-opregion=on,rombar=0 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=vg-parity.fd \
  -drive file=guest-parity.img,format=raw,if=ide,index=0 \
  -vga std -display none -monitor none -serial none -nic none \
  -debugcon file:run-parity.log -no-reboot
```

必須条件と注意（いずれもWS031で実測した失敗に基づく）:

- `host-phys-bits-limit=39` は必須。IOMMUのMGAWが39で、無いとVFIOのDMA mapが-22になる
  （WS031 E-23/E-42/E-43）。
- `memory-backend-memfd,share=on` と `memory-backend=mem` を組にする。
- `x-igd-opregion=on,rombar=0`。`rombar=0` はIGD option ROMの警告を抑える。OVMFでは
  ASLS=0（OpRegion不在）になり、SeaBIOSではOpRegion/VBTが読めた（`plan/ws031/display-ref/README.md`）。
- `-vga std` はUEFIローダのGOP用に要る。無いと `Locate GOP` で止まる（WS029 boot-002）。
- カーネルログは `-debugcon`（port 0xe9）のファイルに出る。`sudo` で起動する（memlock制限のため）。
- ホストは起動時にIGDを `vfio-pci` へbindしている（`/etc/modprobe.d/vfio-igd.conf` で
  i915/xeをblacklist、`options vfio-pci ids=8086:46a8`、ヘッドレス）。WS029の実行時
  unbind（`host-igd.sh attach/restore`）は、間欠ハングの原因になったため使わない。
- 復旧はmodprobe.d/modules-load.dの設定を消してinitramfsを更新し、再起動する（手順は上記の確立手順）。
  hostのreboot・設定変更は、その時点でのユーザー許可が要る。
- 対象ホストはLatitude 5330（`awe@10.0.10.25`）。WS031完了後にホスト状態が変わっている
  可能性があるので、p001で再確認する。

この環境には次の制約がある。p001で検証方法を決める。

- **画面の照合**: IGDはゲスト内で内蔵eDPパネルを駆動する。QEMU側からVNC等で画面を
  取り込むことはできない。自律ループでは、scanoutバッファの読み戻し、pipe CRC、
  plane/pipeレジスタの状態で照合する。パネルを目で確かめるのは人の作業になる。
- **ファームウェアFBからの継ぎ目のない切替え（p005）**: VFIO下ではGOP FBが `-vga std`
  側にあり、IGDのパネルとは別の出力になる。「同じ画面上で継ぎ目なく」の確認は、
  実機のベアメタル起動でしかできない部分が残る。QEMUでは、通知・切替えの手順と
  画面内容の引継ぎ（バッファの一致）を確かめる。
- HDA（p008）も同じホストでVFIOを使う。PCHのHDAのIOMMU groupと、IGDとの同時
  パススルーをp001で確認する。

## 前提・依存・衝突

- **他エージェントとの衝突（最重要）**: p002–p004の一括移動は、`~/zedBSD/` で進行中の
  WS031（`src/drivers/gpu/i915/`）とWS032（libc・`platform/amd64/`）の未コミット作業に
  ぶつかる。移動は両WSの完了後に行う（上記の実行開始条件）。
- **GPU**: 主対象はi915（QEMU＋VFIO、上記）。WS031のdisplay/KMSとnative Vulkanの成果に依存する。
  virtio-gpu（Venus＋host Lavapipe）は、GPUに依存しない部分を素早く試す補助経路として使ってよい。
- **WiFi表示**: networkdに状態のpush通知は無い（下記の調査）。このWSのp018で実装する。
  WS005 p016（同じ目的のplanning Phase）はこのWSへ移管し、canceledとした。
- **Chromium**: WS032のclang/libc++（p005/p008）、WS034の基盤（libc是正）、本WSの
  audiod・libtruetype・zdesktop（Wayland）・GPUに依存する。
- **HDA実機**: WS029と同じVFIOループを想定する（Latitude 5330）。ただしPCHのHDA
  （`00:1f.3`）は、LPC/SMBus等と同じIOMMU groupに入っている可能性がある。単独で
  パススルーできるかをp001で確かめる。

## 制約（Guardrail）

- HAL（`include/hal/hal.h`、`src/hal/`）の変更は、具体的な差分ごとにユーザーの事前承認を得る。
  refactorでHAL配下のinclude文が変わる場合も同じ扱いにする（機械的な置換だけの差分として提示する）。
- UAPIの追加（`/dev/dsp`、graphics通知、audiod protocol等）は、該当Phaseの設計として提示・合意してから実装する。
- RTL8822Bの `.inc` は分けたまま保つ。`drv_` のglobal symbol方針を維持する。
- aggregate `make check` は使わない。git add/commit/pushはユーザーが行う。
- 全文規約 [coding-style.md](../coding-style.md) を適用する。Chromiumへのパッチはupstreamの書式に合わせる。

## Phase一覧

近い順に具体化し、遠い項目は目的と主要リスクだけを書く。見積はQueue作成時に確定する。

| Combined ID | Phase | Status | 依存 |
| --- | --- | --- | --- |
| ws035-p001 | 設計固め: 移動対応表・VFIOホスト状態の再確認・画面照合方法・HDAのIOMMU group確認 | planning | — |
| ws035-p002 | refactor: `include/drivers/` を `src/drivers/` の階層に揃える | planning | p001 |
| ws035-p003 | refactor: `libc/` → `src/libc/`、`libc/include/*` → `include/` | planning | p001 |
| ws035-p004 | refactor: `include/boot/*` と rpi4/sun4u boot → `include/kern/boot/` | planning | p001 |
| ws035-p005 | `/dev/graphics` の共通層と、GPU scanoutへの引き継ぎ（i915、補助にvirtio-gpu） | planning | p002–p004 |
| ws035-p006 | audioフレームワークと `/dev/dsp`（OSS互換寄りのAPI） | planning | p002–p004 |
| ws035-p007 | hdaドライバ（QEMU intel-hda） | planning | p006 |
| ws035-p008 | hdaドライバの実機再生（PCIパススルー） | planning | p007 |
| ws035-p009 | `/sbin/audiod`（PulseAudio基本再生） | planning | p006 |
| ws035-p010 | libtruetype | planning | p003 |
| ws035-p011 | zdesktop: 改名、ウィンドウ管理、コンポジタでのタイトル・フレーム描画 | planning | p005 |
| ws035-p012 | zdesktop: X11サーバ機能（Xzedから移植） | planning | p011 |
| ws035-p013 | zdesktop: タスクバー（WiFi・音量の表示と操作） | planning | p011, p009, p018 |
| ws035-p014 | zdesktop: ウィンドウ一覧のタイル表示（Windows+Tab） | planning | p011 |
| ws035-p015 | GPUドライバ安定化の受け皿（デスクトップで見つかった問題を集約して修正） | planning | p005 |
| ws035-p016 | Chromium（`userland/packages/network/chromium`） | planning | p009–p015, p019, WS032-p005/p008, WS034 |
| ws035-p017 | 全文規約確認・回帰・制限整理（必須の最終確認） | planning | p002–p016, p018, p019 |
| ws035-p018 | networkdの状態push通知（購読）とzdesktopでの受信（WS005-p016から移管） | planning | p002–p004 |
| ws035-p019 | 互換libpulse（再生・録音の基本API） | planning | p009 |

p018・p019は2026-09-23の追加決定で後から加えた。IDは追加順である。p019はp009（audiod）と
対になり、p013（音量）とp016（Chromium）の前に行う。

p015はWS034-p005と同じように一度では閉じない。後続Phaseで見つかったGPU問題はp015へ戻して直す。

## Phaseの要点とリスク

- **p001**: 移動前後の対応表（ファイル単位）を作り、include文・Makefile・sysroot生成・
  noct宣言生成・試験のパス参照の影響範囲を洗い出す。受入環境（QEMU amd64、GPUとHDAはVFIO）、
  HDAの実機host facts（IOMMU group、host側の
  snd_hda_intelのunbind/restore手順）を確定する。
- **p002**: 例: `include/drivers/pci-xhci.h` → `include/drivers/usb/xhci/...`、`gpu.h` →
  `include/drivers/gpu/...`。規則は `src/drivers/` のディレクトリと同じにする。機械的な
  移動とinclude置換に留め、中身は変えない。全platformのbuildで確かめる。
- **p003**: 最大のリスク。`include/` へlibcの公開ヘッダ（`stdio.h`、`sys/` 等）が入ると、
  kernelのbuildで `-Iinclude` からuserland用ヘッダを誤って拾う恐れがある。kernel/HALの
  include経路にlibcヘッダが入らないことを、build設定か検査で保証する。sysrootへの
  install先は変えない。`userland/base/libc/` の扱い（移動しない想定）もp001で確定する。
- **p004**: `include/kern/boot.h` と `include/kern/boot/` の関係（そのまま並べるか、
  中へ移すか）を決める。bootloader側のコピーや参照も追う。
- **p005**: 共通の `/dev/graphics` 層を作り、ファームウェアFB（GOP等）と機種VRAMを
  backendにする。GPUドライバがscanoutを確立したら、kernel内部APIで `/dev/graphics` へ
  通知し、画面内容を引き継いだうえでGPU scanoutへ切り替える。GPU側が故障・解放したら
  元に戻す。console（`kern_text_register`）との関係も整理する。QEMU（virtio-gpu）で
  切り替えの前後を画像で照合する。i915はWS031の成果が揃ってから行う。
- **p006**: `/dev/dsp0` を中心に、`SNDCTL_DSP_*`（形式・チャネル・レート・fragment・
  GETOSPACE・SYNC等）と `/dev/mixer` 相当の音量を、必要な範囲で提供する。driver ops構造体、
  DMAリング、underrun処理を作る。
- **p007**: QEMUの `-device intel-hda -device hda-output`（またはhda-duplex）で、
  codec列挙、stream DMA（BDL）、再生を作る。QEMU側でwav captureして独立に照合する。
- **p008**: WS029と同様のVFIOループで実機HDAを使う。host側の音声停止と復旧を、
  ユーザーの許可範囲で行う。
- **p009**: `/sbin/audiod` が `/dev/dsp` を占有し、複数クライアントの再生をミックスし、
  録音を配る。クライアントとの通信は独自protocolでよい（互換性はp019のlibpulseで取る）。
  最初は再生と録音だけにする。転送（network sink等）やmodule機構は作らない。
- **p019 互換libpulse**: Chromiumが動くことを目的に、`libpulse.so` をAPI互換で提供する
  （2026-09-23ユーザー決定）。Chromiumは非同期API（`pa_threaded_mainloop`、`pa_context`、
  `pa_stream`、introspect）を使うので、`pa_simple`だけでは足りない。最初は再生と録音の
  基本機能に絞り、Chromiumなどで足りない機能に気づいたら、後からPhaseを作って足す。
  libpulseはLGPLなので、ヘッダも実装もupstreamから転記せず独自に書く（API名と型の互換だけを取る）。
- **p010**: `cmap`（format 4/12）、`glyf`（2次ベジエ）、`loca`/`hmtx`/`hhea` を読む。
  anti-aliasのスキャンライン描画を行う。hintingとCFF/OpenTypeレイアウトは範囲外。
  FreeType由来のコードは参照・転記しない。`/lib/libtruetype.so` として提供する。
- **p011**: `userland/base/zwl/` を改名する（ディレクトリ名もzdesktopにするかはp001で決める）。
  focus、移動・リサイズ、z-order、最小化・最大化、server-side decoration。
- **p012**: Xzedの既存コード（Zlib）を取り込み、基本requestとXWayland相当の合成を行う。
  既存 `userland/X11/`（zwm、zshell、zterm）との関係はp001で決める。
- **p013/p014**: タスクバーはWiFi（networkdの通知）と音量（audiod）を表示・操作する。
  タイル表示はGPU合成を使う。
- **p016**: 最大のPhaseで、着手時に分割する前提。GN/ninjaによるcross build、Ozone
  （Wayland）backend、Vulkan/ANGLE、PulseAudio経路、フォント（Chromium同梱のFreeType/
  fontconfig）を扱う。FreeBSD/OpenBSDのport差分を参考にする。

## WiFi状態通知の調査（2026-09-23）

networkdには、状態をpushで通知する経路が**無い**。

- 制御は `/run/networkd.sock`（`AF_UNIX`、`SOCK_STREAM`、mode 0660）の要求・応答だけで、
  opcodeは `SHOW`、`UP/DOWN`、`DHCP`、`STATIC`、`WIFI_ENABLE/DISABLE/LIST/CONNECT/DISCONNECT`、
  `LAN_ENABLE/DISABLE` 等である（`userland/base/net/protocol.h`）。購読やイベント配信の
  opcodeは無い。
- networkd自身はkernelのroute socket（`PF_ROUTE`、`RTM_IFINFO` の carrier up/down・removal・
  overflow）を受けている。これはリンクとassociationの変化だけで、SSID・接続中の状態・IPの
  取得などは含まない。
- WS005 p016（planning）がこの通知を計画していたが、未実装である。

したがって、ユーザー指示どおりこのWSのp018で実装する。networkdに購読用のopcode
（接続すると現在のsnapshotを送り、以後は変化を送る）を加え、遅いクライアントが
networkdを止めないよう、有限のキューと取りこぼし時の再同期を持たせる。
WS005 p016の受け入れ条件（秘密情報を含まない、閲覧から制御権限を得ない、daemon再起動後に
一致する）を引き継ぐ。

## 決定事項（2026-09-23ユーザー回答）

1. 実行はWS031・WS032の完了後。refactor（p002–p004）を最優先で行う。
2. GPU安定化はi915で行い、エージェントの自律ループのためQEMU＋VFIOで試験する（上記の参照条件）。
3. audiodは、Chromiumが動くように互換libpulseを作っていく（p019）。最初は再生と録音だけ。
   転送等は不要。足りない機能は気づいた時点でPhaseを追加する。
4. WiFi状態通知は既存に無いため、このWS（p018）で実装する。WS005 p016は移管済み。

## 未決事項

1. HDA実機（p008）のIOMMU groupと、IGDとの同時パススルーの可否（p001で調査）。
2. 画面の照合方法（scanout読み戻し・pipe CRC等）と、人が目で確認する範囲（p001で決める）。

## 現状

計画を作っただけで、Queue・実装・試験はまだ無い。GitHub Issueも未作成である。
