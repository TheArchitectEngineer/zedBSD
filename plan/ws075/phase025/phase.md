<!-- awesome-plan project=zedbsd record=ws075p025 -->

# ws075-p025: BUG-117 の原因と修正（窓の多いとき draw が拒まれ compositor の描画が止まる）

Phase ID: `ws075-p025`
Parent: [WS075](../ws.md)
Bug: [BUG-117](../../bugs/BUG-117.md)
Status: in-progress（2026-09-30）
Phase disposition: normal
承認: 2026-09-30 main の指示（最優先、p024 の後）。compositor（`userland/desktop/wayland/`）に原因があれば直してよい（main の許可、IME の file は除く、変更は小さく、区切りごとに main を取り込む）。

## 進め方（main の指示）

1. 再現の率を上げる方法（窓の開閉の繰り返し、descriptor の更新を速める等）を見つける。
2. executor の descriptor の追跡か、compositor の descriptor の更新・view の破棄の順序か、原因を切り分ける。
3. 直し、再現の方法で起きないことを確かめる。

## 分かっていること（コードから、2026-09-30）

- 拒む所（`render/state.c`）の条件は「set が bind されていない、または slot の view か sampler が NULL」の 3 つで、以前の文言は区別しない。
- libvulkan（`userland/desktop/libvulkan`）は create・destroy・vkUpdateDescriptorSets・vkAllocateDescriptorSets を同期で送り（返事を待つ）、
  wire id は増えるだけ（再利用しない）。command buffer の記録は client の側に溜め、vkEndCommandBuffer でまとめて送る（bind の set の
  lookup は End の時、slot の中身は submit の時に読む）。
- executor の set・view・sampler は calloc で作られ、slot は vkUpdateDescriptorSets でだけ書かれる。update の時に set・view・sampler の
  identity が表に無ければ、その書き込みは黙って捨てられるか NULL が入る。
- compositor（`compose.c`）は set を 1 つずつ解放せず spare の表に戻して次の image に使い回す。import・shm の image の解放
  （`import_release`・`image_release`）は set を spare に戻し、view を壊し、構造体を 0 で埋める。draw は `import->set` を bind する。
- 起きた 2 回とも、その直後に kei の session が終わり（status 256）、greeter が 3 回失敗して console に落ちた。compositor の出力
  （/run/user/1000/session.log）は集めていなかった。

## 診断（executor、常設）

どれが欠けたかを分ける log を足した（最初の 16 件、BUG-117）:

- `render/state.c`: 拒む行に「no set bound / no view / no sampler」。
- `render/command.c`: vkCmdBindDescriptorSets の set の identity が session の set でない時。
- `render/descriptor.c`: vkUpdateDescriptorSets の set が session の set でない時、image の descriptor の sampler・view が見つからない時。

harness（`hdmi-h4-hw.sh stop`）は kei の session の log（/run/user/1000/session.log）も集める。
