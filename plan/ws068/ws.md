<!-- awesome-plan project=zedbsd record=ws068 -->

# WS068: EGL と OpenGL ES を Vulkan と display 拡張の上に実装する

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG006
Related Milestones: MG002
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: none
Resume point: p033（desktop GL 3.2）cleared（2026-09-27、Venus で glx-p033 と回帰 PASS。i915 未実施）。GL 3.3 以降（p037・p034〜p036）は保留（2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」）。再開はユーザーの指示で p037 から（[phase037](phase037/phase.md) に設計の下書き）。p009・p004・p007 は残り、i915 の高度化（WS031 等）の後
<!-- awesome-plan-current:end -->
作業の手引き（2026-10-01）: [guide.md](guide.md)

## 目標

2026-09-26 ユーザー: 「では、Vulkanとディスプレイ拡張の上に、EGLとGLESを実装するWSを作っておいてください。」（直前の質問: EGL+OpenGL ES のコードをこの Wayland で動かすときの、libGLESv2/v3 の移植（`/dev/gpu0` を直接使う方式と libvulkan で変換する方式）と、Wayland とディスプレイ直接の両方で使える EGL の実装の費用）

EGL と OpenGL ES（2.0、次に 3.0）を、zedBSD の libvulkan（Vulkan）と `VK_KHR_display`（display 拡張）の上に実装する。EGL + GLES で書かれた Linux 等のアプリの source を、Wayland（zwl・Wiseman Mode）の窓と、compositor の無い display 直接（全画面）の両方で動かす。GPU を叩くのは libvulkan だけにし、GL のための別の GPU 経路（`/dev/gpu0` の直接の GL 実装、virgl の GL 経路、i915 の GL driver）は作らない。

## 方式の前提（2026-09-26 の見積もりの回答から。p001 で確定）

| 部分 | 方式 | 見積もり（目安） |
| --- | --- | --- |
| EGL | 自前。`EGLSurface` → `VkSurfaceKHR`（Wayland: `VK_KHR_wayland_surface`、display 直接: `VK_KHR_display`）＋ swapchain、`eglSwapBuffers` → present。config・context・pbuffer。`libwayland-egl`（`wl_egl_window`） | 小（Phase 2〜3 個）。EGLImage は後 |
| GLES 2.0 | 自前の変換層（GL の状態 → Vulkan の pipeline の cache、GLSL ES → SPIR-V は glslang（BSD）の移植）、または Mesa の Zink の移植（MIT）。p001 で比べる | 中 |
| GLES 3.0 | 2.0 の延長（UBO、instancing、MRT、transform feedback、format） | 大 |
| ANGLE | Chromium（WS035 の最後）が使う。GLES＋EGL を Vulkan の上に持つが、C++・GN の build・Vulkan 1.1 相当の要求。Chromium の着手の時に再検討 | 大〜特大 |

採らない方式: `/dev/gpu0` の上の GL の直接実装（Venus の QEMU では virgl の GL の経路と Mesa の virgl driver、i915 では DRM の無い上に iris 相当か第 2 の実行器が要り、backend ごとに二重の保守になる）。

## 依存と制約

- libvulkan の機能: 今は Vulkan 1.0 core と WSI。GLES の変換層・Zink・ANGLE の要る拡張（maintenance1、dedicated allocation 等）と Vulkan 1.1 を足す必要があるかを p001 で洗う。
- i915 の実機: ネイティブ実行器の不足（[F-022](../future-work.md): context の間の画像の共有、[F-023](../future-work.md): builtin・OpSwitch・triangle strip・vkFreeDescriptorSets・fence 等）が GLES の shader と Wayland の窓で効く。Venus（QEMU）では libvulkan が host へ転送するので先に通せる。
- 外部の source（glslang、Mesa 等）は `userland/packages/` に tarball で取得・検証・patch し、ライセンスを監査する（AGENTS.md）。
- EGL・GLES の header は Khronos の公開 header（Apache-2.0 / MIT 系、監査する）。

## 受け入れ（p001 で確定する）

1. Wayland（zwl の Wiseman Mode とゲームモード）と display 直接の両方で、EGL＋GLES 2.0 の試験アプリ（三角形、texture、blend、depth、窓の resize）が描け、画面の読み取りで一致する（Venus）。
2. GLES 3.0 の代表機能の試験。
3. i915 実機（F-022・F-023 の後）で同じ試験。実機の証拠と QEMU の証拠を分ける。
4. 規約・回帰。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws068-p001](phase001/phase.md) | 設計: library の構成（`libEGL.so`・`libGLESv2.so`・`libwayland-egl.so`）、GLES の方式の比較（自前の変換層＋glslang / Zink / ANGLE）、libvulkan に要る機能、試験アプリ、ライセンス（[design.md](design.md)） | cleared（q470-i01。GLES の方式はユーザーの判断待ち） | — |
| [ws068-p002](phase002/phase.md) | EGL の核と `libwayland-egl`、display 直接の platform（最初は clear だけの GLES で疎通） | cleared（q471-i01、2026-09-26。Venus で Wayland と display 直接の clear） | p001 |
| ws068-p003 | 自前の GLSL compiler（C、2026-09-27 方式 A）の共通の核: 前処理・字句・構文・型検査・SPIR-V の出力。GLSL ES 1.00 と GLSL 1.30、`glShaderSource`・`glCompileShader` へ接続（i915 の受ける形） | 分割（2026-09-27、p015〜p019 に分けた。p019 の clear で満たした（2026-09-27）。設計 [glsl-design.md](glsl-design.md)） | p008、p006 |
| [ws068-p015](phase015/phase.md) | GLSL compiler の設計（[glsl-design.md](glsl-design.md)）と p003 の分割 | cleared（2026-09-27） | p008 |
| [ws068-p016](phase016/phase.md) | GLSL compiler: 前処理・字句・構文（AST）と host の試験 | cleared（2026-09-27。host の試験 PASS） | p015 |
| [ws068-p017](phase017/phase.md) | GLSL compiler: 型・意味解析・定数の畳み込み・built-in の宣言 | cleared（2026-09-27。host の試験 PASS） | p016 |
| [ws068-p018](phase018/phase.md) | GLSL compiler: SPIR-V の出力と link（i915 の受ける形、spirv-val・lavapipe の実行・i915 の host の検査） | cleared（2026-09-27。spirv-val、lavapipe の 15 試験、i915 の host 検査 PASS） | p017 |
| [ws068-p019](phase019/phase.md) | GLSL compiler を libGLESv2 へ接続（`glShaderSource`・`glCompileShader`・`glLinkProgram`）、egltest の GLSL の場面、Venus の試験と回帰 | cleared（2026-09-27。Venus で egl-p019 PASS、回帰 PASS。i915 実機は未実施） | p018 |
| ws068-p012 | GLSL 3.30・ES 3.00（in/out、layout、UBO、整数） | 分割（2026-09-27、p020・p021 に分けた。両方 cleared（2026-09-27）。[glsl-design.md](glsl-design.md) §11） | p003（p019） |
| [ws068-p020](phase020/phase.md) | GLSL 1.40〜3.30・ES 3.00 の言語（`#version`、`layout(location)`、in/out の block、整数の varying、複数の出力、新しい sampler と built-in） | cleared（2026-09-27。host の試験 PASS、Venus で egl-p020 PASS（ES 3 の context）。i915 実機は未実施） | p019 |
| [ws068-p021](phase021/phase.md) | GLSL の uniform block（std140、row_major、binding の約束）と libGLESv2 の反射の対応（API は p005・p013） | cleared（2026-09-27。host で std140 の offset を lavapipe で確認。libGLESv2 は API（p005）まで断る） | p020 |
| [ws068-p013](phase013/phase.md) | desktop GL 3.0 の context（`glXCreateContextAttribsARB`、core と compatibility の profile、VAO、GL 3.0 の API） | cleared（2026-09-27。Venus で glx-p013 PASS、回帰 x11-p004・p005・egl-p022〜p030・boot test PASS。i915 実機は未実施） | p003、ws069-p008 |
| ws068-p014 | desktop GL 3.1〜4.6 の出来る範囲（Venus 先。geometry・tessellation・compute・SSBO は device の feature で。i915 の不足は F-023） | 2026-09-27 に p031〜p037 に分けた（GL_VERSION は実装した版を名乗り、上の版の機能は GL_ARB_* で個別に出す。design.md §6） | p012、p013 |
| [ws068-p031](phase031/phase.md) | desktop GL 3.1: texture buffer（glTexBuffer、samplerBuffer）、rectangle texture（sampler2DRect）、glPrimitiveRestartIndex、glGetActiveUniformName、GLSL 1.40 と context の版による GLSL の版の上限、3.1 の context（GL_ARB_compatibility）。3.2 の shader の stage の要らない API（glDraw*BaseVertex、glProvokingVertex、GL_DEPTH_CLAMP、GL_TEXTURE_CUBE_MAP_SEAMLESS） | cleared（2026-09-27。Venus で glx-p031 PASS、回帰 PASS。i915 実機は未実施） | p013 |
| [ws068-p032](phase032/phase.md) | GLSL compiler の geometry shader（`#version 150`、layout の in/out の primitive、EmitVertex・EndPrimitive、`gl_in[]`、`gl_Layer`・`gl_PrimitiveID`）、3 stage の link と SPIR-V、host の試験（spirv-val・lavapipe） | cleared（2026-09-27。host の試験 PASS、Venus の回帰 PASS） | p031 |
| [ws068-p033](phase033/phase.md) | desktop GL 3.2: libGLESv2・libGL の geometry shader（GL_GEOMETRY_SHADER、3 stage の pipeline、adjacency の mode、draw の mode と入力の primitive の検査）、layered の framebuffer（glFramebufferTexture と gl_Layer）、multisample texture（glTexImage2DMultisample、sampler2DMS、texelFetch、glSampleMaski・GL_SAMPLE_MASK、glGetMultisamplefv）、core と compatibility の profile（core は固定機能を断る）、3.2 の context（GL_VERSION 3.2・GLSL 1.50）（2026-09-27 に 3.3 の API を p037 に分けた） | cleared（2026-09-27。Venus で glx-p033 PASS、i915 未実施） | p032 |
| [ws068-p037](phase037/phase.md) | desktop GL 3.3: timer query（GL_TIME_ELAPSED・GL_TIMESTAMP・glQueryCounter・glGetQueryObjecti64v）、dual-source blend（glBindFragDataLocationIndexed、SRC1 の factor、layout(index)）、RGB10_A2UI、3.3 の context（GL_VERSION 3.3・GLSL 3.30） | planned・保留（2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」）。コードは未着手 | p033 |
| ws068-p034 | tessellation（GLSL の control・evaluation の stage、patch、glPatchParameteri）: GL_ARB_tessellation_shader | planning・保留（2026-09-27、p037 と同じ） | p037 |
| ws068-p035 | compute shader、SSBO、image load/store、atomic counter（GLSL の compute の stage、glDispatchCompute、glMemoryBarrier）: GL_ARB_compute_shader 等 | planning・保留（2026-09-27、p037 と同じ）。2026-09-30 main: GLES 3.1 の compute の部分集合（GLSL ES 3.10 の compute、glDispatchCompute・SSBO・glMemoryBarrier）は [WS101](../ws101/ws.md)（p008〜p010）へ移した。desktop GL の compute と image load/store・atomic counter はこの行に残る | p037 |
| ws068-p036 | GL 4.x の残り（fp64、sample shading、draw indirect、cube map 配列、texture gather、separate shader objects、vertex attrib binding、KHR_debug、DSA の部分、clip control、SPIR-V の shader）と版の名乗り。着手前に更に分ける | planning・保留（2026-09-27、p037 と同じ） | p034、p035 |
| [ws068-p004](phase004/phase.md) | GLES 2.0 の残り: `gl_FragCoord`・`gl_PointCoord` の GL の向き、`gl_DepthRange`、sampler の配列、egltest の scene `es2` | in-progress（2026-10-05 P1 q747。範囲を Q1 が承認、実装中） | p003 |
| ws068-p005 | GLES 3.0 | 2026-09-27 に p024〜p027 に分けた | p004 |
| [ws068-p024](phase024/phase.md) | GLES 3.0 の API（1）: VAO、buffer の map・copy、instancing、整数の属性と uniform、uniform buffer、glGetStringi | cleared（q495-i01、2026-09-27。Venus で egl-p024、回帰 PASS） | p020、p021、p022 |
| [ws068-p025](phase025/phase.md) | GLES 3.0 の API（2）: sized の format（float・整数・depth）の texture の保存と変換、glTexStorage2D、OpenGL ES 3 の texture の parameter（BASE/MAX_LEVEL、MIN/MAX_LOD、WRAP_R、swizzle、compare）、sampler object、depth texture と shadow sampler（2026-09-27 に 3D・配列と pixel buffer を p028 に分けた） | cleared（2026-09-27。Venus で egl-p025 PASS、回帰 egl-p008・p019・p020・p022・p023・p024・x11-p005・boot test PASS） | p024 |
| [ws068-p028](phase028/phase.md) | GLES 3.0 の API（2b）: 3D・2D 配列の texture（glTexImage3D・glTexSubImage3D・glCopyTexSubImage3D・glTexStorage3D、sampler3D・sampler2DArray）、pixel の pack/unpack buffer と OpenGL ES 3 の pixel store（ROW_LENGTH・SKIP_*・IMAGE_HEIGHT） | cleared（2026-09-27。Venus で egl-p028 PASS、回帰 PASS。i915 実機は未実施） | p025 |
| [ws068-p026](phase026/phase.md) | GLES 3.0 の API（3）: 複数の colour attachment（glDrawBuffers、4 まで）、READ/DRAW の framebuffer と glReadBuffer、sized の colour の format（ES 3.0 の描ける format と EXT_color_buffer_float）の texture と renderbuffer の取り付け、texture の level と 2D 配列の layer（glFramebufferTextureLayer）、depth・depth-stencil の texture と sized の depth の renderbuffer（DEPTH_STENCIL_ATTACHMENT）、glClearBuffer*、glInvalidate(Sub)Framebuffer、整数・float の glReadPixels と IMPLEMENTATION_COLOR_READ_*（2026-09-27 に glBlitFramebuffer と multisample を p029 に分けた） | cleared（2026-09-27。Venus で egl-p026 PASS、回帰 PASS。i915 実機は未実施） | p028 |
| [ws068-p029](phase029/phase.md) | GLES 3.0 の API（3b）: glBlitFramebuffer（colour の拡大縮小と filter、depth・stencil、窓の framebuffer との間）、multisample の renderbuffer（glRenderbufferStorageMultisample、GL_MAX_SAMPLES 4、blit での resolve）、glGetInternalformativ、3D texture の slice の取り付け（libvulkan は Vulkan 1.0 で 2D_ARRAY_COMPATIBLE が無いので別の image に描いて copy） | cleared（2026-09-27。Venus で egl-p029 PASS、回帰 PASS。i915 実機は未実施） | p026 |
| [ws068-p027](phase027/phase.md) | GLES 3.0 の API（4）: query（occlusion）、sync object（glFenceSync）、残りの entry point（glGetFragDataLocation、program binary の拒否）（2026-09-27 に transform feedback と GL_VERSION を p030 に分けた） | cleared（2026-09-27。Venus で egl-p027 PASS、回帰 PASS。i915 実機は未実施） | p026、p029 |
| [ws068-p030](phase030/phase.md) | GLES 3.0 の API（5）: transform feedback（libvulkan に VK_EXT_transform_feedback が無いので vertex shader の storage buffer への書き込みで写す。GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN、GL_RASTERIZER_DISCARD）、GL_VERSION を「OpenGL ES 3.0」・GLSL ES 3.00 に | cleared（2026-09-27。Venus で egl-p030 PASS、回帰 PASS。i915 実機は未実施） | p027 |
| [ws068-p006](phase006/phase.md) | i915 実機での確認（GLX の zgears、App Home の X11、仮想デスクトップ） | cleared（q484-i01。実機で 6 検査 PASS の run あり、回転の間欠の止まりは BUG-057） | p008、p010、ws069-p005、F-023 |
| ws068-p007 | 規約の全文との照合と回帰（最後） | planning | 全 Phase |
| [ws068-p008](phase008/phase.md) | GLES 2.0 の描画の核（SPIR-V の shader binary、変換層。compiler の方式に依らない部分） | cleared（q475-i01。Venus で strip・texture・blend・depth・cull、display 直接と窓と resize） | p002 |
| [ws068-p009](phase009/phase.md) | frame を 2 枚重ねる（EGL の frame in flight。Venus で clear だけ 125 ms/frame、WSI 直接は 50 ms） | in-progress（2026-10-05 P1 q747。実装は merge 済み、T1-180 の Venus の回帰と計測待ち） | p008 |
| ws068-p039 | ETC2・EAC の圧縮 texture（ES 3.0 の必須の 10 format、`glCompressedTexImage2D`・`glCompressedTexSubImage2D`。device に無ければ CPU で展開。i915 の Gen12 は hardware に無い） | planned（2026-10-05 Q1 が p004 の範囲外から新設、今は実行しない） | p004 |
| [ws068-p010](phase010/phase.md) | EGL の pbuffer（offscreen。GLX の描画先） | cleared（q476-i01。Venus で 600 frame と 2048x1536） | p008 |
| ws068-p011 | framebuffer object・renderbuffer・cube map（texture への描画） | 2026-09-27 に p022（FBO・renderbuffer）と p023（cube map・mipmap の GPU 化・FBO からの copy）に分けた | p008 |
| [ws068-p022](phase022/phase.md) | framebuffer object と renderbuffer（texture への描画、FBO の readback） | cleared（q493-i01、2026-09-27。Venus で egl-p022） | p008 |
| [ws068-p023](phase023/phase.md) | cube map（texture・sample・面への描画）、GPU の描いた texture の `glGenerateMipmap`、FBO からの `glCopyTex*` | cleared（q494-i01、2026-09-27。Venus で egl-p023） | p022 |
