<!-- design-reviewer のレビュー 1（2026-10-19、ws270 design.md 版 1）。Q1 が報告の本文をそのまま置いた -->

## WS270 p001 設計のレビュー 1

対象: `/home/awe/zedBSD-claude1/plan/ws270/design.md` 版 1（あわせて `plan/ws270/ws.md`・`plan/ws270/phase001/phase.md`）。前提として `plan/ws269/design.md` 改訂 3.1、`plan/ws269/ws.md`、`plan/ws269/phase007/phase.md`、`src/drivers/gpu/powervr/display.c` と関係する code を読みました。
やったのは読むことだけです（grep・sed・`git show`、それと host の `qemu-system-riscv64 -device virtio-gpu-gl-pci,help` で property を見たのみ）。build・QEMU・VM は動かしていません。file も編集していません。

**レビュー中に main が進んだ点（先に共有）**: 23:15 の d9a4dc026 で `plan/ws270/phase002/phase.md`（q1336、P1）が merge されました。中身は riscv64 の userland.mk への `userland-dynamic.mk` の include、LIBUTIL の二重定義の除去、基本の program の規則からの `DYNAMIC_PORTABLE_PROGRAMS` の除外、external.mk と openssl の riscv64、gen-cross-toolchain の `-mno-relax`、kwl_cycles、environment.c です。版 1 の §1 の誤りのいくつかは、この p002 ですでに直っています。設計の本文はまだ版 1 のままなので、下ではそれぞれ「p002 で解決済み、設計への反映が要る」と書き分けています。

---

### Blocker

**B1. display の claim・present の道を取り違えている（§2.1 D、§2.2 の条件 2、§3 の p004・p006 の依存）**
- 根拠:
  - `userland/desktop/libkeiland-backend-zedbsd/scanout-zedbsd.c:8-26` は game mode の direct scanout 専用です（「the compositor's game mode shows a client's GPU buffer … without composing it」）。`scanout_display()`（同 :327-329）は `GPU_DISPLAY_CONNECTED | GPU_DISPLAY_BLOB` が無ければ ENOTSUP を返します。powervr の display は BLOB の present を ENOTSUP で断ります（`display.c:741-742`）。constraints も COPY だけです（`display.c:850`）。
  - compositor が普段 display を claim して present する道は libvulkan の WSI です（`libvulkan/wsi-display.c:990` の `GPU_DISPLAY_CLAIM`）。この WSI には copy の道がすでにあります。`wsi-display.c:1883-1889` の `display_prepare_copy` は `GPU_SCANOUT_COPY` を見ます。`:1810-1852` は `GPU_RESOURCE_WRITE` で書いてから `GPU_DISPLAY_PRESENT` を呼びます。`wsi-swapchain.c:1097-1118` は shared の trial が失敗すると copy に落ちます。companion が無い display も隠しません（`wsi-display-nodes.c:426`）。
- 起こること:
  - (1) D を設計どおり scanout-zedbsd.c で試すと必ず ENOTSUP になります。
  - (2) 画面なしの段では Vulkan の device（gpu1）が無いので、desktop の本来の claim の道（libvulkan）はそもそも動きません。結局 D でできるのは WS269 p007 の実機の試験 1〜4（`gpu-bars` の claim → 色 → release、`plan/ws269/phase007/phase.md`）と同じ事だけで、「WS269 p007 と同じ物は重ねない」と矛盾します。
  - (3) §2.2 の条件 2「compositor が copy の道で動かない時は import を待つ」と、p006 が「WS269 p007 の import」に依存する事は前提が違います。copy の道は libvulkan に実装済みで、import を待たずに描画を始められます。
- 直し方:
  - D を削る。または「desktop の image で `gpu-bars` と `display-inventory`（既存の `userland/tests/`）が動く事」に縮める。
  - §2.2 は「最初の描画は libvulkan の copy の道（gpu1 → gpu0 の GPU_RESOURCE_WRITE）、import と damage は性能のため（WS269 p007 の後半）」と書き直す。
  - p006 の依存から「p007 の import」を外し、性能の受け入れの方に置く。
  - scanout-zedbsd.c は game mode として「VF2 では import が来るまで direct scanout なし（`wayland/scanout.c:296-300` で kept に落ちる）」と明記する。

---

### Major

**M1. §1.1・§1.3 の build の原因の分析に誤りと抜けがある（p002 で一部解決済み）**
- 根拠と状況:
  - (a) 二重定義が起きる。版 1 の §1.3 の案（include を足すだけ）では libutil.so の規則が二重になります（旧 `platform/riscv64/userland.mk:88` と `platform/common/userland-dynamic.mk:7`）。desktop の program（class は basic、例 `userland/desktop/wayland/Makefile:47`）も、riscv64 の `RISCV64_APP_BASIC_COMMAND` と userland-dynamic.mk の両方で定義されます。make は「overriding recipe」の警告を出すので warning 0 に反します。326 行の undefined symbol は、正しくは「basic の規則が libc だけで link している」事が原因です。arm64 は `vmunix.mk:589` で filter-out しています。→ p002（d9a4dc026）で両方とも直っています。
  - (b) `DYNAMIC_LINK_LDFLAGS` の案に `--no-relax` がありません（`RISCV64_USER_LDFLAGS` は `-m elf64lriscv --no-relax -z max-page-size=4096`）。→ p002 は `$(RISCV64_USER_LDFLAGS)` を使っていて解決済みです。
  - (c) `bin/keiland-preview` を「userland-dynamic.mk を include していない」側に分類しているのは誤りです。class が static の program です（`userland/desktop/preview/Makefile:16`）。arm64 では `ARM64_USER_STATIC_COMMAND`（`platform/arm64/vmunix.mk:457-490`）が作ります。riscv64 には static の規則がありません。
  - (d) 外部 package の穴として、`gen-cross-toolchain.sh:43-46` の abi_flags（riscv64 の `-DKERN_USER_ABI_LP64`）と、`external.mk:51-52` の `ZEDBSD_EXTERNAL_LLVM_TARGET`（riscv64 が X86 に落ちる）が抜けていました。→ p002 で解決済みです。
- 直し方:
  - §1 を p002 の結果に合わせて書き直す。
  - keiland-preview は「arm64 の static の規則を riscv64 に写す」項目として立てる。kernel は static の PT_TLS を x86・m68k 以外では ENOEXEC で断りますが（`src/kern/elf.c:1382-1387`）、これは arm64 も同じです。したがって「static TLS の設計判断」ではなく、「PT_TLS を持たない static の image を作る」作業です（`check-user-elf.py` で確かめる）。
  - p002 の phase.md の「libcurses は toolchain の範囲」は誤りです。AGENTS.md の toolchain の定義に `userland/base/curses` は入っていません。libcurses の規則は platform の規則です（arm64 `vmunix.mk:344`）。設計 §1.3 の記述（p002 で他の arch から写す）の方が正しいので、p002 の記録を直してください。

**M2. D4（Noct）の推奨の根拠が事実と違い、subagent の禁止にも触れる**
- 根拠: `ZEDBSD_NOCT_ACCEL` は amd64 でしか効きません（`userland/base/noct/Makefile:88` の `NOCT_ZEDBSD_ACCEL := $(and $(filter amd64,…),…)`）。rpi4 の `y` は意味を持っていません。desktop の実行時に `/bin/noct` を使う所も見つかりません（`userland/desktop`・init・etc を grep）。§1.3 の「`make-*.noct` の道具」は host の noct（`build/NoctLang`）で動くので、riscv64 の `bin/noct` とは関係がありません。
- 起こること:
  - 「desktop の image は Noct を前提にしている」という D4 (a) の推奨の理由が成り立ちません。
  - AGENTS.md は「subagent は toolchain を変更・build・install しない」と定めています。`userland/base/noct/` の build もこれに含まれます。p002 の試しの config（`plan/ws270/tests/config-visionfive2-desktop.mk:57`）は `noct` を選んでいます。Q1 が noct の diff を適用した後でこの config を subagent が build すると、Noct の build が走って禁止に触れます。
  - D4 の決定者は、AGENTS.md では main（Q1）の許可です（LLVM の時はユーザーの許可）。ユーザーの判断の表に入れるなら、そう明記する必要があります。
- 直し方: 推奨を (b)「VF2 の desktop の config から noct を外す」に変える。試しの config からも noct を外す。riscv64 の Noct は別の判断として Q1 に回す。

**M3. D2（QEMU riscv64-virt の Venus）の実現性と費用を低く見積もっている。HAL の実装の変更も要る**
- 根拠:
  - (a) riscv64 の HAL は user の device の写像を断ります。`src/hal/riscv64/space.c:1897-1899` は「device pages are not offered to users」で `HAL_SPACE_DEVICE` を UNSUPPORTED にします。`:1902-1907` の NOCACHE は board の uncached の別名が要りますが、virt では offset が 0 です。一方、Venus の blob の user の写像は `DRV_GPU_MAPPING_DEVICE`（`venus/venus.c:1442`）→ `VM_DEVICE_MMIO`（`gpu.c:3267-3268`）→ `HAL_SPACE_DEVICE|HAL_SPACE_NOCACHE`（`src/kern/vm/device.c:163-164`）です。したがって HOST_VISIBLE の memory の map は失敗します。
  - (b) ECAM の host は 32 bit の窓しか使いません（`pci-ecam-fdt.c:294-298`「A 64-bit window … are not used」）。QEMU virt の 32 bit の PCI の MMIO の窓は 1 GiB です（QEMU の仕様、確かめたのは記憶から）。既存の Venus の試験は `hostmem=1G` です（`plan/tools/guest/venus-hostmem.sh`）。1 GiB に整列した BAR を置くと他の BAR が入らないか、hostmem の BAR 自体が入りません。
  - (c) Venus の source は amd64 の list にしかありません（`platform/amd64/vmunix.mk:142-146`）。PCI への登録も `src/kern/platform/pcat.c:46・208・322` だけです。riscv64 の vmunix.mk と `riscv-virt.c` に足す必要があります。
  - (d) virt の Venus の display は venus 自身の node（shared/blob の scanout）なので、VF2 の copy の道は試されません。
  - (e) amd64 の Venus 試験は KVM で動きますが、riscv64 は TCG です。sessiond の `GREETER_READY_MS 30000`（`sessiond/greeter.c:66`）を越えると、greeter の失敗として text に戻ります（推測、起動時間は未計測）。
  - (f) desktop の PNG をどの道具で撮るかが書かれていません。boot-test.sh の riscv64-virt は ramfb を撮りますが、Keiland は virtio-gpu の head に出ます。
- 確かめた事: host の `qemu-system-riscv64` 10.0.11 の `virtio-gpu-gl-pci` には `venus`・`blob`・`hostmem` の property があります。
- 直し方:
  - D2 の選択肢に (a)〜(f) の費用を書く。
  - p005 の LW を上げる（目安 16〜28）か、p005a（kernel: virt の Venus と `vkdemo`）と p005b（Keiland）に分ける。
  - (a) は hal.h の API を変えない HAL の実装の補完です（hal.h:750-754「A port may report HAL_ERR_UNSUPPORTED」の範囲）。AGENTS.md では承認は要りませんが、§4 の「HAL の変更は要らない見込み」には p005 の例外として明記してください。
  - その上で推奨を考え直す。

**M4. copy の道の性能を評価していない（WS269 D7 (a) と組み合わさる）**
- 根拠:
  - libvulkan の copy の道は、毎 frame 全面を CPU で読みます。GPU から host-visible の staging へ copy し、CPU が `GPU_RESOURCE_WRITE` で gpu0 の resource へ書き（`wsi-display.c:1810-1835`）、kernel が framebuffer へ copy して clean します（`display.c` の show_frame）。
  - WS269 D7 (a) では HOST_COHERENT は uncached で、HOST_CACHED は出しません。staging の CPU の読みは uncached の読みになります。
  - damage の矩形だけを copy する案（WS269 §3.5）は import の道だけの話で、copy の道は全面を copy します。
- 起こること: 1080p では 1 frame 約 8 MB を uncached で読みます。U74 の uncached の読みの帯域は未計測ですが、frame あたり数十 ms になり、10 fps 台以下と推測します。compositor の起動も遅くなり、上の 30 s の READY の時間切れにかかる危険があります（推測）。
- 直し方: §2.2・§5 に性能の危険と、測る点（p006 で frame の時間）を書く。「copy の道は最初の画を出すまで、実用の fps は import と damage（WS269 p007 の後半・p011）」と段を分ける。

**M5. WS269 p011 と WS270 p006 の持ち主が二重で、依存が循環しかねない**
- 根拠: WS269 p011 は「Keiland の要る範囲の残り（WS270 と合わせる）」で、確かめは「実機: Keiland の desktop をユーザーが目で見る」です（`plan/ws269/design.md` §6）。WS270 p006 も「WS269 p011 と合わせる」で、確かめは「ユーザーが desktop を目で見る」です。
- 起こること: 同じ受け入れを 2 つの WS が持ちます。どちらがどちらに依存するか（GPU の機能の不足 → desktop の起動、の向き）が決まらず、cleared の判定が宙に浮きます。
- 直し方: WS269 p011 は「Keiland が使う Vulkan の機能の不足を driver 側で埋める（受け入れは機能の試験）」、WS270 p006 は「desktop の起動と目視」とし、矢印は WS269 p011 → WS270 p006 にする。または両者を 1 つの Phase にまとめる。どちらにするかを Q1 と WS269 に依頼する（他 WS の変更）。

**M6. D3 の推奨の仕組みと §1.2 の案が誤り**
- 根拠:
  - `VKVIDEO_SKIP_FAILED`（`libmedia/vkvideo.c:20-28`）は Pi 4 の firmware の decoder で絵が失敗した時に飛ばす設定です。「Vulkan video が無い時」の扱いではありません。
  - aarch64 の側は、`VP_GPU_PRESENT 1`（`videoplayer/videoplayer.h:47-58`、Pi の decoder が持つ絵を GPU で出す）と `VKVIDEO_FLIGHTS 4`（`libmedia/vkvideo-runtime.c:38-47`、§1.2 は見落としています）です。
  - BXE に Vulkan video が無いなら、riscv64 は何もしなくても else の側（CPU で描く）に落ちます。そちらが正しい側です。
- 直し方: §1.2 の案を「変更なし（else の側）」に直し、vkvideo-runtime.c を表に足す。D3 の推奨から `VKVIDEO_SKIP_FAILED` の記述を外し、「Vulkan video の queue が無い時の videoplayer の振る舞いを p007 で見る」に直す。

**M7. §2.1 E が既存の fallback を見ていない**
- 根拠: sessiond には ws239-p001 の fallback がすでにあります（`userland/desktop/sessiond/fallback-rules.c`、`fallback-rules.h:19・22・32`）。greeter が READY を言わずに終わると、6 回連続か 90 s で text console の getty に戻ります。READY の時間切れは 30 s です（`greeter.c:66`）。
- 起こること: E を新しい実装として見積もっています（p003・p004）。正しくは既存の動作を確かめる事が主です。逆に、遅い起動（M3・M4）は READY の時間切れで fallback に倒れ得ます。
- 直し方: E を「ws239 の fallback が riscv64 と VF2 で働く事の確かめ（理由の 1 行が画面に出るかを含む）」に直す。READY の時間切れを §5 の危険に足す。

---

### Minor

1. **§1.3 `check-dynamic-elf.py` の名前**: `--machine riscv64` はすでに使われています（`platform/riscv64/userland.mk:81` ほか）。「p002 で確かめる」は不要です。
2. **§1.1 browser の `shell/internal.h`**: 「推測」は外せます。`-Iuserland/desktop/browser` は `userland-dynamic.mk:1016` の target ごとの flag です。
3. **§1.2 kwl_cycles**: 用途は統計（KWL PERF）だけと確かめました（`wayland/main.c:1374-1376` が自己較正、`compose.c:4003` は perf だけ）。riscv64 の CLOCK_MONOTONIC は tick の分解能で、`HAL_TIMER_FREQUENCY 1000`（`include/hal/arch/riscv64.h:16`、`src/kern/clock.c:520-524`）です。統計としては足ります。p002 の実装（d9a4dc026）はこれと合っています。未決の項目として閉じてください。
4. **§2.1 D の依存「WS269 p007（前半は済み）」**: p007 は in-progress で、実機の目視は未実施です（`plan/ws269/phase007/phase.md`）。「済み」は言い過ぎです。
5. **§2.1 C の試験の program を新しく作る案**: 既存の `userland/tests/hidraw-probe`・`display-inventory`・`gpu-bars`、`aat-input` があります。AGENTS.md の試験の整理の方針から、まず再利用を検討してください。QEMU の image は PCI（xhci・usb-hid）の入った virt の config（例 `plan/ws256/tests/config-riscv64-virt-pci.mk`）だと明記してください。
6. **p003 の root の大きさ**: virt の `root.img` は 64 MiB 固定です（`platform/riscv64/rootfs.mk:20-21`）。desktop の root は入りません（推測、大きさは未測定）。p015 の「走り D」（virt の NVMe に native.img、`RISCV64_NATIVE_ROOT_MIB`）を使う事と、その時の BOOT_MODE を書いてください。`ZEDBSD_ROOTFS_DEVELOPMENT := n`（sysroot の headers と static lib を外す）を D1 か D5 の選択肢に足すと大きさが減ります。
7. **§2.1 B の確かめ方**: login の後に command を打つ手順を、boot-test.sh の `BOOT_THEN` の `type:`・`expect:`（画面を読む）と明記してください。`service status` が要約の 1 行を出すかは未確認です。
8. **ID の衝突**: WS270 の D1〜D5 と WS269 の D5・D7 が同じ名前です（ヘッダの「WS269 ws.md の D5 (a)」と本 WS の D5（root の置き場所））。`WS270-D1` のように前置きを付けると誤読が減ります。
9. **D1 (b) の config の置き場所**: AGENTS.md の「試験の image の作り方」に従い、試験用は `plan/ws270/tests/`（p002 はすでにそうしています）、製品の config/ci は別にすると書き分けてください。
10. **gen-cross-toolchain.sh の注釈**（p002 の変更、推測を含む）: wrapper の注釈「The loader places the startup set in one static area, so … may use any model」は riscv64 では成り立ちません（`platform/riscv64/userland.mk:5-9`）。`-ftls-model=global-dynamic` は強制されていますが、package の明示の `tls_model("initial-exec")` は防げません。openssl・curl・openssh の riscv64 での実行は未確認と記録してください。openssl の patch の中身を変えた時の `ZEDBSD_EXT_openssl_PATCH_LEVEL`（zedbsd7 のまま）を上げる慣習があるかは未確認です。patch は extract の前提なので、再 extract はされます（`external.mk:305-307`）。
11. **`platform/common/userland-dynamic.mk:3-5` の注釈「amd64 and arm64」**: riscv64 も使うようになったので直す（p002 の範囲）。
12. **進め方**: p002（q1336）は、design-reviewer と D1〜D5 の決定の前に main に merge されました。範囲は「設計の判断に依らない物」と絞られていますが、試しの config は rpi4 の全 program（noct・fidoctl・外部 package を含む）を選んでいて、D1・D3・D4 の判断を先取りしています。Q1 の判断として記録を残すことを勧めます。

---

### HAL の変更が要るか

- hal.h の API の変更は、どの Phase でも要らない見込みで、これには同意します。kwl_cycles は clock_gettime で済みます（実装済み）。HOST_COHERENT は WS269 D7 (a) の HAL の実装（承認済みの方針）で出ます。
- ただし p005（D2 (a)）を採るなら、riscv64 の HAL の実装の補完が要ります（M3 (a)）。user の `HAL_SPACE_DEVICE`（と virt での `DEVICE|NOCACHE`）を許す変更です。hal.h の契約の範囲なので承認は不要ですが、設計の §4 に明記が要ります。
- keiland-preview の static の道は kernel にも HAL にも変更は要りません（arm64 と同じ制約）。

### 試験の計画の判定の規則（AGENTS.md）との整合

- QEMU は boot-test.sh の PNG、実機はユーザーの目視で「Q1 の一覧」に載せる、という方針は規則に合っています。
- 抜けているのは次の 3 点です。B の command を打つ道（BOOT_THEN）、p005 の画をどの道具で撮るか（ramfb ではなく virtio-gpu の head）、E の「理由の 1 行」が画面に出るか（sessiond の log が画面に出るかは未確認）。

### 結論

**版 1 のままでは受け入れられません。**

- B1（display の道の取り違い）のため、受け入れ 2 の範囲（D）と、描画を始める条件（§2.2・p006 の依存）を作り直す必要があります。
- M2（D4）・M3（D2）・M6（D3）の 3 つは、ユーザーまたは Q1 に示す判断の推奨か根拠が事実と違うので、提示の前に直す必要があります。
- §1 は main に入った p002（d9a4dc026）に合わせて書き直す必要があります。M1 の libcurses の誤記は p002 の記録の側です。
- M5（WS269 p011 との持ち主）は、他 WS の変更として Q1 の調整が要ります。

上を直した版 2 で、もう一度レビューすることを勧めます。

### 未実施

build・host 試験・QEMU・実機は行っていません。次の点は確かめていません: rootfs の実際の大きさ、U74 の uncached の読みの帯域、TCG での Keiland の起動時間、QEMU virt の 32 bit の PCI の窓の大きさ（記憶による）、VF2 での `/dev/gpu0` の出現。
