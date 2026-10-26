<!-- awesome-plan project=zedbsd record=ws293-p001 -->

# ws293-p001: render/ を vk/ に改名して回帰

Status: in-progress（2026-10-27 q1602 P4: 改名・build・host 試験・5330 の回帰 PASS。cleared の判定は Q1）
Parent: [WS293](../ws.md)
Queue: q1602（P4）
出典: 2026-10-27 ユーザー「vk/に揃えたいです。」。i915 の外の変更は 2026-10-27 に Q1 が了承（path・include・build の規則・注釈の置き換えだけ。識別子と動作は変えない）

## やった事

- cfb417ab9: `git mv` で `src/drivers/gpu/i915/render/` の 55 個の file を `vk/` に移した。名前は `X.c`・`X.h` → `vk-X.c`・`vk-X.h`。入口の対 `render.h`・`vulkan.c` は `vk.h`・`vk.c`、生成物の `vulkan-codec.inc` は `vk-codec.inc`。include guard は `DRIVERS_GPU_I915_VK_VK_X_H`。関数名などの識別子は変えていない。
  - i915 の中: include、兄弟の file を指す注釈（libvulkan の file 名は元のまま）、`tests/contracts`・`tests/fixtures`・`tests/display`・`tests/render` から executor への path。
  - i915 の外: `platform/{amd64,arm64,riscv64}/vmunix.mk`、codec の include（amdrdna・nvrtx・powervr・bcm2711・ghci の vk）、`include/uapi/gpu-op.h` の注釈 1 行。
  - plan: 生きている試験の path（ws031・ws037・ws039・ws075・ws079・ws083・ws101・ws141・ws202・ws269）。基準の commit と比べる ws229/tests/host と ws252/tests/render-host は、`render/`（基準）と `vk/`（今）の両方を読めるようにした。未完の WS の表と未実施の Phase の path（ws083・ws101・ws243・ws294 の ws.md、ws075 p007・ws226 p001・ws290 の design）。
- 直さなかった物: 履歴（ws035 の log・json・tsv、review、実施済みの Phase の記録、exp の patch）、closed の WS288 の試験 2 本（`plan/ws288/tests/ghci-e2e.sh`・`ghci-host-test.sh`）、`src/drivers/gpu/i915/tests/render/`。最後の物は executor の dir ではなく、実行の試験の場面の dir なので残した。
- fbb077cf9: WS252 p005 で入った回帰を試験の側で直した。p005 から、版の比べ方が「9 か、それ以外は 12」から「12 未満」に変わった。版を設定していない試験の batch・device は 0 のまま Gen12 として扱われていたが、0 は「12 未満」に入るので、Gen11 以前の形で書かれるようになっていた。製品の driver は版を必ず 9・11・12 に設定するので影響は無い。直した物: ws101 の compute-batch-test、ws075 の host-layered、ws031 の pipe・cmdbuf の batch を `graphics_ver = 12U` に、ws031 の stubs・cmd-test の device（ADL-P）を `gt.graphics_ver = 12U` に。ついでに cmdbuf-test の古い macro 名（`MI_BATCH_BUFFER_END` → `I915_CMD_MI_BATCH_BUFFER_END`）も直した。
- 1478ff569: main（P2 の amdrdna の変更を含む）を merge。衝突は無く、新しい `render/` の参照も無い。

## 確かめた事（2026-10-27）

- build、どれも rc 0・warning 0: amd64 の既定、`I915_TESTS=y` の execution と vke2、`CONFIG_DRIVER_PCI_GHCI=y`、arm64 の rpi4、riscv64 の visionfive2 と visionfive2-keiland。arm64・riscv64 の build にも `i915/vk/vk-codec.o` が入る。
- host 試験の PASS: i915 の contracts、ws252 の render-host・gen12-golden・eu-gen11、ws229 の host、ws101 の host、ws075 の layered、ws202 の nv12-readback、ws083 の mfx-avc、ws037 の nvrtx-vk 2 本、ws141 の vulkan-stream。ws031 の run-vk-host-tests は cmd・spirv・res・resdispatch・sync・eu・pipe・cmdbuf が PASS。
- 元から通らない試験（改名の前の tree の写しでも同じ所で落ちることを確かめた）:
  - ws031 の lower と compile: mview の shader（`test_mview_shaders`・`test_eu_mview`）の期待値が今の shader と合わない。
  - ws039 の amdrdna-vk-host-test: `struct amdrdna_vk_framebuffer` に `view` が無い（P2 の側の API の変化）。
- 5330 の素の起動（kernel 1478ff569）: ssh まで 47 s、survey lit（frames +240、CRC 2 回同値、underrun 0）、i21 の基準と steady の差 0 → PASS。書き込みの数は 13594 で前と同じ。順の digest は違うが、起動ごとに揺れる。画面は desktop と Settings の初回の画面。証拠は `plan/ws245/tests/evidence/p4-ws293-vk-1478ff569.md`、PNG は `plan/ws245/temp/5330/p4-ws293-vk-1478ff569.png`（git の外）。

## 残り

- 空の dir `src/drivers/gpu/i915/render/` が P4 の worktree に残っている（git は空の dir を持たないので、main には出ない）。
- ws031 の lower・compile の mview の期待値と、ws039 の amdrdna の host 試験は、この Phase の範囲の外。
