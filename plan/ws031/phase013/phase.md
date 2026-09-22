# WS031 p013 計画: Wayland モデルビューア（Venus 版）

ユーザー決定（2026-09-22）: WS031 に Phase を追加し、Wayland クライアントのデモを、ユーザーが著作権を持つ FBX モデル
（`QS-40-49244 r4.fbx`）をマウスで回転・移動・拡大縮小して閲覧できるモデルビューアにする。まず Venus（virtio-gpu）で動くアプリを作り、
その後 i915（QEMU＋passthrough）で動かす（p014）。i915 版を通して i915 のシェーダーを一通り動かせるようにする。

## ユーザー決定
- FBX はビルドホストで分解し、**独自のテキスト表現のポリゴンリスト**と**非圧縮テクスチャ**にする。テクスチャは縮小してよい。
  ビューアは FBX・zlib・PNG を扱わない。
- **zwl に `wl_seat`・`wl_pointer`・`wl_keyboard` を追加**する（マウス・キーボード）。zwl を「入力なしの試験用 compositor」とした
  WS014/WS029 の範囲（`plan/ws029/ws.md:64`、`plan/config.md:209`）はこの Phase で拡張する。
- モデルはユーザーが著作権を持ち、変換結果をリポジトリに commit してよい。

## モデル（調査結果）
binary FBX 7.4、7.9 MB。表示対象の mesh 4（Body 8239 頂点、Face 4043、Hair 10288、bag 2868; 三角形換算 約 3.6 万）、
material 16（Phong）、埋込み PNG 20（最大 2048×2048、多くが RGBA）。bone 142・skin・blend shape 57 は使わず bind pose を表示する。

## Module と所有ファイル
| Module | 場所 | 内容 |
| --- | --- | --- |
| 変換器 | `userland/base/mview/tools/fbx2mview.py` | FBX binary 7.x の parser（zlib 配列）、mesh を三角形へ分割、法線・UV・material 割当、埋込み PNG の抽出・縮小（最大 1024）、出力 |
| モデル資産 | `userland/base/mview/models/qs40/` | `model.txt`（テキストのポリゴンリスト）、`tex/*.pam`（非圧縮 RGBA、PAM P7）、`provenance.json`（元 FBX の SHA-256、変換器版、縮小規則、著作権者） |
| ビューア | `userland/base/mview/`（`/bin/mview`） | Wayland＋Vulkan クライアント（wltest と同じ構成: `VK_KHR_wayland_surface`、xdg-shell）。モデル読込み、material ごとの描画、入力 |
| shader | `userland/base/mview/shaders/` | GLSL と、オフラインで生成した SPIR-V（vkdemo と同じ provenance 方式）。centris に glslc を入れる |
| zwl 入力 | `userland/base/zwl/` | evdev（`/dev/input/event*`、Xzed と同じ読み方）→ `wl_seat` v1 / `wl_pointer`（enter/leave/motion/button/axis）/ `wl_keyboard`（keymap は no_keymap、evdev keycode、modifiers） |
| client 入力 | `userland/base/libwayland/`、`libc/include/wayland/` | `wl_seat`・`wl_pointer`・`wl_keyboard` の proxy と event 配送 |
| 試験 | `plan/ws031/tests/mview-venus.py`、`plan/ws031/tests/config-mview-amd64.mk` | Venus QEMU（WS014 の wayland-qemu.py を基に）、`usb-tablet`（USB HID を有効化）、QMP `input-send-event` で入力を流し、VNC で画面を取得 |

## テキスト表現（案）
```
mview 1
texture <index> <file.pam>
material <index> <name> texture <index|-> alpha opaque|cutout|blend cull back|none color <r> <g> <b> <a>
mesh <name> material-ranges <count>
v <x> <y> <z> <nx> <ny> <nz> <u> <v>
t <material> <i0> <i1> <i2>
```
頂点は mesh 内で重複を除いて index 化する。座標は FBX の UpAxis/UnitScaleFactor を正規化して右手系 Y-up・メートル単位で書く。

## ビューアの機能
- 描画: depth test、material ごとの draw（不透明 → cutout（discard）→ blend の順、blend は奥から手前へ）、diffuse テクスチャ＋簡単な
  平行光（Lambert＋ambient）。mipmap を生成する。
- 操作: 左ドラッグ＝回転（orbit）、右または中ドラッグ＝平行移動、ホイール＝拡大縮小。キーボード: `R` 初期視点、矢印＝回転、
  `+`/`-`＝拡大縮小、`Q`/Esc＝終了。
- 画面: zwl の `--width`/`--height`（試験は 640×480）。起動時にモデル全体が収まる視点。
- 試験用: `--token`、フレームごとに `MVIEW FRAME` 行、入力を受けるたびに `MVIEW INPUT` 行（視点パラメータを出す）。

## 受け入れ条件と試験（Venus）
1. 変換器: 同じ FBX から毎回同じ出力（byte 一致）、頂点数・三角形数・material 数が FBX と一致。
2. zwl/libwayland: host 試験で `wl_seat`/`wl_pointer`/`wl_keyboard` の wire を確認。既存の wltest 受入（FIFO/MAILBOX）が回帰なし。
3. 実 QEMU（Venus）: ビューアがモデルを表示し、VNC 画像で確認（初期視点の画像をユーザーが目視で承認し、以後の比較の基準にする）。
   QMP の入力で、回転・移動・拡大縮小・`R` によって画像が変わり、`R` で初期画像へ戻る（画素一致）。
4. zwl の入力: pointer の enter/motion/button/axis と keyboard の key が client に届く（ログと画像で確認）。
5. build warning 0、`plan/coding-style.md` 準拠、`git diff --check` PASS。

## 依存・触れないファイル
- 前段: WS014 p006（zwl/libwayland/wltest）、Venus 環境。
- 触れない: カーネル（入力 driver は既存の evdev をそのまま使う）、UAPI、i915 driver（p014 で扱う）。

## 見積・制限
変換器 半日、zwl/libwayland 入力 1 日、ビューア 1 日、試験 半日。skin・blend shape・MToon 風の見た目は対象外。

---

# WS031 p014 計画（概要）: モデルビューアを i915 で動かす

p013 のビューアを QEMU＋passthrough（5330）の i915 で動かす。ビューアが必要とする順に i915 の shader compiler と executor を
拡張し、各段で小さな試験 shader を実機で確かめてからビューアへ進む。詳細は p013 完了後に書く。

- executor: `BindIndexBuffer`/`DrawIndexed`、uniform buffer、fragment の push constant、viewport/scissor、blending、mipmap、
  command buffer あたり 64 操作の上限の撤廃。
- compiler: 行列（`OpTypeMatrix`/`MatrixTimesVector`/stride）、FDiv、比較と Select、分岐とループ、`OpKill`（discard）、
  normalize/max/clamp/pow/mix、varying 4 本以上、register spill。
- 試験環境: `run-parity-vk.sh` に QMP（`input-send-event`）を足す。人の操作は `usb-host` で実マウスを渡す。
- 性能（draw ごとの同期 batch、同期 present）は正しさの後。

## 完了（2026-09-22、Venus）
- 変換器 `userland/base/mview/tools/fbx2mview.py`: qs40 を 4 mesh・25,861 頂点・37,000 三角形・material 16・texture 13（最大 1024、PAM）へ。
  出力 34 MB、再実行で byte 一致。形式は `userland/base/mview/models/README.md`。
- zwl/libwayland: `wl_seat` v5・`wl_pointer`・`wl_keyboard`（evdev、絶対/相対 pointer、wheel、NO_KEYMAP）。host 試験 PASS（通常＋ASan/UBSan）。
- mview: material ごとの `vkCmdDrawIndexed`（opaque → cutout → blend）、mipmap、Lambert＋ambient、push constant のみ。host 試験 91/91。
- 実 QEMU（Venus、5330 の iGPU を host i915 に切替）: `plan/ws031/tests/run-mview-remote.py` attempt `p013-mview-009` PASS。
  model_visible・drag_rotates・right_drag_pans・wheel_zooms・keys_turn・reset_restores_first_frame（初期画像と画素一致）すべて真、
  `ZWL EXIT ... error=0 cleanup_failed=0 input_events=279`。証跡 `plan/ws031/temp/remote/p013-mview-009/`。
- 回帰: WS014 Wayland 受入（wltest FIFO/MAILBOX、`p013-wayland-regress-001`）PASS、i915 passthrough の `vkloop-hw.sh wayland` PASS。

### 分かったこと・制限
- Venus は q312 で作った virglrenderer（`dependencies/q312-quiesce`）が要る。既定の 1.1.0-2 では物理デバイスが 0 になる。
  `--render-server …/q312-quiesce/install/libexec/virgl_render_server --renderer-library-dir …/install/lib/x86_64-linux-gnu`。
- 5330 の iGPU は既定で vfio-pci。Venus の試験中だけ `~/bigbang/igpu-mode.sh host` で host i915 に切替え（awe へ renderD128・/dev/kvm の
  実行時 ACL）、終了後に `vfio` へ戻す（runner が自動で行う。`vkloop-hw.sh` も開始前に vfio を確かめる）。
- 鍵盤: QMP の key event は USB keyboard（`usb-kbd`）経由で届く。PS/2 keyboard 経由では zwl に届かなかった（原因未調査、XXX）。
  `input-send-event` に `device` を付けると QEMU 10.0.11 が egl-headless で abort する（`qemu-fixed-text-console.device`）。
- 見た目: sRGB 変換なし（UNORM surface）、blend は最後に描くが奥→手前の並べ替えなし（qs40 に blend material はない）。
