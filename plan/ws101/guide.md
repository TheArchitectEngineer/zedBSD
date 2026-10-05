# WS101 作業の手引き（2026-10-01、Q1 の survey）

この file だけを読めば WS101 を続けられるように書いた。記録の本体は [ws.md](ws.md)・[design.md](design.md)・各 phase.md で、
ここは要約と手順。食い違ったら ws.md・phase.md が正しい。command は全て **repo の root（`/home/awe/zedBSD-claude1`）から**実行する。
`<W>` は作業の名前（例 `ws101-p019`）に置き換える。

## 1. ゴール

### 達成基準（ws.md「達成基準」）

| # | 基準 | 今 |
| --- | --- | --- |
| G1 | i915 のネイティブの実行器で Vulkan の compute（pipeline・dispatch・indirect・local size・built-in・shared memory と barrier・SSBO と shared の atomic） | **passthrough で済み**（vkcs 21/21、p007）。素の 5330 は未実施（p012） |
| G2 | libglesv2 の OpenGL ES 3.1 の compute（GLSL ES 3.10、`glDispatchCompute`、SSBO、`glMapBufferRange`、`glMemoryBarrier`、headless EGL） | **Venus（p009）と passthrough（p010・p007 の再実行）で済み** |
| G3 | Noct の自動並列化（`accel_opengles.c`）が zedBSD で GPU を選び、整数の DOALL の見本 `mix.nct` が CPU と一致し、CPU より速い | **一致は済み**（p011、passthrough と Venus）。**速さは未達**: GPU が CPU の約 22 倍遅い（p017 の 5330 の追記: GPU 221 ms、CPU 10 ms） |
| G4 | fg010 の台本の場面 S13（Noct の見本を GPU と CPU で走らせて時間を比べる）として見せられる | **passthrough の demo の image で済み**（p015、[s13-live.png](phase015/s13-live.png)）。**素の 5330（デモの本番の形）は未実施** |

### fg010 の場面 S13（master「fg010 の達成基準」）

| 場面 | 内容 | 本番 |
| --- | --- | --- |
| S13 | GPU の compute: Noct の見本を GPU と CPU で走らせ、時間を比べる | 2026-10-17 OSC Tokyo Fall、Dell Latitude 5330 の内蔵 LCD（`display=edp`）、USB boot の demo の image。Terminal で `sh /usr/share/gpudemo/s13.sh` を打つ |

画面に出る行（`userland/desktop/gpudemo/s13.sh`）: `Kei GPU compute: 4000000 integers…`、`CPU: N ms a run`、`GPU: N ms a run (6 kernels ran on the GPU)`、
`The CPU is X times as fast as the GPU.`（または GPU が速い時の文）、`The results are the same on the CPU and the GPU.`。

### ユーザーの判断（全て決定済み。master「fg010 に必要な判断」、ws.md）

| # | 判断 | 決定 |
| --- | --- | --- |
| D1 | libglesv2 が「OpenGL ES 3.1」を名乗るか | 名乗る（2026-09-30「名乗ってOKです」）。compute を持つ device だけ。未実装の 3.1 の関数は error の stub（`userland/desktop/libglesv2/gles.c:1452`） |
| D2 | Noct の accel を ON（toolchain の変更） | 許可、構成で選ぶ（`ZEDBSD_NOCT_ACCEL := y` の amd64 の `/bin/noct` だけ）。main が適用済み（189c3bf1） |
| D3 | N = 4,000,000、倍率 3 倍以上（伸び 10 倍） | 「このまま」。ただし下の「今のまま」で倍率の追求は止めた |
| D4 | ws068-p035 の GLES 3.1 の compute の部分を WS101 へ移す（main） | 記録済み（ws068 の ws.md の p035 の行） |
| D5 | デモの見本は整数だけ | 「整数だけでよい」。Noct の意味は変えない |
| 優先 | 最適化の優先を下げる（2026-09-30「GPUでは疎通できたので、最適化の優先度を下げます」） | WS101 はデモ critical の中で一番低い（master「WS の優先順位」4） |
| 今のまま | p017 の後、次の手（重い見本・Noct の copy 削減・両方・今のまま）の問いに「今のまま」 | **S13 は今の見本で「動くこと」を見せる。最適化（L2・L3、p017・p018）はここで止める**。再開はユーザーが言う時だけ |

### 完了の定義（WS101 を completed にする条件）

1. G1・G2・G3 の一致・G4 が証拠付きで満たされる。G3 の「CPU より速い」は「今のまま」の決定で未達のまま閉じる扱いにするか、
   ユーザーに受け入れ条件の変更を確かめる（**人間の判断。main に出す**。下の 3.2 の提案 A）。
2. p017（uncleared）の扱いが決まる（canceled か、uncleared のまま WS の受け入れから外す。main・ユーザーの判断）。
3. 素の 5330 で S13 が通る（G4。デモの本番の形）。
4. p013（規約の全文との照合と回帰）が cleared。code を作る WS の必須の Phase（AGENTS.md・Awesome Plan §6）。

## 2. 今の状態（2026-10-01）

### 済み（証拠）

| Phase | 結果 | 証拠 |
| --- | --- | --- |
| p001 | 設計 | [design.md](design.md) |
| p002〜p004 | compiler（GLCompute、SLM 以外）・実行器の object・batch | host の試験 PASS（2026-10-01 にも再実行して PASS、下の 6 章） |
| p005〜p007 | 5330 の passthrough で vkcs 9 → 15 → 21/21、回帰 vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9 | [phase007](phase007/phase.md)、`OUT=build/ws101-p007-hw plan/ws101/tests/hw/run-hw.sh r1 vkcs vkx vke1 vke2 vkc` |
| p008 | GLSL ES 3.10 の compute | `plan/ws101/tests/glsl/run.sh` PASS（2026-10-01 再実行 PASS） |
| p009 | libglesv2・libegl の ES 3.1 の compute の API。Venus で glescompute PASS | [phase009](phase009/phase.md) |
| p010 | passthrough で glescompute・egltest feedback・queries PASS | `plan/ws101/tests/hw/gles-hw.sh` |
| p011（p014 を含む） | toolchain の差分（main が適用）、`mix.nct`・`s13.sh`、G3 の一致（passthrough・Venus） | [phase011](phase011/phase.md) |
| p015 | demo の image の Terminal で S13 が通る（passthrough） | [phase015](phase015/phase.md)、[s13-live.png](phase015/s13-live.png) |
| p016 | 時間の分解（QEMU）。道具 `KEI_GLES_COMPUTE_TRACE=2`・`tests/time-split.sh`・`tests/time-split.py` | [phase016](phase016/phase.md) |

### 開いているもの

| 項目 | 状態 | 次 |
| --- | --- | --- |
| **p017**（L2 3 倍） | **uncleared**（2026-09-30）。libGLESv2 の device の buffer の使い回し（spare）と device の上の bytes を実装（main に merge 済み）。QEMU の GPU の call 835 → 212 ms。5330 の passthrough（P1 の追記）: 1 call 215 ms のうち gpu-wait 167 ms（78%）、Noct 35 ms、buffer-data 8 ms。S13 の表示「The CPU is 22.1 times as fast as the GPU.」 | ユーザーの「今のまま」で止めた。受け入れ（GPU ≤ CPU / 3）は未達。扱い（canceled か保留）は main の判断（3.2 の提案 A） |
| p012（L3 素の 5330） | planned（phase.md なし → 2026-10-01 に [phase012](phase012/phase.md) を作った）。依存は表では p017 | 3.1 の順番 1 |
| p018（L3 10 倍） | planned、保留（「今のまま」） | ユーザーが再開を言うまで触らない |
| p013（規約と回帰） | planned（2026-10-01 に [phase013](phase013/phase.md) を作った） | 3.1 の順番 2 |

### bug・Future Work

- WS101 に紐づく Bug Ticket は無い（`plan/known-bugs.md` を 2026-10-01 に検索: ws101・glescompute・libglesv2 の compute の行なし）。
- F-061（ローカルの LLM を GPU の compute で）: WS101 の完了の後。F-064（Venus の同期の呼び出しが約 10 ms 刻み）: QEMU の計測が実機と違う理由の一つ。

### この checkout で壊れている前提（2026-10-01 に確かめた）

repo は `/home/awe/zedBSD-rpi4` から `/home/awe/zedBSD-claude1` に移った。**`/home/awe/zedBSD-rpi4` は存在しない。** 次の script の既定値が古い path を指す:

| file:line | 既定値 | 影響 |
| --- | --- | --- |
| `plan/ws101/tests/hw/g3-hw.sh:20` | `NOCT=/home/awe/zedBSD-rpi4/build/demo-lcd9/bin/noct` | 既定のままでは「no accelerator-enabled noct」で止まる |
| `plan/ws101/tests/hw/g3-hw.sh:24`、`gles-hw.sh:19` | fonts `/home/awe/zedBSD-rpi4/build/ws035-fonts` | `[ -f ]` で飛ばすだけ（font 無しの image になる）。`build/ws035-fonts/Inter.ttf` はある |
| `plan/ws101/tests/demo/build-s13-image.sh:12・15` | NOCT、wallpaper | NOCT は止まる。wallpaper は飛ばす |
| `plan/ws101/tests/noct/g3-venus.sh:21` | NOCT | 止まる |

さらに **`NOCT` を環境変数で渡すと、`userland/base/noct/Makefile:56` の `NOCT ?= …`（host の noct）を上書きし、image の build が
`native-swap-1024m.img` で失敗する**（guest の ELF を host で走らせる。p017 の 5330 の追記で P1 が発見）。`g3-hw.sh`・`build-s13-image.sh` は
`make` を呼ぶので同じ危険がある（`g3-venus.sh`・`time-split.sh` は make を呼ばないので安全）。
→ 直すのは 3.2 の提案 B（p020）。直すまでの回避: script を `build/<W>/` に複写し、変数名を `S13_NOCT` などに変えて走らせる（元の file は変えない）。

accel 付きの `/bin/noct` の在りか（2026-10-01、`grep -a libGLESv2` で確かめた）:

| path | accel | 時刻 |
| --- | --- | --- |
| `build/demo-lcd9/bin/noct` | あり | 2026-09-30 09:03（p011〜p015 で使った物と同じ build） |
| `build/amd64/bin/noct` | あり | 2026-10-01 08:18（`config/ci/config-amd64.mk` の `ZEDBSD_NOCT_ACCEL := y`） |
| `.claude/worktrees/ws101-compute/build/ws101-p011-hw/rootfs/bin/noct` | あり | 2026-09-30 09:32（同じ dir の `bin/noct` は**accel 無し**） |

**main の checkout に S13 の入った demo の image は無い**（2026-10-01）: `build/demo-lcd9/hdd-image.img`（09-30 08:29）と `rootfs/bin/noct`（08:28）は
accel の前の物で、`rootfs/usr/share/gpudemo` も無い（`bin/noct` だけ 09:03 に accel 付きで作り直された）。`build/amd64/rootfs` にも gpudemo は無い
（CI の構成は gpudemo を選ばない）。素の 5330 の S13 には main が demo の image を作り直す必要がある（Noct の build を含むので main の仕事）。

**WS101 は Noct を build しない**（toolchain）。上の file を image に写して使う。worktree `ws101-compute` の git の link は旧 path を指して壊れている
（`git -C .claude/worktrees/ws101-compute log` が失敗）。その中の `build/` は読むだけ。

## 3. 次の作業の順番

### 3.1 既存の Phase（この順）

| 順 | Phase | 目的 | 依存 | 完了の条件 |
| --- | --- | --- | --- | --- |
| 1 | [ws101-p012](phase012/phase.md)（の S13 の部分。3.2 の提案 C で分けるなら p019） | 素の 5330 の demo の image で S13 が通る（G4）、GPU が固まった時の回復の手順を台本に書く、G1 の vkcs を素の機械で（可能なら） | demo の image（main の build）、ユーザーの起動 | phase012 の「完了の条件」 |
| 2 | [ws101-p013](phase013/phase.md) | WS101 の全ての変更の規約の全文との照合と回帰 | p002〜p017 の code。**新規実装の期間（〜10/10）の後半に** | phase013 の「完了の条件」 |
| 保留 | p018（10 倍）、p017 の続き | ユーザーの「今のまま」 | ユーザーの再開の指示 | — |

### 3.2 提案（main の承認が要る。ID は次の番号）

| 提案 | ID | 目的 | 触る file | 受け入れ |
| --- | --- | --- | --- | --- |
| A（判断） | — | p017 の扱いと G3 の「CPU より速い」: (a) p017 を canceled（理由: 2026-09-30 ユーザー「今のまま」）にして G3 の受け入れを「一致と offload の証拠」に縮める、(b) uncleared のまま WS を incomplete で止める。**人間の判断**なので main がユーザーに出す | ws.md・phase017（main） | 決定が ws.md に記録される |
| B | ws101-p020 | 試験の script の path と `NOCT` の衝突を直す: 既定の noct を repo の相対の `build/demo-lcd9/bin/noct`、fonts を `build/ws035-fonts`、wallpaper を `build/ws035-wallpaper/wallpaper-1080.ppm` に。環境変数の名前を `S13_NOCT`（build-s13-image.sh）・`G3_NOCT`（g3-hw.sh）・`NOCT` のまま（g3-venus.sh・time-split.sh は make を呼ばない）に | `plan/ws101/tests/hw/g3-hw.sh`・`gles-hw.sh`・`demo/build-s13-image.sh`・`noct/g3-venus.sh`（WS101 の plan の中だけ。source は触らない） | `bash -n` の 4 本が 0、`grep -rn zedBSD-rpi4 plan/ws101/tests` が 0 件、`G3_HW_BUILD_ONLY=1 BUILD=build/<W>-g3 plan/ws101/tests/hw/g3-hw.sh build/<W>/g3`（image の build だけ）が `g3-hw: built` を出す |
| C | ws101-p019 | p012 から「素の 5330 で S13（G4）と回復の手順」だけを分ける（デモ critical。vkcs の素の機械の run は p012 に残す。素の機械で試験の kernel を起こす方法が未知のため、下の 4 章） | `plan/ws101/phase019/phase.md`、記録だけ（source は触らない） | 素の 5330 の Terminal の画面（写真）に「The results are the same on the CPU and the GPU.」、3 回続けて通る、回復の手順が台本の形で phase.md にある |

p019 を採るなら、[phase012](phase012/phase.md) の手順の A を p019 へ移し、p012 は B だけにする。

## 4. 未知と調べ方

| 未知 | 調べ方 |
| --- | --- |
| 5330 の gpu-wait 167 ms（1 call の 78%）の中身: i915 の実行器の dispatch の実行時間か、submit と完了の同期の往復か | **「今のまま」なので調べるのは再開の時だけ**。方法: passthrough の H4 の run（`plan/ws075/tests/hdmi-h4-hw.sh start`）の間に `plan/ws075/tests/hdmi/engine-gdb.sh VMUNIX SECONDS`（gdbstub で i915 の worker の context ごとの engine の時間・queue の待ち・round trip を読む。script の先頭 1〜10 行）を S13 の GPU の run と同時に走らせる。VMUNIX は image の BUILD の `vmunix` |
| 素の 5330 で S13 の時間・結果が passthrough と同じか | p012（p019）で測る。passthrough は CPU 10 ms・GPU 221 ms |
| 素の 5330 で試験の kernel（vkcs）を走らせる方法 | 今の vkcs は `plan/ws075/tests/test-hw.sh`（passthrough の QEMU）だけ。素の機械で走らせた記録は無い。調べ方: `plan/ws031/tests/vkloop-hw.sh` の image（`VKLOOP_BUILD_ONLY=1` で作る `hdd-image.img`）を USB に書いて素の 5330 で起こせるか、`plan/tools/hw5330/README.md`（作成中）と WS075・WS084 の素の機械の手順で確かめる。分からなければ G1 の素の機械の分を「未実施」と書き、main に報告 |
| GPU が固まった時の回復（デモ中） | design.md §8 の最後の行。手順の候補: (1) Terminal で Ctrl+C、(2) 新しい Terminal で同じ script、(3) session の logout と login、(4) 機械の再起動（電源 button 長押し → USB boot）。どれが効くかは素の 5330 で、固まる状態を作らずに「Ctrl+C で止めて再実行」まで確かめる。**GPU を故意に固めない** |
| `--gpu` が黙って CPU に戻る条件 | Noct は書き換えを断ると黙って CPU で走る（phase011「Noct の書き方で分かったこと」）。offload の証拠は `KEI_GLES_COMPUTE_TRACE=1` の `gles: compute dispatch` の行の数（`userland/desktop/libglesv2/compute.c:494〜510`）。s13.sh は「(N kernels ran on the GPU)」で数を出す（N=6 が正しい） |
| Noct の accel の中身 | `userland/base/noct/noct/src/accel/accel_opengles.c`（2069 行。call ごとの buffer の作成・`glBufferData`・dispatch・`glFinish`・`glMapBufferRange`・`glDeleteBuffers` は 1588〜2040 行）。**読むだけ。変更は toolchain（main の許可）** |
| libglesv2 の compute の時間の内訳 | `KEI_GLES_COMPUTE_TRACE=2`（`userland/desktop/libglesv2/gles.c:226〜250`）で `gles: time step=NAME us=N bytes=B` を stderr へ。集計は `python3 plan/ws101/tests/time-split.py GPU.log CPU.log`。spare の定数は `userland/desktop/libglesv2/gles.h:151〜159`（`GLES_SPARES` 8、`GLES_SPARE_BYTES` 64 MiB、`GLES_SPARE_FRAMES` 8、`GLES_ON_DEVICE_MIN` 64 KiB） |
| compiler・実行器の誤り（再発時） | host の試験 `plan/ws101/tests/host/run.sh`: Mesa 25.0.7 の `brw_disasm`・`brw_asm`（`BRW_TOOLS`、既定 `/home/awe/p014-c/mesa/build-asm/src/intel/compiler`、run.sh:24）で byte の往復、Mesa の genxml（`GENXML`、既定 `/home/awe/p014-c/mesa/src/intel/genxml`、run.sh:25）で batch と IDD を decode（`tests/host/genxml-check.py`）。実行器の source は `src/drivers/gpu/i915/render/compute.c`・`command.c`、compiler は `src/drivers/gpu/i915/compiler/spirv.c`・`eu.c` |
| GLSL ES 3.10 の compute の誤り（再発時） | `plan/ws101/tests/glsl/run.sh`: 自前の compiler（`userland/desktop/libglesv2/glsl/*.c`）→ `spirv-val --target-env vulkan1.0` → i915 の host の compile（`tests/host/compute-dump.c`）→ brw の往復 → host の lavapipe（`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`、run.sh:155）で実行し C と照合。新しい shader は `tests/glsl/pass/*.comp`、断るべき物は `tests/glsl/fail/*.comp`（`// expect:` の行） |
| GPU が固まる（試験中） | serial・console の log で判定しない（AGENTS.md）。QEMU の `-S -gdb` で止め、i915 の error の register（EIR・IPEHR・ACTHD・INSTDONE）を monitor・gdb で読む（design.md §5.3）。同じ条件の変更無しの再試行は 3 回まで |

## 5. コマンド

general な build・boot test は [plan/ws104/commands.md](../ws104/commands.md) の §0（守ること）・§1（build）・§3（1 つの library）・§4（boot test）に従う。
以下は WS101 の物。script の行番号は 2026-10-01 に読んだ物。

### 5.1 host の試験（GPU も QEMU も要らない。2026-10-01 に 5 本とも PASS、各 1〜25 秒）

```
sh plan/ws101/tests/host/run.sh                      # 最後の行 "ws101 host test PASS"
sh plan/ws101/tests/glsl/run.sh build/<W>/glsl       # "ws101 glsl compute host test PASS"
sh plan/ws101/tests/gles/run.sh build/<W>/gles-host  # "ws101 gles host test PASS"
sh plan/ws068/tests/glsl-host/run.sh build/<W>/ws068-glsl-host   # WS068 の回帰 "glsl-host: PASS"
sh plan/ws068/tests/spirv-host/run.sh build/<W>/spirv-host       # "spirv-host: PASS"
```

- `host/run.sh` は作業の dir を `mktemp -d "${TMPDIR:-/tmp}/ws101-host.XXXXXX"`（run.sh:26）に作り、終わりに消す。
- 要る host の道具: `glslc`、`spirv-val`、`cc`、Mesa の `brw_disasm`・`brw_asm`・genxml（2026-10-05: 既定 path の `/home/awe/p014-c` は host から無くなった。無ければ `host/run.sh`・`glsl/run.sh`・`plan/ws075/tests/guard/run.sh` は往復と genxml の decode を「NOT RUN」と出し、それ以外の検査だけで PASS（制限付き）と言う。`BRW_TOOLS`・`GENXML` で作り直した build を指す）、lavapipe（`/usr/share/vulkan/icd.d/lvp_icd.json`）。

### 5.2 library と program だけの build（image は作らない）

```
make -j16 ZEDBSD_CONFIG=plan/ws101/tests/hw/g3/config.mk BUILD=build/<W>-lib \
    build/<W>-lib/dynamic/libGLESv2.so build/<W>-lib/dynamic/libEGL.so build/<W>-lib/bin/glescompute build/<W>-lib/bin/egltest
python3 plan/tools/style-check.py userland/desktop/libglesv2/*.c userland/desktop/libglesv2/gles.h   # 実行 bit が無いので python3 で
git diff --check
```

- `g3/config.mk` は noct を外している（`plan/ws101/tests/hw/g3/config.mk`）ので Noct は build されない。`make -n` で確かめた（noct・NoctLang・llvm の行 0）。
- p016・p017 は `BUILD=build/amd64` で build したが、**共有の `build/amd64` を上書きしないよう、自分の `build/<W>-lib` を使う**。

### 5.3 QEMU の Venus

image（1 つずつ。**他の image の build と同時に走らせない**）:

```
plan/ws101/tests/gles/build-image.sh build/<W>-img > build/<W>/img-build.log 2>&1; echo "exit=$?"
```

- `gles/build-image.sh:10` の既定は `build/ws101-p009-img`。この checkout には無い（2026-10-01）ので必ず作る。
- 構成は `plan/ws101/tests/gles/config-amd64-compute.mk`（WS068 の lean な GLSL の image + glescompute。noct・clang・libcxx を外す）。

GLES 3.1 の compute（G2）:

```
IMAGE=build/<W>-img/hdd-image.img SYMBOLS=build/<W>-img/vmunix GUEST_RUNTIME=$PWD/build/<W>-run \
    plan/ws101/tests/gles/venus.sh build/<W>/venus
```

- PASS: 最後の行 `ws101 venus: PASS`（auto・default・repeat 1000 の 3 つと compositor の生存、venus.sh:42〜66）。
- host の準備（host の起動ごとに 1 回）: `sudo modprobe vgem && sudo chmod 0666 /dev/dri/renderD128`。Venus の renderer は `build/ws035-sq-venus/install`（ある）。

G3（Noct の GPU の見本、正しさだけ。Venus の時間は参考にならない）:

```
NOCT=build/demo-lcd9/bin/noct IMAGE=build/<W>-img/hdd-image.img SYMBOLS=build/<W>-img/vmunix \
    GUEST_RUNTIME=$PWD/build/<W>-run plan/ws101/tests/noct/g3-venus.sh build/<W>/g3-venus
```

- PASS: `g3-venus: PASS`（CPU と GPU の `MIX check=1001 wrong=0`、checksum 一致、dispatch 数 ≥ ROUNDS、g3-venus.sh:53〜62）。
- `NOCT` は make に渡らないのでここでは安全。`N`（既定 4000000）・`ROUNDS`（既定 8）・`TENURE` を変えられる。

時間の分解（走っている guest に対して。guest は別に起こす）:

```
GUEST_RUNTIME=$PWD/build/<W>-run BIN=build/<W>-lib NOCT=build/demo-lcd9/bin/noct plan/ws101/tests/time-split.sh build/<W>/split 4000000 6
```

- guest には `/usr/share/gpudemo/mix.nct` が要る（time-split.sh:32〜33）。`gles/build-image.sh` の image には gpudemo が無いので、
  gpudemo 入りの image（`plan/ws081/tests/config-amd64-demo-win.mk` 系、p016・p017 は `build/ws081/demo-win-venus.img` の複写）を使う。

### 5.4 実機（5330 の QEMU VFIO passthrough）

全て `flock /tmp/i915-hw.lock` を内部で取る。**2 つの実機の試験を同時に走らせない。lock を手で取らない。**
`I915_HOST` の既定は `solaris10-man`（run-hw.sh:18、gles-hw.sh:16、g3-hw.sh:19）。`test-hw.sh` 単体の既定は `awe@10.0.30.3`（test-hw.sh:20）。

i915 の kernel の試験（G1 の vkcs と実行器の回帰）:

```
OUT=build/<W>-hw plan/ws101/tests/hw/run-hw.sh r1 vkcs vkx vke1 vke2 vkc
```

- 各場面の image を lock の外で `OUT/SCENARIO` に build（`VKLOOP_BUILD_ONLY=1`、上限 3000 s）してから、`plan/ws075/tests/test-hw.sh` で lock の下で走らせる（上限 3600 s、lock の待ちを含む）。
- vkcs は `I915_TEST_SET=compute`、他は `all`（run-hw.sh:28〜31）。走っている間は tree を編集しない（lock の中の make が拾う、run-hw.sh:6）。
- PASS: 終了 code 0。結果は `OUT/hw-SCENARIO-r1/`（vkcs 21/21、vkx 9/9、vke1 6/6、vke2 17/17、vkc 9/9 が前回）。

GLES 3.1 の compute（G2）と egltest feedback・queries:

```
BUILD=build/<W>-gles plan/ws101/tests/hw/gles-hw.sh build/<W>/gles-hw
```

- `GLES_HW_BUILD_ONLY=1` で image の build だけ（gles-hw.sh:32）。lock は copy・run・読み取りの間だけ（約 6 分）。
- PASS: 最後の行 `gles-hw: PASS`（`GLESCOMPUTE DONE failures=0` が 2 つ、EGLTEST CHECK feedback・queries、`GLES HW DONE`、gles-hw.sh:50〜54）。

G3（S13 の script と mix.nct の CPU・GPU、passthrough）:

```
# 提案 B（p020）の後
G3_NOCT=build/demo-lcd9/bin/noct BUILD=build/<W>-g3 plan/ws101/tests/hw/g3-hw.sh build/<W>/g3-hw
```

- **提案 B の前は `NOCT=…` を渡すと image の build が壊れ（2 章）、既定の path も無い**。直す前に走らせるなら、`build/<W>/g3-hw.sh` に複写して
  `noct=${NOCT:-…}` の行（g3-hw.sh:20）を `noct=${G3_NOCT:-$PWD/build/demo-lcd9/bin/noct}` に変えた複写を走らせる（元の file は変えない）。
- `G3_HW_BUILD_ONLY=1` で build だけ（g3-hw.sh:38）。PASS: `g3-hw: PASS`（「The results are the same」、両方 check 1001 wrong=0、checksum 一致、dispatch ≥ 8、g3-hw.sh:66〜72）。

S13 を demo の image の Terminal で（手の操作の再現、passthrough）:

```
# image（提案 B の後は S13_NOCT）
S13_NOCT=build/demo-lcd9/bin/noct plan/ws101/tests/demo/build-s13-image.sh build/<W>-demo
# run（lock は start〜stop、H4_MINUTES 既定 12、BOOT_S 既定 150、RUN_S 既定 60）
plan/ws101/tests/demo/s13-hw.sh build/<W>-demo/hdd-image.img build/<W>/s13-hw
```

- 構成は `plan/ws101/tests/demo/config.mk`（`plan/ws075/demo/config-demo-hdmi.mk` から noct を外した物。gpudemo は入る）。
- 判定は撮った PNG（`build/<W>/s13-hw/shots/*-s13-live.png`）を読む（s13-hw.sh:7）。Terminal の tile の座標 (1031,386) は `plan/ws035/demo/apps.conf` の 4 番目（s13-hw.sh:23〜24）。apps.conf の並びが変わったら座標を直す。
- PNG はユーザーに見せる。

## 6. 実機（Dell Latitude 5330）

一般の手順（USB への書き込み、起動、ssh、写真、lock）は [plan/tools/hw5330/README.md](../tools/hw5330/README.md)（作成中。2026-10-01 時点で未作成）を見る。
**素の 5330 の操作はユーザーが行う**（エージェントは実機の harness・lock に触らない時がある。指示に従う）。WS101 で頼む物:

| 確認 | 手順（ユーザー） | 期待する出力 |
| --- | --- | --- |
| S13（G4） | demo の image（main が `plan/ws075/demo/build-demo-image.sh build/demo-lcdN` で作る。構成 `config-demo-hdmi.mk` は `ZEDBSD_NOCT_ACCEL := y` と gpudemo を含む）を USB に書き、5330 を USB から起動 → kei の自動 login → Kei の button → App Home → Terminal → `sh /usr/share/gpudemo/s13.sh` | 「CPU: N ms a run」「GPU: N ms a run (6 kernels ran on the GPU)」「The CPU is X times as fast as the GPU.」「The results are the same on the CPU and the GPU.」。画面の写真 |
| 繰り返し | 同じ Terminal で 3 回 | 3 回とも「The results are the same」、Terminal と compositor が生きている |
| 時間の内訳（任意） | ssh で `cd /tmp; KEI_GLES_COMPUTE_TRACE=2 noct -O2 -j --gc-tenure-size=100000000 --gpu /usr/share/gpudemo/mix.nct 4000000 6 > /tmp/split-gpu.log 2>&1` と `--gpu` なしで `/tmp/split-cpu.log`、2 つを持ち帰る | `python3 plan/ws101/tests/time-split.py split-gpu.log split-cpu.log` の表 |

- demo の image の `/bin/noct` が accel 付きであることの確認: ssh で `noct --gpu-list` が `opengles:Kei OpenGL ES on Vulkan` を出す（phase011）。
- 記録は「素の 5330」と「passthrough」と「QEMU（Venus）」を分けて書く。やっていない確認は「未実施」。

## 7. 注意

- **Noct の accel**: `ZEDBSD_NOCT_ACCEL := y`（`userland/base/noct/Makefile:64〜67`、amd64 だけ）を置いた構成で noct を選ぶと Noct の target が build される。
  **Noct の build は toolchain（AGENTS.md）**。WS101 の試験の構成（`tests/hw/g3/config.mk`・`tests/hw/gles/config.mk`・`tests/demo/config.mk`・
  `tests/gles/config-amd64-compute.mk`）は noct を外してある。新しい構成を作るときも `$(filter-out noct,…)` を保つ。gpudemo の `Makefile` は
  noct を require しない（phase011 の注意: require したら worktree の target の Noct が build された）。
- **toolchain の lock**（`plan/tools/toolchain-lock.sh`）を外さない。Noct の patch・`accel_opengles.c` の変更が要るなら main に理由を送る。
- **実機の試験は 1 つずつ**。`/tmp/i915-hw.lock` は WS075 などと共有。lock を持つ script を `timeout` で包まない（`hdmi-h4-hw.sh` の先頭の注意: 待ちの間に殺すと lock が後で取られて QEMU が無いまま残る）。
- **image の build を 2 つ同時に走らせない**。BUILD は必ず自分の `build/<W>-*`。`boot-test.sh` の `OUTPUT` は専用の dir（rm -rf される）。
- **QEMU の console・serial の log で判定しない**。判定は script の PASS の行・guest の disk の log（`ufs-cat.py`）・PNG。
- **QEMU（Venus）の時間は実機の参考にならない**（host の lavapipe、F-064 の約 10 ms 刻み）。倍率の判定は素の 5330 の数だけ。
- `KEI_GLES_COMPUTE_TRACE` は既定で何も出さない。1 は dispatch の行、2 は時間の行（計測の時だけ）。デモの s13.sh は 1 を使う。
- 最適化（p017 の続き・p018）は**ユーザーが再開を言うまで実装しない**（「今のまま」）。
