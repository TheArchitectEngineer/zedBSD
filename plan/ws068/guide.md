# WS068 作業の手引き（2026-10-01、Q1 の survey）

この file だけを読めば WS068 を続けられるように書いた。記録の本体は [ws.md](ws.md)・[design.md](design.md)・[glsl-design.md](glsl-design.md)・
各 phase.md で、ここは要約と手順。食い違ったら ws.md・phase.md が正しい。command は全て **repo の root（`/home/awe/zedBSD-claude1`）から**実行する。
`<W>` は作業の名前（例 `ws068-p038`）に置き換える。

## 1. ゴール

### 目標（ws.md「目標」、2026-09-26 ユーザー）

EGL と OpenGL ES（2.0、3.0）と desktop GL（GLX、3.0〜3.2）を、zedBSD の libvulkan と `VK_KHR_display` の上に実装する。Wayland の窓と
display 直接の両方。GPU を叩くのは libvulkan だけ（GL の別の GPU 経路は作らない）。GLSL は自前の compiler（2026-09-27 ユーザー決定 方式 A）。

### 範囲の決定（ユーザー）

| 日付 | 決定 | 影響 |
| --- | --- | --- |
| 2026-09-27 | 「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」 | GL 3.3 以降（p037・p034〜p036）は**保留**。再開はユーザーの指示で p037 から |
| 2026-09-30（master「WS の優先順位」4） | WS068 は「GL 3.2 まで。3.3 以降は保留」でデモ critical の残り | 新しい API は足さない。残りは i915 の確認・frame の重ね・規約 |
| 2026-09-30（WS101 の D1） | libglesv2 は compute を持つ device で「OpenGL ES 3.1」を名乗る。3.1 の compute 以外は `GL_INVALID_OPERATION` の stub（`userland/desktop/libglesv2/es31.c` の先頭の comment） | WS068 の「実装した版を名乗る」方針の例外。egltest の feedback の版の検査は 3.1 も受ける（`userland/desktop/egltest/feedback.c:177`） |
| D4（main、2026-09-30） | GLES 3.1 の compute の部分（GLSL ES 3.10 の compute・`glDispatchCompute`・SSBO・`glMemoryBarrier`）は WS101 へ移した | p035 は desktop GL の compute・image load/store・atomic counter だけ |

### 受け入れ（ws.md「受け入れ」）と今

| # | 受け入れ | 今 |
| --- | --- | --- |
| 1 | Wayland（Wiseman Mode とゲームモード）と display 直接で EGL＋GLES 2.0 の試験（三角形、texture、blend、depth、resize）が画面の読み取りで一致（Venus） | 済み（p002・p008・p010・p022・p023） |
| 2 | GLES 3.0 の代表機能の試験 | 済み（Venus。p019〜p030） |
| 3 | i915 実機で同じ試験（QEMU と実機の証拠を分ける） | **一部だけ**: GLX の zgears（p006、passthrough の capture）、egltest feedback・queries（ws101-p010・p007 の `gles-hw.sh`、passthrough）。**draw・glsl・glsl3・fbo・cube・es3・formats・volumes・targets・blits と glxtest --gl3/--gl31/--gl32 は i915 で未実施** |
| 4 | 規約・回帰 | 未実施（p007） |

### fg010 との関係

fg010 の台本に WS068 の名指しの場面は無い。デモで GL を使うのは App Home の Gears（`zgears`、GLX の固定機能。`plan/ws035/demo/apps.conf`）と
WS101 の S13（libglesv2 の compute）。どちらも i915 の passthrough で動いた記録がある（p006、ws101-p015）。

### 完了の定義（WS068 を completed にする条件）

1. 受け入れ 3: i915 で GLES 2.0・3.0 と GL 3.0〜3.2 の試験が通る、または通らない物が Bug・Future Work に理由付きで移され、main・ユーザーが受け入れる。
2. p009（frame を重ねる）と p004（GLES 2.0 の残り）が cleared、または main の判断で範囲から外される。
3. p007（規約の全文との照合と回帰）が cleared。
4. 保留の p037・p034〜p036 の扱い（WS の受け入れから外して別の WS か Future Work に移すか）が main・ユーザーの判断で決まる。**人間の判断**。

## 2. 今の状態（2026-10-01）

### 済み（cleared）

| Phase | 内容 | 証拠（全て Venus。i915 は別記） |
| --- | --- | --- |
| p001 | 設計 | [design.md](design.md) |
| p002・p008・p010 | EGL の核・GLES 2.0 の描画の核・pbuffer | egl-p002・p008・p010 |
| p015〜p019 | GLSL compiler（前処理〜SPIR-V・libGLESv2 への接続） | host の glsl-host（2026-10-01 再実行 PASS）、egl-p019 |
| p020・p021 | GLSL 1.40〜3.30・ES 3.00、uniform block | glsl-host、egl-p020 |
| p022〜p030 | FBO・cube・ES 3.0 の API（VAO・UBO・format・3D・MRT・blit・MSAA・query・sync・transform feedback） | egl-p022〜p030 |
| p013・p031・p032・p033 | desktop GL 3.0・3.1・geometry shader・3.2 | glx-p013・p031・p033 |
| p006 | i915（passthrough）で GLX の zgears ほか 6 検査 | [phase006](phase006/phase.md)。BUG-057 は resolved |

### 開いているもの

| Phase | Status | 内容 |
| --- | --- | --- |
| p009 | planned（phase.md なし → 2026-10-01 に [phase009](phase009/phase.md) を作った） | EGL の frame を 2〜3 枚重ねる（Venus で clear だけ 125 ms/frame、WSI 直接は 50 ms） |
| p004 | planning（phase.md なし） | GLES 2.0 の残りと試験の充実。範囲が未定（下の 4 章で決め方） |
| p007 | planning（phase.md なし → 2026-10-01 に [phase007](phase007/phase.md) を作った） | 規約の全文との照合と回帰（最後） |
| p037 | planned・**保留** | desktop GL 3.3（[phase037](phase037/phase.md) に設計の下書き）。ユーザーの指示まで触らない |
| p034・p035・p036 | planning・**保留** | tessellation、desktop の compute ほか、GL 4.x |

### bug

| Bug | 状態 | WS068 との関係 |
| --- | --- | --- |
| [BUG-057](../bugs/BUG-057.md) | resolved（ws069-p010） | i915 で zgears が数百 frame の後に止まる。再発したら reopen |
| [BUG-058](../bugs/BUG-058.md) | tracking | i915 で App Home の zgears が最初の frame の前に黙って終わる（1 回、再現せず） |
| [BUG-056](../bugs/BUG-056.md) | resolved | zwl が client の後片付けの後に終わる（ws068-p006 の run で 17 回中 3 回） |

Future Work: F-021（Venus の窓の frame の時間。p009 で一部を隠せる）、F-023（i915 の実行器・compiler の不足: `gl_VertexIndex`・`gl_FragCoord`・
OpSwitch・OpCompositeInsert・triangle strip ほか。deferred）、F-030（GLX の画像を GPU の buffer のまま渡す）。

## 3. 次の作業の順番

### 3.1 順番（既存の ID と提案）

| 順 | Phase | 目的 | 依存 | 完了の条件 |
| --- | --- | --- | --- | --- |
| 1 | **提案 ws068-p038**（3.2） | 受け入れ 3: i915（5330 の passthrough）で egltest の全 scene と glxtest --gl3/--gl31/--gl32 | なし（i915 の今の実行器） | 3.2 の表 |
| 2 | [ws068-p009](phase009/phase.md) | EGL の frame を 2 枚重ねる | p008 | phase009 の「完了の条件」 |
| 3 | ws068-p004 | GLES 2.0 の残り（範囲を先に決める。4 章） | p003（済み） | 範囲を決めた phase.md による |
| 4 | [ws068-p007](phase007/phase.md) | 規約の全文との照合と回帰 | 全 Phase（保留の物を除く範囲は main の判断） | phase007 の「完了の条件」 |
| 保留 | p037 → p034・p035 → p036 | GL 3.3〜4.6 | ユーザーの再開の指示 | — |

WS068 はデモ critical の残り（master「WS の優先順位」4）。2026-10-10 ごろ以後は bug の修正と実機の調整だけ（master）なので、
p009・p004 の新しい実装は 10/10 までに入れるか、10/17 の後に回す（main の判断）。

### 3.2 提案（main の承認が要る。ID は次の番号 p038）

| 提案 | ID | 目的 | 触る file | 受け入れ |
| --- | --- | --- | --- | --- |
| A | ws068-p038 | i915 の passthrough で GLES 2.0・3.0 と GL 3.x の既存の試験の program を走らせ、受け入れ 3 の証拠を取る。新しい API は足さない。i915 の compiler・実行器が断る scene は Bug にする（直すのは WS075・WS031 か新しい Phase） | 新: `plan/ws068/tests/hw/es-hw.sh`・`plan/ws068/tests/hw/es/`（`config.mk`・`run-es.sh`・`rc.conf`・service の file）。`plan/ws101/tests/hw/gles-hw.sh`・`gles/` の複写を元に作る。source は触らない | 下の「p038 の手順」の判定で、全 scene の `EGLTEST CHECK … failures=0 glerror=0x0` と `EGLTEST DONE … failures=0`、glxtest の `GLXTEST GL32 ready failures=0` ほか。通らない scene は Bug Ticket（症状・scene・log の行）と Bug Board の行 |
| B（判断） | — | 保留の p037・p034〜p036 を WS068 の受け入れから外すか（WS068 を「GL 3.2 まで」で完了にできるようにする）。**人間の判断** | ws.md（main） | 決定が ws.md に記録される |

#### p038 の手順（案。Phase を作る時に phase.md へ写す）

1. 複写（`<W>` = `ws068-p038`）:
   ```
   mkdir -p plan/ws068/tests/hw/es
   cp plan/ws101/tests/hw/gles-hw.sh plan/ws068/tests/hw/es-hw.sh
   cp plan/ws101/tests/hw/gles/run-zdesktop.sh plan/ws101/tests/hw/gles/poweroff plan/ws101/tests/hw/gles/rc.conf plan/ws068/tests/hw/es/
   cp plan/ws101/tests/hw/gles/gles1 plan/ws068/tests/hw/es/es1
   ```
2. `plan/ws068/tests/hw/es/config.mk`: `include plan/ws031/tests/config-zdesktop-hw.mk`（egltest・glxtest・libgl・xserver・zgears を含む）と
   `ZEDBSD_USER_PROGRAMS := $(filter-out browser files libz-compat libpng-compat noct libcxx clang,$(ZEDBSD_USER_PROGRAMS))`（**noct を外す: toolchain**）。
3. `plan/ws068/tests/hw/es/run-es.sh`（`plan/ws101/tests/hw/gles/run-gles.sh` の `run()` の形）: compositor の窓で
   `egltest --display=/tmp/wayland-0 --size=640x400 --scene=S --frames=60 --delay-ms=30 --token=S` を S = draw glsl glsl3 fbo cube es3 formats volumes targets blits queries feedback の順に
   （log は `/var/log/egl-S.log`）、pbuffer の `egltest --platform=pbuffer --scene=draw --frames=60 --token=pb`、
   xserver（`DISPLAY=:0 /bin/xserver &`、`plan/ws068/tests/glx-p033.sh:51` の形）の下で `glxtest --gl3`・`--gl31`・`--gl32`（各 `--frames=60 --delay-ms=30 --token=gN`）。
   最後に `echo "ES HW DONE" >> /var/log/es-ps.log`、`dmesg > /var/log/dmesg.log`、`sync`。
4. `es-hw.sh`: `D=plan/ws068/tests/hw/es`、service の名前（`gles1` → `es1`、`run-gles.sh` → `run-es.sh`）、`ZEDBSD_TEST_IMAGE_TAG=ws068es`、
   既定 `BUILD=build/ws068-p038-hw`、出力 `build/ws068-p038-es-hw`、fonts は `build/ws035-fonts`（repo の相対。**`/home/awe/zedBSD-rpi4` は無い**）、
   `ufs-cat.py` で読む log の一覧、判定の grep を egltest・glxtest の行に替える。
5. `bash -n plan/ws068/tests/hw/es-hw.sh`、`ES_HW_BUILD_ONLY=1` に当たる変数で build だけ → 実機の run（lock は script が取る）:
   ```
   BUILD=build/ws068-p038-hw plan/ws068/tests/hw/es-hw.sh build/ws068-p038/es-hw
   ```
6. scene ごとの結果の表を phase.md に。i915 の compiler が断った shader は executor の log（guest の `/var/log/zdesktop.log` か egltest の log の理由の行）を写し、
   host で `plan/ws068/tests/i915-shader-check/run.sh` の形（`build/<W>/check vertex|fragment SPV…`）で同じ shader を通して理由を確かめる。

## 4. 未知と調べ方

| 未知 | 調べ方 |
| --- | --- |
| i915 の compiler・実行器が egltest・glxtest のどの shader と state を断るか（F-023 の不足: `gl_FragCoord`・`gl_VertexIndex`・OpSwitch・OpCompositeInsert・triangle strip ほか） | host で先に見る: 各 scene の shader（`userland/desktop/egltest/shaders/`、GLSL の scene は自前の compiler の出力）を `plan/ws068/tests/i915-shader-check/main.c` で compile する。`i915-shader-check/run.sh` が fixed・scene の shader を通す手本（run.sh:13〜23）。GLSL の scene は `plan/ws068/tests/glsl-host/run.sh` の `I915_PAIRS`（環境変数、既定 `scene fixed scene300`、run.sh:87）に pass/ の対の名前を足して見る。最後は p038 の実機の run |
| p009: frame を重ねた時の libGLESv2 の garbage と `used == frame` の約束 | libegl は今 1 frame ずつ submit して fence を待つ（`userland/desktop/libegl/vulkan.c:1162` `vulkan_submit`、待ちは 1228 行）。command buffer・fence・semaphore は surface に 1 組（`userland/desktop/libegl/zegl.h:158〜163`）。libGLESv2 は frame の終わりを `frame_done` の callback（`zegl.h:209`）と `gles_frame_wait`（`userland/desktop/libglesv2/query.c:147`、`query_finish`）で知り、garbage（`userland/desktop/libglesv2/buffer.c:242` `gles_garbage_destroy`、290 `gles_collect`）と spare（WS101 p017、`gles.h:151〜159`）を frame の番号で片付ける。重ねる前に「frame N が終わった」を fence ごとに知らせる形にできるか読む。WS101 の compute（pbuffer）も同じ道を通るので、glescompute の回帰が要る |
| p009: 効果 | Venus では clear だけで 125 ms/frame、WSI 直接 50 ms（phase008 の 87 行）。Venus の同期は約 10 ms 刻み（F-064）なので、効果は実機で別に測る。`egltest --log-frames` 相当の時間の行が無ければ、egltest の `--frames=N` の全体の時間を guest の `time` で測る |
| p004 の範囲 | 決まっていない。候補の集め方: (1) `gl_FragCoord` の y の向き（phase019 の 57 行「GL の向きには framebuffer の高さの隠れた uniform が要る」）、(2) `grep -n "GL_INVALID_ENUM\|GL_INVALID_OPERATION" userland/desktop/libglesv2/*.c` で GLES 2.0 の entry point が断る値を拾い、ES 2.0 の仕様（Khronos の ES 2.0.25 の state table）と比べる、(3) Khronos の dEQP は使っていない（外部 package の取り込みが要る: AGENTS.md の tarball の規則とライセンスの監査）。範囲を phase.md に書き、main の承認を得てから実装 |
| GL 3.3（再開の時） | [phase037](phase037/phase.md) の「設計の下書き」に file ごとの変更点がある（libegl の timestampValidBits・dualSrcBlend、query.c の timestamp の pool、GLSL の `layout(index)`、draw.c の SRC1、gl3.c の `glVertexAttribP*`、glx.c・fixed.c の版） |
| GLSL compiler の誤り（再発時） | `plan/ws068/tests/glsl-host/run.sh`: pass/ の compile、fail/ の `// expect:`、対の link と `spirv-val --target-env vulkan1.0`、i915 の host の compile、exec/ の lavapipe の描画の色（`VK_DRIVER_FILES` は lavapipe）。設計は [glsl-design.md](glsl-design.md) §8 |
| SPIR-V の反射・gl_Position の書き換え | `plan/ws068/tests/spirv-host/run.sh`（`userland/desktop/libglesv2/spirv.c`、glslc と spirv-val と spirv-dis） |

## 5. コマンド

general な build・boot test は [plan/ws104/commands.md](../ws104/commands.md) の §0（守ること）・§1（build）・§3（1 つの library）・§4（boot test）に従う。
以下は WS068 の物。script の行番号は 2026-10-01 に読んだ物。

### 5.1 host の試験（GPU・QEMU なし。2026-10-01 に PASS: glsl-host 25 秒、spirv-host 1 秒）

```
sh plan/ws068/tests/glsl-host/run.sh build/<W>/glsl-host       # 最後の行 "glsl-host: PASS"
sh plan/ws068/tests/spirv-host/run.sh build/<W>/spirv-host     # "spirv-host: PASS"
sh plan/ws068/tests/i915-shader-check/run.sh build/<W>/i915-shaders   # shader ごとに "accepted" か "REFUSED by …"（main.c:93〜112）。PASS の行は無い
sh plan/ws101/tests/glsl/run.sh build/<W>/ws101-glsl           # libglesv2 の GLSL を変えたら WS101 の compute も "ws101 glsl compute host test PASS"
sh plan/ws101/tests/gles/run.sh build/<W>/ws101-gles           # "ws101 gles host test PASS"
```

要る host の道具: `cc`、`glslc`、`spirv-val`、`spirv-dis`、lavapipe（`/usr/share/vulkan/icd.d/lvp_icd.json`）。

### 5.2 library と program だけの build

```
make -j16 ZEDBSD_CONFIG=plan/ws068/tests/config-amd64-glsl.mk BUILD=build/<W>-lib \
    build/<W>-lib/dynamic/libEGL.so build/<W>-lib/dynamic/libGLESv2.so build/<W>-lib/bin/egltest
python3 plan/tools/style-check.py userland/desktop/libegl/*.c userland/desktop/libglesv2/*.c   # 実行 bit が無いので python3 で
git diff --check
```

`config-amd64-glsl.mk` は noct・clang・libcxx を外している（config-amd64-glsl.mk:7）。

### 5.3 QEMU の Venus

image（**他の image の build と同時にしない**。既定は `build/ws068-glsl`、build-glsl-image.sh:10。必ず自分の BUILD を渡す）:

```
plan/ws068/tests/build-glsl-image.sh build/<W>-glsl > build/<W>/img-build.log 2>&1; echo "exit=$?"
```

guest の起動と試験。**試験の script ごとに既定の `GUEST_RUNTIME` が違う**（egl-p002〜p025 は `build/ws035-sq-run`、p019・p020 は `build/ws068-glsl-run`、
p026〜p030・glx-p013〜p033 は `build/ws068-run`。各 script の 13〜18 行）ので、起動と全ての試験に同じ `GUEST_RUNTIME` を export する:

```
sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128      # host の起動ごとに 1 回
export GUEST_RUNTIME=$PWD/build/<W>-run
sh plan/ws035/tests/zdesktop-guest.sh start build/<W>-glsl/hdd-image.img
sh plan/ws035/tests/zdesktop-guest.sh wait --timeout 240         # "guest: ready"
for t in egl-p008 egl-p010 egl-p019 egl-p020 egl-p022 egl-p023 egl-p024 egl-p025 egl-p026 egl-p027 egl-p028 egl-p029 egl-p030 glx-p013 glx-p031 glx-p033; do
    sh plan/ws068/tests/$t.sh build/<W>/$t > build/<W>/$t.txt 2>&1; echo "$t exit=$?"
done
sh plan/tools/x11/x11-p004.sh build/<W>/x11-p004
sh plan/tools/x11/x11-p005.sh build/<W>/x11-p005
sh plan/ws035/tests/zdesktop-guest.sh stop
```

- `zdesktop-guest.sh start` の `--symbols` は `build/ws035-sq/vmunix` 固定（zdesktop-guest.sh:55）。symbol が要る解析では `python3 plan/tools/guest/guest.py start IMAGE --symbols build/<W>-glsl/vmunix …` を直接使う。
- 判定: 各 script の `exit=0`（`status` を返す）と、`log: … MISSING` の行が無いこと、撮った PNG（`build/<W>/<t>/*.png`）を目で確かめる。
- `egl-p002.sh` は zdesktop の窓の dock（double click）を含む。WS068 の回帰では p008 以降で足りる（p002 の clear は p008 が含む）。

### 5.4 実機（5330 の QEMU VFIO passthrough）

全て `flock /tmp/i915-hw.lock` を内部で取る。**2 つの実機の試験を同時に走らせない。lock を手で取らない。** `I915_HOST` の既定は `solaris10-man`。

今ある WS068 に効く実機の試験（WS101 の script。egltest feedback・queries を含む）:

```
BUILD=build/<W>-gles plan/ws101/tests/hw/gles-hw.sh build/<W>/gles-hw      # "gles-hw: PASS"
```

- `GLES_HW_BUILD_ONLY=1` で build だけ（gles-hw.sh:32）。fonts の既定 path（gles-hw.sh:19）は旧 `/home/awe/zedBSD-rpi4` で、無ければ font 無しで進む。

GLX の zgears（p006 の capture の scenario `zdesktop-x11`）: [phase006](phase006/phase.md) の手順（`plan/ws031/tests/i915-capture.py`、`plan/ws031/tests/vkloop-hw.sh`）。

p038 を作った後: `BUILD=build/ws068-p038-hw plan/ws068/tests/hw/es-hw.sh build/ws068-p038/es-hw`（3.2 の手順）。

## 6. 実機（Dell Latitude 5330）

一般の手順（USB への書き込み、起動、ssh、写真、lock）は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（作成中。2026-10-01 時点で未作成）を見る。

| 確認 | 形 | 出力 |
| --- | --- | --- |
| GLES・GL の scene（受け入れ 3） | passthrough の p038（上）。素の機械は要らない（受け入れは i915 の実行器で、passthrough と素の機械で実行器は同じ）。素の機械で要るかは main の判断 | guest の disk の log の EGLTEST・GLXTEST の行（`ufs-cat.py`） |
| デモの Gears | 素の 5330 の demo の image で App Home → Gears（ユーザーの操作、写真）。WS099・WS075 の demo の確認に含まれる | 歯車が回る。止まれば BUG-057・BUG-058 を見る |

- 記録は「素の 5330」「passthrough」「QEMU（Venus）」を分けて書く。やっていない確認は「未実施」。

## 7. 注意

- **保留を守る**: p037・p034〜p036（GL 3.3 以降）はユーザーの指示まで実装しない。p035 の GLES 3.1 の compute は WS101 の物。
- **libglesv2・libegl は WS101 と共有**。変えたら WS101 の host の試験（5.1）と `plan/ws101/tests/gles/venus.sh`（WS101 の guide の 5.3）も走らせる。
  frame の扱い（p009）は compute の pbuffer の道（`gles_frame_wait`・garbage・spare）に効く。
- **Noct**: WS068 の試験の構成に noct を入れない（`config-amd64-glsl.mk` は外している。新しい構成も `$(filter-out noct,…)`）。noct を含む構成で
  `ZEDBSD_NOCT_ACCEL := y` だと Noct の target が build される（toolchain、main の許可が要る）。
- **toolchain の lock**（`plan/tools/toolchain-lock.sh`）を外さない。外部の source（dEQP 等）を入れるなら tarball・検証・patch・ライセンスの監査（AGENTS.md）。
- **image の build を 2 つ同時に走らせない**。BUILD は自分の `build/<W>-*`。`boot-test.sh` の `OUTPUT` は専用の dir。
- **実機の試験は 1 つずつ**。lock を持つ script を `timeout` で包まない。
- **QEMU の console・serial の log で判定しない**。判定は script の PASS・`log: … ok` の行・guest の disk の log・PNG。
- Venus の時間は実機の参考にならない（F-021・F-064）。
