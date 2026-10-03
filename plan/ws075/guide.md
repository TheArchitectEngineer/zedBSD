<!-- awesome-plan project=zedbsd record=ws075-guide -->

# WS075 の作業の手引き（2026-10-01）

WS075（i915 のネイティブ実行器で今のデスクトップとグラフィックスを Dell Latitude 5330 で動かす）を、追加の調査なしで続けるための手引き。
正本は [ws.md](ws.md) と各 `phaseNNN/phase.md`。この文書は指示ではなく道案内で、食い違ったら ws.md・phase.md・[master](../master.md)・[AGENTS.md](../../AGENTS.md) が優先する。
2026-10-01 の survey（file の読み取り、`make -n`、host の試験の一部の実行）で確かめた。実機・QEMU は動かしていない。

## 1. ゴール

### 完了の条件（ws.md の「受け入れ」）

| # | 条件 | 今 |
| --- | --- | --- |
| 1 | 実機（5330、capture）で zdesktop の glass・backdrop・tab・System Menu が Venus と同じ形（`CAPTURE=zdesktop`・`plan/tools/titlebar/menu-hw.sh`） | 満たす（p001・p002、2026-09-27） |
| 2 | 実機で egltest の es2・es3、GLX の zgears と glxtest の `--gl3`・`--gl31`・`--gl32` が通る（通らない機能は device の feature として断り記録） | 一部。es3 の targets・blits・queries・feedback は p006 で 0（実機 capture）。`--gl32`（geometry shader）は p007（未着手）。glxtest の記録は無い |
| 3 | host の i915 の shader の検査（`shader-survey/run.sh`）で client と libGLESv2 の shader が全て通る | p001 の時点。p004〜p006 の後の再実行の記録は無い（未確認） |
| 4 | 変えた source の規約の全文との照合、回帰（Venus の回帰と boot test、実機の回帰） | p010（planning、最後） |

完了の宣言は p010 で 1〜4 を確かめてから。cleared の Phase がそろっても WS の受け入れの証明にはならない。

### デモ（fg010）で支える場面

| 場面 | 内容 | WS075 の役割 |
| --- | --- | --- |
| S11 | 窓 10 個の移動・resize・Wiseview・最大化 | WS099 の C6（pointer の移動から表示まで中央値 50 ms 以内）の i915 の側。段 L1〜L3（ws.md の「段の計画」） |
| S1〜S15 全て | 5330 の内蔵 LCD（`display=edp`）で Keiland と app が i915 で描かれる | 描画の正しさ・止まらないこと（BUG-117 の類） |
| S13 | GPU の compute | WS101 が持つ（i915 の compute の executor は WS101 が直した） |

### ユーザーの判断（守ること）

| 日付 | 判断 | この WS への意味 |
| --- | --- | --- |
| 2026-09-30 朝 | WS099 の C6 は **50 ms を保つ**（届かなければ 10/10 に見直す）（master の判断の表 #6） | 基準は 50 ms。今は L2 の 67.3 ms |
| 2026-09-30 朝 | 「広く浅く」: 段（L1・L2・L3）ごとに数値目標を持つ 1〜2 時間の Phase | 1 つの Phase で深掘りしない |
| 2026-09-30 | すりガラスの blur は窓ごと、既定は無効、Settings だけ有効。Files と keyboard も無効のまま | compositor の既定を変えない |
| 2026-09-30 午後 | **描画の高速化はラップアップ、優先を下げ機能性へ**（「いったん描画の高速化はラップアップして…機能性の実装にフォーカスしましょう。」） | L3（p031 以降）は**ユーザーが再開を言うまで着手しない**。C6 は L2 の 67.3 ms で止める |
| 2026-09-29 | MAILBOX・IMMEDIATE はデモの後（F-057） | present mode を触らない |
| 2026-09-29 夜 | 10-10 ごろまで新規実装、10-10〜10-17 は bug の修正と実機の調整だけ | 10-10 以降は新しい機能（p006 の残り、p007）を入れない |
| 2026-09-29 | 表示の build: GPU の driver を直す Phase だけ logo を消す（下の §5） | 実機の image の作り方 |
| master の優先順位 | WS075 は「デモ critical の残り」（4 番目の群）、描画の高速化は止めたまま | 上位の WS の後 |

## 2. 今の状態

### 済み（証拠つき）

| 範囲 | Phase | 証拠（phase.md の結果の節） |
| --- | --- | --- |
| desktop・client の shader と command、zdesktop の capture | p001・p002 | 実機 zdesktop 6/6、home 4/4、menu 11/11、x11 6/6 |
| topology・GLES 2 の compiler・texture の種類 | p003〜p005 | vkx 9/9・vke1・vke2 17/17・vkc 9/9、egltest の 7 場面 failures 0 |
| HDMI の主出力・デモの形・lease の替わり目 | p011〜p013、p016 | 実機の passthrough（HDMI は 2026-09-29 に LCD のみの構成へ、WS084） |
| 安定: BUG-091・BUG-085・BUG-094・BUG-117 | p014・p015・p025、p009 | 各 phase.md。BUG-117 は stress 100 回で消失 0 |
| 性能: 完了の割込み・FIFO の先行・RPS・compiler の guard と分岐の ALU の skip | p008・p019・p020・p021・p023 | 10 app の latency 132 → 32 ms（旧物差し）、RPS で rate 19.7 → 54.8/s |
| C6 の物差しと段 L1・L2 | p024・p026〜p029 | **L1: C6 中央値 91.3 ms（p027）、L2: 67.3 ms・p90 102.6 ms（p029、5 run、Settings を含む 10 app）**、stress 100 回で停止 0 |
| L3 の計測 | p030 | frame 約 45 ms、小さな draw 1 つ約 25 µs、文字が約 400 draw/frame |

全て **5330 の QEMU passthrough** の証拠。**素の 5330（bare metal）での段の確認は未実施**（ws.md「素の 5330 での確認は段ごとに 1 回」）。

### 開いているもの

| 物 | 状態 | 再開の条件 |
| --- | --- | --- |
| [p006](phase006/phase.md) | in-progress、段の外（後回し） | texel buffer（samplerBuffer）・sampler2DMS の texelFetch、増分 6 の実機の再確認。デモの台本は使わない。10-10 より前に main が選んだとき |
| p007（GL 3.2 の geometry shader・gl_Layer・PrimitiveId） | planning、段の外、phase.md 無し | 着手前に分ける（ws.md）。受け入れ 2 の `--gl32` は「feature として断る」でも満たせる |
| p010（規約の照合・統合回帰） | planning、phase.md 無し | WS の完了の直前（全 Phase の後） |
| [p017](phase017/phase.md)（BUG-058 の再試験） | uncleared（6 回で再現せず） | BUG-058 が再発したとき |
| [p018](phase018/phase.md)（非同期の実行器） | uncleared（計測で効果が小さい見込み） | L3 の手 3。描画の高速化の再開の後 |
| [p022](phase022/phase.md)（scoreboard の緩和） | uncleared、patch 保留 | 命令の並べ替えを入れるとき |
| [p031](phase031/phase.md)（L3: 文字の draw をまとめる） | uncleared、patch `phase031/exp/text-batch.patch` | **ユーザーが描画の高速化の再開を言うとき**。2026-10-01 の確認: patch は `git apply` では当たらない（compose.c・glass.c が後で変わった）、`git apply --3way` なら衝突なく当たる（`--check` で確認）|
| （候補）draw ごとの pipeline の停止と flush の削減 | planning | p023 の (b) で効かなかった。L3 の手 2 と合わせて考える |
| （候補）GPU の object の枠 128（BUG-120） | planning | 下の提案 p032 |

### bug（[Bug Board](../known-bugs.md)）

| bug | 状態 | この WS での扱い |
| --- | --- | --- |
| [BUG-120](../bugs/BUG-120.md) | reproduced（passthrough）/ tracking | 窓 30 個で `gt memory: object pool exhausted`。`I915_GT_MAX_OBJECTS 128U`（`src/drivers/gpu/i915/memory.h:73`）。デモの 10 窓では出ない。提案 p032 |
| [BUG-058](../bugs/BUG-058.md) | tracking | p017。再発時に setup の段の印と SIGSEGV の handler |
| [BUG-085](../bugs/BUG-085.md) | tracking | 再発時に `plan/ws075/tests/bug085-hw.sh` の印の後に gdb |
| [BUG-095](../bugs/BUG-095.md) | tracking（base system の判断） | capture の image の power-off が終わらない。試験の終わりは `hdmi-h4-hw.sh stop` が QEMU を quit する |
| [BUG-117](../bugs/BUG-117.md) | resolved（p025） | 素の 5330 での確認が残り |
| [BUG-121](../bugs/BUG-121.md)・[BUG-122](../bugs/BUG-122.md) | resolved（WS099） | 再発時は `plan/ws099/tests/resize-hw.sh` |
| BUG-123 | resolved（WS073、desktop-probe） | i915 と無関係。5330 での確認は WS094 の側 |

## 3. 次の作業の順番

描画の高速化はユーザーの指示で止めている。機能性・安定・デモの実機を先にする。各 Phase は Queue に入れて 1 つずつ（AGENTS.md）。

| 順 | Phase | 目的 | 条件・時期 |
| --- | --- | --- | --- |
| 1 | **提案 p032**（新）: BUG-120 の object の枠 | 窓 30 個で GPU の object が尽きない | 10-10 より前。デモの 10 窓には要らないので、上位の WS の後 |
| 2 | **提案 p033**（新）: 素の 5330 での S11 の確認（L2 の段の実機の 1 回） | デモの実機で窓 10 個が止まらず動くことを、ユーザーの目と ssh の数値で確かめる | ユーザーが 5330 を USB で起動できるとき。WS084 の p003（10 回の起動）と同じ機会にまとめてよい |
| 3 | [p006](phase006/phase.md) の残り | texel buffer・sampler2DMS、増分 6 の実機の再確認 | 段の外。main が選んだとき、10-10 より前 |
| 4 | p007（分けてから） | GL 3.2 の stage | 段の外。断る方針でよいかを main に確かめてから |
| 5 | p010 | 規約の全文との照合・統合回帰（WS の完了の条件） | 全 Phase の後 |
| 保留 | [p031](phase031/phase.md)→ L3 の手 2・3（p018） | C6 50 ms | **ユーザーが描画の高速化の再開を言うとき**だけ |

### 提案 p032: BUG-120（GPU の object の枠）

| 項目 | 内容 |
| --- | --- |
| ID | `ws075-p032`（次の番号。登録は main） |
| 目的 | `gt memory: object pool exhausted` を窓 30 個で出さない |
| file | `src/drivers/gpu/i915/memory.h:73`（`I915_GT_MAX_OBJECTS 128U`）、`memory.c:141`（`drv_i915_gt_object_create`、`:163` の線形の探索、`:173` の log）。object の表は `gm->objects[]` の固定の配列 |
| 手 | (a) 上限を 512 に上げる（表の大きさ × object の struct の大きさが kernel の 16 MiB の上限（master の Tools: `AMD64_KERNEL_MAX_BYTES`、.bss を含む）に入るかを `nm --size-sort build/<W>/vmunix | grep objects` で確かめる）、または (b) 表を heap から確保。まず (a) の大きさを計算して決める |
| 受け入れ | passthrough で窓 30 個（apps8.sh を 3 周）を開いて exhausted 0、stress-117 の 100 回で停止 0、test-hw の vkx・vke1・vke2・vkc PASS、kernel の build の warning 0、boot test PASS |
| 注意 | GPU の driver を直す Phase なので image は logo 無効（§5）。kernel の上限に注意 |

### 提案 p033: 素の 5330 での S11（L2 の実機）

| 項目 | 内容 |
| --- | --- |
| ID | `ws075-p033`（登録は main） |
| 目的 | passthrough で測った L2 を素の 5330（firmware の画面の takeover の後）で 1 回確かめる。bare metal には QMP が無いので C6 は測れない。数値は guest の kernel の `perf:` の行（ssh の `dmesg`）、見た目はユーザー |
| 手順 | ユーザーが demo の既定の image（§5 の A）を USB から起動 → 10 app を開く → ユーザーが 1 分窓を動かす → エージェントが `ssh root@<IP> dmesg | grep 'i915: perf:'`（WS084 の demo-lcd3 と同じ読み方: present/s、submit ごとの GPU ms） |
| 受け入れ | ユーザーの目視で止まり・崩れ 0、窓の操作中の present 14/s 以上（p027 の passthrough の 10 app の flip の率 14〜20/s と比べる）。数値はあくまで参考で、C6 の判定ではないと記録する |
| 未知 | 素の 5330 の IP（前回 10.0.30.5、DHCP で変わる）。ユーザーに聞く |

## 4. 未知と調べ方

| 未知 | なぜ要るか | 調べ方（file:line、道具） |
| --- | --- | --- |
| U1 draw 1 つの固定の費用（約 25 µs）の内訳 | L3 の手 2（状態の書き直しを減らす）の効き | draw ごとに `i915_draw_build_batch`（`render/draw.c:896`）が `drv_i915_gfx_emit_context_setup`（`render/state.c:748`、`draw.c:920` から）を呼び、PIPELINE_SELECT（`state.c:773`）と STATE_BASE_ADDRESS（`state.c:783`）を毎回出す。p030 の手 2: SBA と PIPELINE_SELECT を batch の最初だけにした 1 run の実験の build で C6 を 1 run 測る（source は main に出さず `phaseNNN/exp/` に patch）。PRM の根拠は Mesa の genxml（`/home/awe/p014-c/mesa/src/intel/genxml/gen120.xml` の STATE_BASE_ADDRESS・PIPELINE_SELECT、`/home/awe/p014-c/mesa/src/intel/compiler/` の brw_*）と Linux の参照（`plan/ws031/linux-parity/linux-reference/i915-src/`）。**描画の高速化の再開まで着手しない** |
| U2 slot が尽きて frame の途中で run が走る | run の数（1 frame 4〜7）と同期の往復 | `render/heap.h:40-41`（slot 16 KiB × 128）、`render/draw.c:328`（`slot_next >= I915_GFX_SLOTS` で途中の run）。p028 で 512 は効かなかった（記録） |
| U3 executor の frame の内訳 | どこに時間が行くか | executor の session ごとの log（`render/draw.c:1137` の `i915: vk: session N: frames ...`、p026 で常設）、`plan/ws075/tests/hdmi/engine-gdb.sh`（gdbstub で `i915_worker_run`（`worker.c:1149`）の context の engine_ns を読む、kernel log を使わない） |
| U4 compiler の命令の正しさ | 新しい shader（text.frag など）や compiler の変更 | `plan/ws075/tests/guard/run.sh`（Mesa の brw_disasm・brw_asm と byte で比べ、scoreboard を `guard/scoreboard-check.h` で検査。道具は `/home/awe/p014-c/mesa/build-asm/src/intel/compiler/`、2026-10-01 に存在を確認、PASS 1 秒）。1 つの SPIR-V は `shader-dump vertex|fragment FILE.spv OUT.bin`（`guard/shader-dump.c`）。分岐の skip は `compiler/compile.c:3585`（`i915_compile_skips`）〜`:4124`、guard の texture は `:3536`、scoreboard は `compiler/eu.c:1205`・`:1839` |
| U5 shader の不足（受け入れ 3） | 受け入れ 3 の再確認 | `plan/ws075/tests/shader-survey/run.sh build/<W>/shaders`（先に `plan/ws068/tests/glsl-host/run.sh` で `build/ws068-glsl-host` を作る、run.sh の先頭）。出力 `modules.txt`・`gaps.txt`・`first.txt`。未実行（2026-10-01） |
| U6 GT の周波数（素の 5330） | 素の 5330 で RPS が上がるか（p020 は passthrough のみ） | `gt-power.c:1162`（`i915_rps_tick`、busy の時間の評価）、`:316`（`drv_i915_rps_enable`）。passthrough は `h4-ctl.py freq SECONDS MS move`（RPNSWREQ と CAGF を BAR から）。素の 5330 は `dmesg | grep 'i915: rps'` |
| U7 BUG-120 の窓あたりの object の数 | p032 の上限の決め方 | `memory.c:141` の確保の数を、窓を 1 個ずつ開いて `gm->objects[]` の in_use を gdbstub で数える（engine-gdb.sh の形で symbol を nm から） |
| U8 p006 の scenes_shown が false（p005 の 7 場面の capture が同じ絵） | 受け入れ 2 | p006 の「検証」表の最後。capture の更新の遅れか停止か。`CAPTURE=zdesktop-egltest KEILAND_APP=egltest BUILD=... plan/ws031/tests/vkloop-hw.sh zdesktop` の再実行と result.json の hash |
| U9 PRM の疑問一般 | 新しい 3DSTATE・message | 先に Mesa の genxml（gen120.xml、Alder Lake-P は Gen12 = `gen120.xml`）、次に Linux の参照（上）。repo の `src/drivers/gpu/i915/intel/genxml.h` が使う定義と照らす。`plan/ws031/mesa-refs` は自己を指す壊れた symlink で使えない（2026-10-01 確認） |

## 5. コマンド

全て repo の root から。一般の build・回帰は [plan/ws104/commands.md](../ws104/commands.md) の §0（守ること）・§1（build と warning の数え方）・§4（boot test）・§5（C1・C2・C9）・§7（glass の見た目）を使う。
**image の build を 2 つ同時に走らせない。BUILD は Phase ごとに別にする（`build/<W>`、`<W>` は例 `ws075-p032`）。**

### 5.1 kernel だけ（driver の compile の確認、image は作らない）

```
mkdir -p build/<W>
make -j64 BUILD=build/<W> ZEDBSD_CONFIG=plan/ws075/demo/config-demo-hdmi.mk I915_TEST_VBT=y build/<W>/vmunix > build/<W>/kernel.log 2>&1; echo "make exit=$?"
grep -E ':[0-9]+:[0-9]+: warning:' build/<W>/kernel.log | wc -l
```

`make -n` で target `build/<W>/vmunix` が i915 の source を compile することを確かめた（2026-10-01）。実行時間は未計測。

### 5.2 実機の image（`plan/ws075/demo/build-demo-image.sh`、引数は `:11`、`passthrough` は `:26` で `I915_TEST_VBT=y`）

| 用途 | コマンド |
| --- | --- |
| A. デモの既定（logo、`kmsg=quiet`、`display=edp`）、USB で素の 5330 | `plan/ws075/demo/build-demo-image.sh build/<W>-usb` |
| B. GPU の driver を直す Phase の素の 5330（logo なし、kernel の message を画面に） | `plan/ws075/demo/build-demo-image.sh build/<W>-usb ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"` |
| C. passthrough（QEMU、hdmi-h4-hw.sh・measure-apps.sh 用） | `plan/ws075/demo/build-demo-image.sh build/<W>-pt passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"`（p027 の形） |

- 出力は `BUILD/hdd-image.img`、kernel は `BUILD/vmunix`（measure-apps.sh の 2 番目の引数）。
- **graphical boot が有効（A）のとき `login=graphical` を足さない**（重複で kernel が boot の parameter を拒み起動が止まる、ws103 の 5330 の smoke の事故と auto-memory の記録）。B・C は `ZEDBSD_GRAPHICAL_BOOT=n` と一緒なので足す。
- C の flag の違いは make が見ないので、variant ごとに BUILD を分ける（script の `:19`）。
- warning の確認は commands.md §1 の grep を `build/<W>-pt` の build の出力に。

### 5.3 host の試験（GPU・QEMU なし）

| 試験 | コマンド | 時間（2026-10-01） | PASS の行 |
| --- | --- | --- | --- |
| executor の fixture（10 個、ASan つき） | `sh plan/ws031/tests/run-vk-host-tests.sh` | **2 分を超える**（2 分で cmd〜eu の 7 個が PASS、pipe・cmdbuf・compile は未了）。background で走らせる | `WS031 vk host fixtures PASS: ...` |
| 1 つだけ | `sh plan/ws031/tests/run-vk-host-tests.sh "pipe cmdbuf"` | — | 同上 |
| compiler の guard（Mesa の disasm と byte 比較） | `sh plan/ws075/tests/guard/run.sh` | 約 1 秒、PASS | `ws075-p021 guard host test PASS` |
| shader の survey（受け入れ 3） | `sh plan/ws068/tests/glsl-host/run.sh` の後 `sh plan/ws075/tests/shader-survey/run.sh build/<W>/shaders` | 未計測 | `gaps.txt` が空 |
| 出力の選択（`display=`・EDID） | `sh plan/ws075/tests/hdmi/host-output-test.sh build/<W>/h2` | 約 1 秒 | `host-output-test: 80 checks, 0 failures` |
| driver の contract（RPS など） | `sh src/drivers/gpu/i915/tests/contracts/run.sh rps`（既定は全 7 個） | rps は約 3 秒 | `i915 contract tests PASS: rps (ordinary + ASan/UBSan)` |

### 5.4 QEMU（Venus）の回帰

- boot test: `OUTPUT=build/<W>/boot plan/tools/boot-test.sh build/<W>-pt/hdd-image.img; echo "exit=$?"`（commands.md §4。GPU なしで login prompt。PNG をユーザーに見せる）。
- compositor の基準: commands.md §5（C1・C2・C9）。compositor（`userland/desktop/wayland/`）を変えたら C7（`criteria.sh ... C7`）も（p029 の形）。

## 6. 実機（Dell Latitude 5330）

一般の手順は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（別の agent が作成中。機械と host・passthrough の lock・USB の単独の起動・demo の image の節）。ここは WS075 に固有の所だけ。

### 6.1 2 つの形

| 形 | 何 | 使う道具 | 測れるもの |
| --- | --- | --- | --- |
| passthrough | 5330 の Linux（ssh の alias `solaris10-man` = 10.0.10.25、`~/.ssh/config`）の上の QEMU に iGPU を VFIO で渡す | `hdmi-h4-hw.sh`・`measure-apps.sh`・`stress-117.sh`・`test-hw.sh`・`capture-hw.sh` | QMP で入力、BAR の register（PLANE_SURFLIVE）で flip、C6、GT の周波数、gdbstub |
| 素の 5330 | ユーザーが USB の image（§5.2 A・B）から UEFI で起動。firmware の画面の takeover（WS084）を通る | ユーザーの目、ssh で guest の root（image に `plan/tmp/guest/id_ed25519.pub` が入る、`build-demo-image.sh:42`） | `dmesg` の `i915: perf:`・`takeover` の行、ユーザーの写真 |

**同時には使えない**（素の 5330 で起動している間は Linux の passthrough の host が居ない）。素の 5330 の IP は起動ごとにユーザーに聞く。

### 6.2 passthrough の 1 回の計測（L1・L2 と同じ形）

```
# 1. image（§5.2 C）。build は lock の外で、他の image の build と重ねない
plan/ws075/demo/build-demo-image.sh build/<W>-pt passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"
# 2. C6 の 1 run（lock を取り、120 秒待ち、desktop → 10 app → rate・latency・c6 40 試行 → engine-gdb → 撮影 → 返す）
plan/ws075/tests/hdmi/measure-apps.sh build/<W>-pt/hdd-image.img build/<W>-pt/vmunix build/<W>/m1
# 3. 同じ image で m2〜m5 を 1 つずつ（並べない）。まとめ:
python3 plan/ws075/tests/hdmi/c6.py build/<W>/m1 build/<W>/m2 build/<W>/m3 build/<W>/m4 build/<W>/m5
```

- measure-apps.sh（`plan/ws075/tests/hdmi/measure-apps.sh:17-37`）は `hdmi-h4-hw.sh start`（`flock /tmp/i915-hw.lock` を background の `h4-lock.sh` が持つ、`hdmi-h4-hw.sh:48-51`）から `stop` まで自分で行う。1 run 約 10 分（未計測の目安）。
- 結果: `build/<W>/mN/measure.txt`（`c6 samples:` の行、`rate:` の行）、`shots/`（`a-<app>-live.png` は apps8.sh の各 app の後、`end1`・`end2`）、`kernel.log`、`guest-logs.txt`。
- c6.py の読み: run ごとの中央値・p90、全体の中央値・p90、run の中央値の幅、判定（`LIMIT_MS = 50.0`、`c6.py:18`）。段の判定は全体の中央値（L1 100、L2 75、L3 50 ms）。

### 6.3 止まらないこと（stress）

```
plan/ws075/tests/hdmi-h4-hw.sh start build/<W>-pt/hdd-image.img build/<W>/s1
sleep 120
plan/ws075/tests/hdmi/apps8.sh 8000
plan/ws075/tests/hdmi/stress-117.sh 100 | tee build/<W>/s1/stress.txt
plan/ws075/tests/hdmi-h4-hw.sh stop build/<W>/s1
```

PASS: 最後の行 `stress-117: no stop`（各 round は `C n: N flips in 3 s`、0 flip の round で止まる、`stress-117.sh:17-24`）。`start` は lock を待つ（timeout で切らない、`hdmi-h4-hw.sh:16-20`）。

### 6.4 executor の試験の場面と capture

| 試験 | コマンド |
| --- | --- |
| vkx・vke1・vke2・vkc（期待: 9/9・6/6・17/17・9/9） | `I915_HOST=solaris10-man BUILD=build/<W>/vkx plan/ws075/tests/test-hw.sh vkx build/<W>/hw-vkx`（vke1・vke2・vkc も同じ形、BUILD を場面ごとに） |
| zdesktop の capture（受け入れ 1） | `I915_HOST=solaris10-man BUILD=build/<W>/zd plan/ws075/tests/capture-hw.sh zdesktop zdesktop build/<W>/hw-zdesktop` |
| files の場面（9 検査） | `I915_HOST=solaris10-man KEILAND_APP=home BUILD=build/<W>/files plan/ws075/tests/capture-hw.sh files zdesktop build/<W>/hw-files` |
| System Menu（11 検査） | `I915_HOST=solaris10-man plan/tools/titlebar/menu-hw.sh build/<W>/hw-menu` |
| 窓の角の drag（BUG-121） | `plan/ws099/tests/resize-hw.sh build/<W>-pt/hdd-image.img build/<W>/resize 20`（`RESIZE-HW RESULT drags=N vanished=0` と `resize-hw: PASS`） |

- **`test-hw.sh`・`capture-hw.sh`・`menu-hw.sh` の既定の host は `awe@10.0.30.3`（`test-hw.sh:20`、`capture-hw.sh:20`）で、今は届かない。必ず `I915_HOST=solaris10-man`**（p023・p024 の形）。
- test-hw.sh は `vkloop-hw.sh test` を呼び、**lock の中で image を build する**（`vkloop-hw.sh:211`）。他の image の build と重ねない。lock の外で先に作るなら `VKLOOP_BUILD_ONLY=1`（`vkloop-hw.sh:221`）。
- 判定の行: `build/<W>/hw-vkx/run.log` の `verdict`（test-hw.sh の最後の grep）。

### 6.5 その他の観察の道具（`h4-ctl.py`、`hdmi-h4-hw.sh ctl` 経由、`start` の後だけ）

| コマンド | 出力 |
| --- | --- |
| `plan/ws075/tests/hdmi-h4-hw.sh ctl rate A 10` | `rate: N flips ...`（pointer を 8 ms ごとに動かす間の flip/s） |
| `plan/ws075/tests/hdmi-h4-hw.sh ctl c6 A 40 960 100` | `c6 samples: ...`、中央値・p90（壁紙の上の 960,100 で） |
| `plan/ws075/tests/hdmi-h4-hw.sh ctl freq 10 100 move` | GT の要求と実際の周波数（MHz）の平均・最小・最大 |
| `plan/ws075/tests/hdmi-h4-hw.sh ctl shot NAME` → `fetch OUTDIR` | `shots/NAME-live.png`（画面の buffer、`h4-png.py`） |
| `RATE_XY="1700 1000" plan/ws075/tests/hdmi/engine-gdb.sh build/<W>-pt/vmunix 10` | session ごとの engine の占有・1 run の時間（gdbstub） |

### 6.6 画素の比較（p031 の再開、compositor を変えた Phase）

`pixdiff2.py` と `build/ws075-p031/hw.sh` は P1 の worktree（消えた）にあり、repo に無い（2026-10-01 確認）。代わりに apps8.sh の撮影を PIL で比べる:

```
python3 - build/<W>/base/shots build/<W>/new/shots <<'EOF'
import sys, glob, os
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(a + '/a-*-live.png')):
    q = os.path.join(b, os.path.basename(p))
    if not os.path.exists(q): print(os.path.basename(p), 'missing'); continue
    box = ImageChops.difference(Image.open(p).convert('RGB'), Image.open(q).convert('RGB')).getbbox()
    print(os.path.basename(p), 'same' if box is None else 'diff ' + str(box))
EOF
```

違ってよいのは時刻の文字（system bar、Files の挨拶、Notes の file 名）と Gears の animation だけ（p023・p028 の基準）。diff の box がそれ以外にかかれば FAIL。

## 7. 注意

- **HAL**（`include/hal/hal.h`）と **UAPI**（`include/drivers/gpu.h`、`include/uapi/gpu*.h`）の変更は差分ごとに事前の承認。差分は `plan/ws075/proposed/` に置き、適用しない（ws.md の規則・Guardrail）。`src/hal/` の実装の修正は承認なしでよい。
- **toolchain**（`toolchain/`、`build/llvm*`、`build/NoctLang`、clang・libcxx の package）は変えない・build しない。lock（`plan/tools/toolchain-lock.sh`）は外さない。
- **実機は同時に 1 つ**: 全ての実機の script は `flock /tmp/i915-hw.lock`。lock は WS099・WS101 と共有。`hdmi-h4-hw.sh start` を timeout で殺さない（lock の待ちが孤立する、`hdmi-h4-hw.sh:16-20`）。終わったら必ず `stop`。
- **image の build は同時に 1 つ**（commands.md §0）。test-hw.sh は lock の中で build する。
- **zdesktop を直さず i915 の側を直す**（ws.md の規則）。compositor（`userland/desktop/wayland/`）に触れるのは main の許可の後（p029・p031 の形）。
- **証拠を分ける**: passthrough・素の 5330・QEMU の Venus をそれぞれ書く。やっていない確認は「未実施」。素の 5330 の段の確認は今まで 1 度も無い。
- **log で判定しない**（AGENTS.md）。WS075 の過去の Phase は passthrough の kernel.log（debugcon）の `draw refused`・`not a descriptor set`・`exhausted` の grep と、test-hw.sh の serial の verdict を使ってきた。規則との食い違いなので、**判定は register の読み（flip の数・c6）と撮影と host の試験で行い、log の grep を判定に使うなら先に main に確かめる**。
- 段 L3（p031 以降）・present mode（F-057）・blur の既定は、ユーザーの再開・判断なしに触らない。
- 10-10 以降は bug の修正と実機の調整だけ（新しい機能を入れない）。
- commit は `git commit -m WIP -- <自分の path>` だけ。push しない。`.internal/` を読まない。集約の `make check` を走らせない。
