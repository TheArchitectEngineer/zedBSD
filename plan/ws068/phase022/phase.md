<!-- awesome-plan project=zedbsd record=ws068p022 -->

# ws068-p022: framebuffer object と renderbuffer（texture への描画）

Phase ID: `ws068-p022`
Parent: [WS068](../ws.md)
Status: in-progress（q493-i01）
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
