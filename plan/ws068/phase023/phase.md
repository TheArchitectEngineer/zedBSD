<!-- awesome-plan project=zedbsd record=ws068p023 -->

# ws068-p023: cube map と、GPU の描いた texture の mipmap・copy

Phase ID: `ws068-p023`
Parent: [WS068](../ws.md)
Status: in-progress（q494-i01）
Phase disposition: normal
Queue: q494-i01
承認: 2026-09-27 ユーザーの自走の指示、WS068 の計画（ws068-p011 を p022・p023 に分けた）

## 範囲

- `GL_TEXTURE_CUBE_MAP`: 面ごとの level（`levels[face * GLES_LEVELS + level]`、2D は面 0）、unit ごとの cube の束縛、6 layer の
  image（CUBE_COMPATIBLE、cube の view）、完全性（6 面の level 0 が同じ大きさの正方形）、`glTexImage2D` ほかの面の target、
  `glGenerateMipmap` の面ごと、`GL_TEXTURE_BINDING_CUBE_MAP`、samplerCube の sampler（GLSL と SPIR-V の Dim Cube の反射）、黒の cube。
- FBO の面への描画（`glFramebufferTexture2D` の cube の面）、面の readback、`gpu_written` の面ごとの読み戻し。
- `glGenerateMipmap` と `glCopyTex*` は GPU の描いた texture・FBO で正しく動く（読み戻しの後に CPU で作る。GPU の blit は後）。

## 受け入れ

1. build warning 0、変えた・新しい C の style-check 0。
2. egltest の `--scene=cube`（CPU・FBO の clear・FBO からの copy の 3 通りで面を作り mipmap、6 方向を sample）が Venus の display と
   Wayland で PASS（画面の点と readback）。
3. 回帰: egl-p022・egl-p020・egl-p008・x11-p005、boot test。i915 実機は未実施でよい（記録する）。
