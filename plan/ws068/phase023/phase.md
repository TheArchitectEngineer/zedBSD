<!-- awesome-plan project=zedbsd record=ws068p023 -->

# ws068-p023: cube map と、GPU の描いた texture の mipmap・copy

Phase ID: `ws068-p023`
Parent: [WS068](../ws.md)
Status: cleared（q494-i01、2026-09-27）
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

## 結果（2026-09-27、q494-i01）

- texture の level を面ごとに（`levels[face * GLES_LEVELS + level]`、2D は面 0）、unit ごとの 2D と cube の束縛、`glBindTexture` は
  最初の target を保つ（違う target は GL_INVALID_OPERATION）。cube map の image は 6 layer（CUBE_COMPATIBLE）で cube の view、面ごとの
  level 0 の attach view。完全性（6 面の level 0 が同じ大きさの正方形）、level の鎖は全面で数える。`glTexImage2D`・`glTexSubImage2D`・
  `glCopyTex*` は面の target、`glGenerateMipmap` は面ごと（`texture_mipmaps`）。`GL_TEXTURE_BINDING_CUBE_MAP`、黒の cube。
- samplerCube の uniform は cube の unit と cube の view で束ねる（GLSL の反射に加え、SPIR-V の反射も OpTypeImage の Dim Cube を
  `GL_SAMPLER_CUBE` にした）。
- FBO: `glFramebufferTexture2D` に cube の面、面の readback（`color_layer`）、`gpu_written` は面の bit で、読み戻しは描いた面すべてを
  1 回の flush で。
- egltest `--scene=cube`（`userland/desktop/egltest/cube.c`）、試験 `plan/ws068/tests/egl-p023.sh`。

## 検証

- build warning 0（我々の source）、style-check: libGLESv2・egltest の変えた・新しい file すべて 0。
- Venus（QEMU）: egl-p023 PASS（display と Wayland の画面の 7 点、readback failures=0。CPU・FBO の clear・FBO からの copy で作った面の
  色が mipmap の後も正しい）。画面を目で確かめた。回帰 egl-p022・egl-p020・egl-p008・x11-p005 PASS、boot test PASS
  （`build/ws068-p023-boot/login.png`）。
- i915 実機: 未実施。
- 制限: `glGenerateMipmap` は GPU の描いた面を読み戻して CPU で作る（GPU の blit は後）。面の向き（面の中の texel の並び）は単色の面で
  確かめただけ。
