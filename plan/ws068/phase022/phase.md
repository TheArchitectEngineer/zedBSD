<!-- awesome-plan project=zedbsd record=ws068p022 -->

# ws068-p022: framebuffer object と renderbuffer（texture への描画）

Phase ID: `ws068-p022`
Parent: [WS068](../ws.md)
Status: cleared（q493-i01、2026-09-27）
Phase disposition: normal
Queue: q493-i01
承認: 2026-09-27 ユーザーの自走の指示と WS068 の計画（ws068-p011「framebuffer object・renderbuffer・cube map」を p022・p023 に分けた、
desktop GL 3.0（p013）と GLES 3.0（p005）の前提）

## 範囲

- libGLESv2 の framebuffer object: `glGenFramebuffers`〜`glCheckFramebufferStatus`、`glFramebufferTexture2D`（2D texture の level 0）、
  `glFramebufferRenderbuffer`、`glGetFramebufferAttachmentParameteriv`、renderbuffer（RGBA4・RGB565・RGB5_A1・RGBA8 は RGBA8 の image、
  DEPTH_COMPONENT16・DEPTH24_STENCIL8・STENCIL_INDEX8 は device の depth/stencil の format）。
- FBO への draw・clear・`glReadPixels`・scissor・viewport。描いた texture を後の draw で sample する。
- 方式:
  - FBO の render pass は draw surface の frame の command buffer に積む（既定の framebuffer の pass と交互に）。FBO の pass は開いたまま
    続け、target の切り替え・readback・swap（libEGL の新しい hook `frame_closing`）で閉じる。
  - pipeline は FBO 用の互換 render pass（RGBA8 と depth の有無）で作る。FBO では y を裏返さない（texture の行は GL の下から上）:
    program は y を裏返さない vertex module も持ち、front face を逆にし、viewport・scissor も裏返さない。
  - GPU が描いた texture（`gpu_written`）は CPU の level が古いので、CPU の変更（TexImage・TexSubImage・GenerateMipmap・level 数の
    変わる parameter）の前に読み戻す。
  - 取り付けは名前で持ち、使うときに引く（消された object は不完全）。
- cube map・`glGenerateMipmap` の GPU 化・`glCopyTex*` の FBO からの読みは ws068-p023。

## 受け入れ

1. build warning 0、新しい・変えた C は style-check の指摘 0（legacy の file は増やさない）。
2. egltest の新しい scene（FBO の texture に depth 付きで描き、その texture を窓に貼る。FBO からの glReadPixels）が Venus の Wayland と
   display 直接で PASS（画面の点と readback の点）。
3. 回帰: egl-p008・egl-p019・egl-p020・x11-p005（GLX）・zdesktop-p070、boot test。i915 実機は未実施でよい（F-023 の範囲、記録する）。

## 結果（2026-09-27、q493-i01）

- libEGL: `zegl_depth_format`（depth と stencil の format の選び方）を公開、`zegl_gles.frame_closing`（eglSwapBuffers の submit の前と
  eglMakeCurrent の前の context で呼ぶ）。
- libGLESv2:
  - `framebuffer.c` を作り直した: framebuffer object と renderbuffer を名前空間で持ち、取り付けは名前で引く（消された object は
    不完全、束縛中の FBO からは detach）。完全性の検査（MISSING・ATTACHMENT・DIMENSIONS・UNSUPPORTED）、render pass（LOAD/STORE、
    外との依存）と framebuffer を取り付けの view が変わったときに作り直す。`gles_target_open/close`: draw・clear・read の行き先を
    surface か FBO に開き、FBO の pass は開いたまま続けて切り替え・readback・swap で閉じる。
  - pipeline は FBO 用の互換 pass（[colour][depth]）で作る。program は y を裏返さない vertex module も持つ（`gles_spirv_position` の
    flip 引数）。FBO では front face を逆に、viewport と scissor を裏返さない。
  - texture の image に COLOR_ATTACHMENT と TRANSFER_SRC、level 0 の attach view。FBO が描いた texture は `gpu_written` で、CPU が
    変える前（TexImage・TexSubImage・CopyTex*・GenerateMipmap・min filter の変更）に `gles_texture_fetch` で読み戻す。
  - `glReadPixels`（`gles_read_rgba`）は FBO の colour image を読む（行は GL の順）。
- egltest `--scene=fbo`: scene を 256x256 の texture（16 bit の depth renderbuffer 付き）に描き、その texture を窓いっぱいに貼る。
  FBO からの readback（run=TOKEN-fbo）と窓の readback の両方を検査。試験 `plan/ws068/tests/egl-p022.sh`。

## 検証

- build（`plan/ws035/tests/build-zdesktop-image.sh`）: 我々の source の warning 0（NoctLang の interpreter と openssl の warning は外部の既存のもの）。
  style-check: libGLESv2・libEGL・egltest の変えた file すべて 0。
- Venus（QEMU）: egl-p022 PASS（display・wayland・docked の resized の画面の点、FBO と窓の readback、failures=0）。
  回帰: egl-p008・egl-p019・egl-p020・x11-p005 PASS、boot test PASS（`build/ws068-p022-boot/login.png`）。
  zdesktop-p070 は 5 つの試験の後の同じ guest で 1 回 FAIL（App Home の icon が log に出ない。host は 7 つのサブエージェントで高負荷、
  zdesktop の起動の 4 秒の待ちの後の click が早すぎたと見る）、新しい guest で再実行して PASS（`build/p022r2-p070/`）。
- i915 実機: 未実施（i915 の実行器の render pass・依存の扱いは F-023 の範囲）。
- 制限: 描き込めるのは 2D texture の level 0 と renderbuffer（cube の面は p023）。depth texture は無い（UNSUPPORTED）。
  別の draw surface に current を移した後の fetch は、元の surface の frame に積んだ描画を見ない。
