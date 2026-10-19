<!-- design-reviewer の差分の確認（2026-10-19、ws270 design.md 改訂 2）。Q1 が報告の本文をそのまま置いた -->

## WS270 p001 改訂 2 の差分の確認

**対象**: `/home/awe/zedBSD-claude1/plan/ws270/design.md` 版 2（d6feead4f）。版 1 との差分は `git diff 674470370 d6feead4f -- plan/ws270`。同じ commit の `platform/riscv64/userland.mk`（static の規則・libcurses）、`platform/common/userland-dynamic.mk`、`plan/ws270/tests/config-visionfive2-desktop.mk`、`plan/ws270/phase002/phase.md` も見ました。WS269 側は main の `plan/ws269/design.md` の §3.3・§3.5・§6 と Q1 の追記（:311）を照合しました。

**やった事**: 読む事だけです（git・grep・sed）。build、host 試験、QEMU、VM は動かしていません。file は編集していません。

**先に結論**: B1・M1〜M7・Minor 1〜12 は、本文の上でほぼ直っています。ただし直しの結果として、新しい Major が 4 つ見つかりました。そのうち 1 つ（N4）は、私のレビュー 1 の Minor 5 の誤った提案が原因です。

---

### レビュー 1 の指摘の確認（対応表と本文の照合）

| 指摘 | 判定 | 見た所 |
| --- | --- | --- |
| B1 | 直った | §2 の冒頭と §2.2 の 3 に copy の道が書かれ、scanout-zedbsd.c は game mode 専用と明記された。D は既存の program に縮み、p006 の依存から import が外れた。copy の道に落ちる条件も合っている。`display_import_image` は `GPU_SCANOUT_SHARED` が無いと FORMAT_NOT_SUPPORTED を返し（`wsi-display.c:1287-1288`）、`wsi-swapchain.c:1103-1117` で copy に落ちる |
| M1 | 直った | §1.1 の表。p002 の記録の libcurses の誤記も直った。libcurses の規則の形に新しい問題がある（Minor 1） |
| M2 | 直った | D4 は欠番になった。試しの config から noct と emacs が外れた（emacs が noct を要るのは `userland/base/emacs/Makefile:22` の `REQUIRE = base/noct` で確かめた） |
| M3 | 直った | §4 に (a)〜(f)、p005 は 16〜28 LW、§5 に HAL の例外 |
| M4 | 直った | §2.3。ただし段 2 の前提が成り立たない（N3） |
| M5 | 直った（Q1 の判断） | WS269 design §6 の p011 の行と ws.md:35 の写しは合っている。ただし §2.2 と §3 が矛盾する（N2） |
| M6 | 直った | §1.2 の 3 行、WS270-D3 |
| M7 | 直った | §2.1 E。ただし QEMU で通る道が違う（Minor 3） |
| Minor 1〜12 | 直った | 本文で確かめた。Minor 5 は私の提案そのものが誤りだった（N4） |

---

### Blocker

なし。

---

### Major

**N1. render の node が遅れて出る事への備えに持ち主がいない（WS269 から WS270 への依頼を拾っていない）**
- 根拠:
  - WS269 design §3.3 :142 は「libvulkan は render の node が無い時に失敗するので、Keiland の起動は render の node を上限付きで待つ（WS270 と調整、p010）」と書いています。render の node（gpu1）は、root の mount と FW の読み込みの後（上限 60 s の例）に worker が登録します（同 :138-139）。
  - sessiond の待ちは `/dev/gpu0` だけを見ます（`userland/desktop/sessiond/main.c:528-556`）。待つのは「`/dev/gpu0` が無く、かつ `hw.gpu.attaching` が 0 でない間」です。VF2 では gpu0 が display の node として早く出るので、sessiond は gpu1 を待たずに greeter を起動します。
  - libvulkan の node の組み合わせは、instance ごとに一度決まったら固定されます（`wsi-display-nodes.c:443`「all pairing decisions are fixed for this instance's native node inventory」）。
  - 改訂 2 の本文に、この待ちの扱いはありません（「上限」「gpu1 を待つ」の記述が無い）。
- 起こる事: cold boot では greeter が gpu1 より先に起き、libvulkan の初期化で失敗します。失敗の後は 1・2・4・8・16 s の backoff で再試行します（`main.c:35-37`）。FW の読み込みが長いと、連続 6 回の失敗か 90 s の fallback（`fallback-rules.h:32`）で text に落ちます。最悪の場合、p006 の「desktop を目で見る」は起動のたびに結果が揺れます。
- 直し方: p006 の必須の項目に、次のどちらかを選んで持ち主を決める（他 WS との調整なので Q1 経由）。§6 の危険にも 1 行足す。
  - (i) sessiond の待ちを「`hw.gpu.attaching` が 0 でない間は、gpu0 があっても待つ」に広げる（powervr の worker が attaching を数えている事が前提で、WS269 p005 の必須項目に足す）。
  - (ii) Keiland（または greeter）が render の node を上限付きで待つ。

**N2. §2.2 と §3 の依存が食い違い、WS269 p011 の「不足」を見つける手段が無い**
- 根拠: §2.2 は、描画を始める条件を「WS269 p010 の cleared、p003・p004 の cleared、copy の道」としています。一方 §3 の表の p006 の依存と「p006 以降は WS269 p011 の後」は、WS269 p011（30〜50 LW）を待ちます。WS269 p011 の受け入れは「Keiland が使う Vulkan の機能の試験 program が通る」（WS269 design §6 :209）ですが、その program も機能の一覧も、作り手がどこにも決まっていません。
- 起こる事: 不足は Keiland を VF2 で動かして初めて分かります。ところが p006（Keiland を動かす Phase）は p011 の後にしか始められません。p011 は対象を決められず、宙に浮きます。desktop の最初の試しも 30〜50 LW 遅れます。
- 直し方: p006 を 2 つに分ける。
  - p006a: WS269 p010 の後に Keiland を起動して、不足の機能を一覧にする。この一覧が WS269 p011 の入力になる。
  - p006b: WS269 p011 の後に目視の受け入れ。
  - あるいは、§2.2 に WS269 p011 を足し、機能の試験の program と一覧を作る持ち主（WS270 側）を明記する。
  - どちらの案でも矢印 WS269 p011 → WS270 p006(b) は保てます。Q1 の判断を変える必要はありません。

**N3. 段 2 の「damage の矩形だけの copy」を支える UAPI が無く、持ち主もいない**
- 根拠:
  - `struct gpu_display_present`（`include/uapi/gpu-display.h:189-204`）に damage の field はありません。
  - libvulkan の damage の hook（`wsi-internal.h:211-213`）を埋めているのは Wayland の WSI（`wsi-wayland.c:113`）だけで、display の WSI（`wsi-display.c`）は埋めていません。
  - `VK_KHR_incremental_present` は受け付けています（`device.c:375-379`）。しかし display の道では捨てられます（`wsi-swapchain.c:2992` で platform->damage が NULL）。
  - それでも WS270 §2.3 は、段 2 の実用の fps を「WS269 p007 の後半の import・damage」に置いています。WS269 §3.5 :169 も「copy は damage の矩形だけ」としています。
- 起こる事: import ができても、kernel は damage を知りません。毎 frame 全面を uncached から読むので、段 2 でも fps は段 1 と同程度に留まります（推測）。必要な変更は UAPI の struct の拡張（`version`・`size` で互換を保つ）、`drv_gpu_display_ops` の present への damage、wsi-display の damage の hook、Keiland の `VkPresentRegionsKHR` の 4 層ですが、どの WS の Phase にも入っていません。
- 補足: 逆に copy の道なら、UAPI を変えずに libvulkan の中だけで damage の行だけを読み書きできます。`GPU_RESOURCE_WRITE` は offset と bytes を取るからです（`wsi-display.c:1810-1835`）。§2.3 の「damage は import の道だけ」は事実と逆に近い記述です。
- 直し方: §2.3 の段 2 に、案と持ち主を書く。
  - (a) display の UAPI と WSI に damage を足す（UAPI の変更。WS269 と WS270 のどちらが持つかは Q1 の調整）。
  - (b) copy の道の WSI に damage の hook を足し、damage の行だけを読む（userland だけで済み、WS270 で持てる）。
  - §6 には「damage が無いと段 2 でも全面の copy」を足す。

**N4. §2.1 C の確かめの program が、keyboard・mouse の event を読まない（レビュー 1 Minor 5 の私の誤り）**
- 根拠:
  - `hidraw-probe` は FIDO の security key の CTAPHID の probe です（`userland/tests/hidraw-probe/Makefile` の説明「raw HID security key probe」）。
  - `aat-input` は `/dev/input-inject`（`CONFIG_INPUT_TEST_INJECT`）に event を**注入する**道具で、SSH で操作する前提です（`userland/tests/aat-input/main.c:8-13`）。試しの config では `CONFIG_INPUT_TEST_INJECT := n` です。
  - どちらも USB の keyboard・mouse から `/dev/input` の event を読みません。レビュー 1 の Minor 5 でこの 2 つを挙げたのは私の誤りです。
- 起こる事: C の試験は、通っても落ちても「input を読める」事を示しません。
- 直し方:
  - keyboard は、WS256 p016・p020 と同じく「USB keyboard で login できる」で足ります（`BOOT_THEN` の `type:` が USB の kbd を通る事は p016 の走りと同じ）。
  - mouse は、既存の道具に読み手がありません（`seat-probe`・`tablet-probe` は Wayland の client で compositor が要る）。p003 の中身に「`/dev/input` の mouse の event を読んで 1 行に出す最小の program を作る（または既存の物を探す）」を足す。
  - §2.1 C と表の Minor 5 の行を直す。

---

### Minor

1. **riscv64 の `lib/libcurses.a` が PIC でない**（d6feead4f、`platform/riscv64/userland.mk:222-227`）
   - arm64 の形（`$(BUILD)/user` の `-fno-pic` の object）を写しています。amd64 は、target で作る program が PIE なので、意図して PIC の object（`$(BUILD)/dynamic/obj`）から作っています（`platform/amd64/vmunix.mk:861-868`）。
   - riscv64 の medlow の非 PIC の code は絶対 address（R_RISCV_HI20 など）を使うので、PIE には link できません（lld が「recompile with -fPIC」で断る見込み。実際に link は試していません）。aarch64 は ADRP が PC 相対なので偶然通っています。
   - 今この archive を使う物は tree にありません（grep で 0 件）。ただし image（`/lib/libcurses.a`）には入ります。
   - 直し方: amd64 と同じく `$(call ZEDBSD_USERLAND_OBJECTS,$(DYNAMIC_DIR)/obj,curses)` から作る。注釈「as on arm64」も直す。
2. **riscv64 で static の program を実行した事が無い**
   - riscv64 には static の試験の ELF の規則が無く（`platform/riscv64/vmunix.mk` に `.ELF` も `check-user-elf` も無い）、`keiland-preview` が最初の static の実行になります。sandbox_spawn の道（`preview/zedbsd/confine.c`）も riscv64 では未実行です。
   - 直し方: p003 で text の console から `keiland-preview` に画像を 1 枚渡して出力を確かめる手順（`BOOT_THEN` の `type:`・`expect:`）を足す。
3. **§2.1 E は QEMU と VF2 で別の道を通る。試しの config が graphical login n のまま**
   - riscv64-virt には gpu0 が無く、`hw.gpu.attaching` も 0 なので、sessiond は display を待たずに戻ります（`main.c:536-543`）。greeter の READY の時間切れや連続の失敗の fallback は通りません。これを通るのは VF2（gpu0 あり、gpu1 なし）だけです。
   - `config-visionfive2-desktop.mk:21` は `ZEDBSD_GRAPHICAL_LOGIN := n` です。p003・p004 の config では y にするか、boot の行に `login=graphical` を足す必要があります。graphical boot n の時だけです（memory の「login=graphical の重複で boot が止まる」の件）。
   - 直し方: E の表に、QEMU は「display が無い時の道」、VF2 は「greeter の失敗の道」と書き分ける。config の設定も書く。
4. **D の試験の program が試しの config に入っていない**
   - `gpu-bars`・`display-inventory` は `config-visionfive2-desktop.mk` の `ZEDBSD_USER_PROGRAMS` にありません。p004 の config に足すと明記してください。
   - p004 の image で graphical login を y にすると、sessiond・greeter と `gpu-bars` が display の claim を取り合う（EBUSY）恐れがあります。E（text に落ちた後）→ D の順にするか、boot を分けると書いてください。
5. **WS270-D1 の選択肢が既存の命名の慣習を外している**
   - 既存は、rpi4・amd64 とも名前の無い方が desktop で、最小は `-server` です（`config/ci/config-rpi4.mk` と `config-rpi4-server.mk`、CI も `.github/workflows/ci.yml:50-58` でその対）。
   - (b) の `config-visionfive2-desktop.mk` は逆の命名になります。
   - 直し方: (c)「`config-visionfive2.mk` を desktop にし、今の最小の config を `config-visionfive2-server.mk` にする（WS256・WS269 の手順の config の名前を合わせて直す）」を選択肢に足す。推奨はどちらでもよいですが、慣習との差は書いてください。
6. **WS270-D2 の推奨が費用だけで、(a) の価値の書き方が弱い**
   - (d) は「得られるのは riscv64 の userland の確かめだけ」と書いています。しかしそれこそ、WS269 p010 を待たずに減らせる危険です。riscv64 の上の compositor・shell・app、全部 dynamic の TLS、atomics は一度も動いていません。
   - 推奨 (b) と「p003 の後に見直す」は妥当です。見直しの判断の材料に「(a) で先に潰せる危険」を 1 行足してください。
   - (a) の HAL の補完は、WS269 p004 が持つ `space.c` の user の NOCACHE（WS269 design §3.2 の 5）と同じ関数（`src/hal/riscv64/space.c:1897-1907`）を触ります。p005 の依存に WS269 p004 を足すか、同じ担当にまとめると書いてください。
7. **前置きの揺れ**: §2.3 と §5 の「WS269 D7 (a)」は、ヘッダの「WS269-D5 (a)」と前置きの形が違います（レビュー 1 Minor 8 の続き）。
8. **riscv64 の Noct の判断がどこにも登録されていない**: 「Q1 が WS270 の外で判断する」とありますが、future-work.md にも master.md にも項目がありません（queue.md の q1338 の行だけ）。Q1 の記録の件です。
9. **数値は確かめ直していない**: §1.1 の結果（bin 215 個、`.so` 23 個、warning 0）は確かめ直していません。P1 の worktree（`/home/awe/zedBSD-worktrees/p1/build/`）に `visionfive2` の出力は残っていません。

---

### WS270-D1・D2・D3・D5 の選択肢と推奨の妥当さ

| ID | 判定 |
| --- | --- |
| WS270-D1 | 推奨 (b) の理由（最小の config を実機の debug に残す）は妥当。命名の慣習の案 (c) を足すべき（Minor 5） |
| WS270-D2 | 推奨 (b)＋p003 の後に見直す、は妥当。費用 (a)〜(f) の根拠は確かめた範囲で合っている（space.c・device.c・pci-ecam-fdt.c。QEMU の窓の大きさは未確認）。価値の側と WS269 p004 との重なりを足す（Minor 6） |
| WS270-D3 | 推奨 (a) は妥当。§1.2 の「else の側」は正しい。(b) は p007 の結果で選び直せる |
| WS270-D5 | 推奨 (a)（SD の root を大きくする）は妥当。`RISCV64_NATIVE_ROOT_MIB ?= 256`（`platform/riscv64/rootfs.mk:24`）で config から変えられる。QEMU の走り D は WS256 p015 の T1-739 で 2 回 PASS（`plan/ws256/phase015/phase.md`）なので、p003 の道として使える |

---

### 結論

**p001 は今のままでは cleared にしない事を勧めます。** ただし、全面の再レビューは要らないと考えます。

- レビュー 1 の B1・M1〜M7・Minor 1〜12 は直っています。
- 新しい Major の N1〜N4 は、どれも受け入れ 2（画面なしで確かめる方法）か受け入れ 3（Phase の分割と依存）に直接かかります。
  - N1・N2 は p006 の分割と依存、N3 は段 2 の持ち主の話で、Q1 が他 WS と調整する必要があります。
  - N4 は p003 の試験の中身です。
- 局所の直し（版 2.1）で済む範囲です。版 2.1 で N1〜N4 と Minor 1・3・4 が直った事を、Q1 が diff で確かめれば cleared にしてよいと考えます。
- WS270-D1・D2・D3・D5 は推奨が妥当なので、ユーザーへの提示は版 2.1 と並行してかまいません（D1 は Minor 5 の案 (c) を足してから）。
- p003 は、N4 と Minor 3・4 を反映した試験の config ができるまで待つのが安全です。

### 未実施

build、host 試験、QEMU、実機は行っていません。次の点は確かめていません。
- lld が riscv64 の非 PIC の object を PIE に link する時の実際の error
- QEMU virt の 32 bit の PCI の窓の大きさ
- U74 の uncached の読みの帯域
- powervr の worker が `hw.gpu.attaching` を数えているか（WS269 p005 の範囲）
- 試しの build の数（bin・`.so`）
