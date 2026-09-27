<!-- awesome-plan project=zedbsd record=ws075p006 -->

# ws075-p006: MRT・query・texel と storage の buffer・multisample（実行器と compiler）

Phase ID: `ws075-p006`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-28〜）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。p005 の後（依存 p005 cleared）。

## 範囲

egltest の OpenGL ES 3 の場面 targets・blits・queries・feedback を実機の i915 で通す。そのために要る:

1. MRT（ws031-p031）: 複数の colour attachment への描画（compiler の location 0 以外の出力、render target の binding table と
   blend state の entry、clear）。
2. depth と stencil の形式: DEPTH_COMPONENT24（`X8_D24_UNORM_PACK32`）、DEPTH24_STENCIL8（`D24_UNORM_S8_UINT`）と stencil の試験。
3. occlusion query（sync の module）と fence sync。
4. texel buffer（buffer view、samplerBuffer の texelFetch・textureSize）と storage buffer（transform feedback の VS の store）。
5. multisample の image と resolve（sampler2DMS の texelFetch、blit の resolve）。

範囲外: geometry shader・gl_Layer・layered の描画（p007）、GLX の glxtest（p007 以降で capture の場面を足す）。

## 手順

最初に 4 場面を今の実行器で実機に走らせ、log の CHECK と executor の拒否（`i915: vk: ... refused`・`XXX unimplemented`）から
不足を並べ、場面ごとに直す。`plan/ws031/tests/zdesktop/run-egltest.sh` に 4 場面を足した（p005 の 7 場面の後）。

## 検証

| 確認 | 結果 |
| --- | --- |
| 実機 capture zdesktop-egltest | 未実施 |
| QEMU | 未実施（i915 の実機の変更） |
