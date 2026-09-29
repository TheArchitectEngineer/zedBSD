<!-- awesome-plan project=zedbsd record=ws075p009 -->

# ws075-p009: 安定: BUG-056・BUG-057 ほか p002〜p008 で出た bug

Phase ID: `ws075-p009`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-29。デモの形の実機の run で App Home の 8 つの app が全て起動、20 分の負荷で停止・失敗 0。見つけた 2 つの i915 の不具合（render の context が 8 つまで、compiler の OpSwitch）を直した）
Phase disposition: normal
承認: 2026-09-29 main の依頼（WS075 の i915 の subagent、項目 2）。

## 現状（2026-09-29 の調べ）

| bug | 状態 | この Phase で |
| --- | --- | --- |
| [BUG-056](../../bugs/BUG-056.md)（zdesktop で compositor が client の後片付けの後に終わる） | resolved（2026-09-28、BUG-077 と同じ原因、GPU core の修正 404830b4） | デモの形の連続の実機の run で再発が無いことを見る |
| [BUG-057](../../bugs/BUG-057.md)（GLX の zgears が数百 frame の後に止まる） | resolved（2026-09-27、ws069-p010: kernel の unix socket の待ちが waitq_sleep の EAGAIN を失敗にしていた） | 同上（Gears を回し続ける） |
| [BUG-058](../../bugs/BUG-058.md)（App Home の zgears が最初の frame の前に終わる） | tracking（p017 の 6 回で再現せず） | 同上（App Home から Gears を起動） |
| [BUG-085](../../bugs/BUG-085.md)（wlkill の直後に compositor が止まる） | tracking（p015 の 11 回で再現せず） | 同上（client の終わりの後も描画が進むこと） |
| [BUG-094](../../bugs/BUG-094.md)（HAL の起動時の時間の測定が vCPU の停止で狂う） | scheduled（p015 で `lapic.c`・`timecounter.c` を修正、修正の後の 10 回で 0。p015 は cleared） | p015 の証拠と、この Phase の起動を合わせて resolved にする |
| [BUG-095](../../bugs/BUG-095.md)（capture の image の power-off） | tracking（原因は init の oneshot。base system の判断） | 範囲外（i915 でない）。触れない |

p002〜p008 で新しく出た i915 の bug は無い（p008 の「request の終わりの 150 回に 1 回が 1 ms の期限で見つかる」は安全網の働きで、不具合ではない）。

## 範囲

デモの構成（内蔵 LCD、`display=edp`、自動の login の session、App Home の application）での連続の実機の run で、上の bug の再発が
無いことを確かめる。再発したら gdbstub で原因を調べて直す（i915 の側）。

非範囲: GPU の engine の reset（`drv_i915_worker_engine_reset` の未実装の道。GPU の hang は今も device lost で compositor が終わる）。
大きいので、要れば別の Phase。

## 手順（実機の passthrough、Phase の終わりに 1 回、`flock /tmp/i915-hw.lock`）

1. デモの passthrough の image（main を merge した tree、`build-demo-image.sh ... passthrough ZEDBSD_GRAPHICAL_BOOT=n
   "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`）を `hdmi-h4-hw.sh start`。
2. App Home から Files・Notes・Terminal・PDF Viewer・Browser・Model viewer・Gears・X terminal を順に起動し、各 1 枚を撮る（窓が描かれること）。
3. Gears を開いたまま、周期の drag の負荷（`h4-ctl.py load`）で 20 分以上。途中と終わりに `rate`・`latency` と撮影（描画が進むこと、
   Gears が回ること = 2 枚の窓の中が違う）。
4. 終わりに gdbstub で worker の数（executed・failed、待ちの終わり方）を読む。
5. 判定: 全ての app の窓が出る、描画が最後まで進む（flip の率が落ちない）、worker の failed 0。

## 記録（2026-09-29）

### 見つけて直した i915 の不具合（この Phase の run で発見）

1. **render engine の context が 8 つまで**（`worker.c` の `I915_WORKER_CONTEXTS`）。GPU の node の open ごとに 1 つ要る（compositor、
   各 Vulkan・EGL・GLX の app、X の app の X server）。App Home の app を順に開くと 9 つ目から `drv_i915_worker_context_create` が ENOMEM で、
   app は窓を出さずに終わる（Gears と Browser）。gdbstub で worker を読んで確かめた: `live_contexts=8`。32 に上げた（live の record だけが
   context image・16 KiB の ring・timeline の page を持ち、空きの record は表の entry だけ）。修正の後、全部開いて `live=12`。
   BUG-058（App Home の zgears が最初の frame の前に終わる、tracking）は、この形（context が尽きたときの起動）だった可能性がある（推測。
   BUG-058 の 1 回の run の context の数は分からない）。
2. **compiler が OpSwitch を拒む**（`compiler/spirv.c`）。Browser は窓の後の最初の frame で `vkCreateGraphicsPipelines failed (-8)` で
   終わる（Browser を zterm から走らせた stderr、`build/ws075-shots/ws075-p009-browser-error.png`）。原因は `paint/shaders/display.frag` の
   glslc -O の SPIR-V: 早い return を spirv-opt の merge-return が default だけの OpSwitch で包む（host の `i915-shader-check` が
   「OpSwitch is not lowered」）。OpSwitch を if 変換の edge として lower した: case ごとに selector と literal の IEQ（同じ target の literal は
   OR で 1 本の edge）、default は どの literal でもない channel（default と同じ target の case の channel を含む）、header の construct は
   OpBranchConditional と同じ。32-bit 整数の selector だけ、loop の header への edge は拒む。edge の表の大きさに case の数を足した。
   ws031-p041（OpSwitch）の範囲のうち OpSwitch の部分にあたる。

### 変えた file

- `src/drivers/gpu/i915/worker.c`: `I915_WORKER_CONTEXTS` 8 → 32 と理由の comment。
- `src/drivers/gpu/i915/compiler/spirv.c`: `i915_spirv_lower_switch()`、`switch_edges`（edge の表の大きさ）、file の説明。
- `plan/ws031/tests/i915-vk-lower-test.c`: `test_switch`（下）、refusal の試験から OpSwitch を外した。
- `plan/ws075/tests/switch/`: `switch.frag`（6 literal、同じ target の 2 literal、fall-through、default と同じ target の case、早い return）と
  glslc の `switch.frag.spv`・`-O` の `switch-O.frag.spv`。
- `plan/ws075/tests/hdmi/h4-ctl.py`: `keys` に shift の記号（`> < | & _ :`）と `;`（terminal で redirect を打つため）。
- `plan/bugs/BUG-094.md`・`plan/known-bugs.md` の BUG-094 の行: resolved（下）。

### 検証

| 確認 | 結果 |
| --- | --- |
| build（demo の passthrough の image、`build-demo-image.sh ... passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`） | 成功、この tree の warning 0 |
| host: `plan/ws031/tests/run-vk-host-tests.sh lower`（通常と ASan/UBSan） | PASS。`test_switch`: switch.frag と -O 形を IR の interpreter で 2 × 4096 pixel、C の式と一致 |
| host: spirv・resdispatch・eu・compile・pipe | PASS |
| host: cmd・res・sync・cmdbuf | FAIL（変更の前の tree でも同じ: cmd は link の `drv_i915_batch_emit` の未定義、res・sync・cmdbuf は p003・p005 で実装した opcode を「未実装」と期待する古い assert。この Phase の変更とは無関係、未修正） |
| host: `i915-shader-check` | Browser の display.vert・display.frag とも accepted（前は frag が OpSwitch で拒否） |
| QEMU の boot test（GPU なし） | PASS（`build/ws075-p009/boot-test/login.png`） |
| Venus の回帰 | 未実施（Venus・GPU core は変えていない） |

実機（5330 の VFIO passthrough の QEMU、デモの image、kei の自動の login の session。`hdmi-h4-hw.sh`、lock の下）:

| run | image | App Home の 8 app（Files・Notes・Terminal・PDF Viewer・Browser・Model viewer・Gears・X terminal） |
| --- | --- | --- |
| hw1 | main + p008 | Gears・Browser の窓が出ない（Files は操作の誤り: App Home を閉じる click になった）。gdb: `live_contexts=8` |
| hw2 | + context 32 | Files・Gears が出る。Browser は出ない（上の 2、zterm で stderr を確かめた）。gdb: `live=11` |
| hw3 | + OpSwitch | **8 つ全て出る**（`build/ws075-shots/ws075-p009-apps-after.png`、前は `...-apps-before.png`） |

hw3 の続き（8 app を開いたまま、Gears は回り続ける）: `h4-ctl.py load 1200 4` で X terminal の窓を 4 秒ごとに往復に drag を 20 分（300 回、
`load.log`）、5 分ごとに撮影（`build/ws075-shots/ws075-p009-soak.png`: 窓が動き、Gears の歯車の角度が変わる）。終わりに gdbstub で worker:
executed 259634・**failed 0**・live 12、待ちは割込みで 274910・1 tick で 1120613。停止・fault・device lost の形は無い（BUG-056・057・085 の
形の再発 0）。`rate`・`latency` は負荷の前後で同じ（下）。

### 性能の所見（p018・p019 へ）

8 app を開き Gears が回る desktop では、入力の続く間 **3.1 flip/s**、入力から flip まで中央値 **166 ms**（132〜215）、入力なしでも 3 flip/s
（Gears の frame）。desktop だけのときは 19.6 flip/s・49 ms（p008）。GPU の仕事は 1 つの worker で直列（全 client の submit と present が
順に run_sync で待つ）なので、client が増えると compositor の frame が待たされると見る（推測、内訳は p018 で測る）。デモでは Gears のような
連続描画の app を開いたままにすると操作が重くなる。p018（非同期の実行器）の受け入れの物差しにこの構成（8 app と Gears）を足す。

### bug の扱い

- BUG-056・BUG-057: resolved のまま。デモの形の 20 分で再発なし（実機の passthrough の証拠）。ticket は変えていない。
- BUG-058: tracking のまま。この Phase の context の上限は同じ症状（App Home の Gears が窓を出さずに終わる）を確実に起こしたので、
  原因の候補として ticket への追記を main に依頼する（この subagent は指示された ticket の他は変えない）。
- BUG-085: tracking のまま。client を開いたまま 20 分で compositor の停止なし。
- BUG-094: resolved にした（p015 の 10 回と p008・p009 の 5 回の起動で再現なし）。
- 新しい bug の ticket は起こしていない（2 つとも この Phase で直した）。登録が要るなら main に依頼する。

### 未実施・残り

- 素の 5330（USB の image）: 未実施（ユーザーの作業）。確かめる点: App Home の 8 app が全て開くこと、特に Browser と Gears。
- engine の reset（GPU の hang からの回復）は未実装のまま（非範囲）。
- 古い host の fixture（cmd・res・sync・cmdbuf）の失敗は既存。直すなら別の Phase。
